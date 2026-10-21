// Transport test for src/ach_http.cpp: a throwaway HTTP server on 127.0.0.1
// (POSIX sockets, one thread) checks what actually goes over the wire -- POST
// body, Content-Type, User-Agent -- and that the status, the body and a
// refused connection come back right, through the same queue the app uses
// (request -> worker -> pump on this thread).  No internet involved.
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>

#include "ach_http.h"

#if defined(_WIN32)
int main() {
    std::printf("ach_http_test: skipped on Windows (POSIX socket server)\n");
    return 77;
}
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace {

std::string g_seen;   // the request the server received

int listenOnFreePort(int& port) {
    int s = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in a;
    std::memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.sin_port = 0;
    if (bind(s, (sockaddr*)&a, sizeof a) != 0 || listen(s, 4) != 0) return -1;
    socklen_t len = sizeof a;
    getsockname(s, (sockaddr*)&a, &len);
    port = ntohs(a.sin_port);
    return s;
}

void serveOne(int ls) {
    int c = accept(ls, nullptr, nullptr);
    if (c < 0) return;
    std::string req;
    char buf[4096];
    size_t need = 0;
    for (;;) {
        ssize_t n = recv(c, buf, sizeof buf, 0);
        if (n <= 0) break;
        req.append(buf, (size_t)n);
        size_t hdrEnd = req.find("\r\n\r\n");
        if (hdrEnd == std::string::npos) continue;
        if (!need) {
            size_t cl = req.find("Content-Length: ");
            need = hdrEnd + 4 + (cl == std::string::npos ? 0 : (size_t)std::atoi(req.c_str() + cl + 16));
        }
        if (req.size() >= need) break;
    }
    g_seen = req;
    const char* body = "{\"Success\":true}";
    std::string resp = "HTTP/1.1 201 Created\r\nContent-Type: application/json\r\nContent-Length: " +
                       std::to_string(std::strlen(body)) + "\r\nConnection: close\r\n\r\n" + body;
    send(c, resp.data(), resp.size(), 0);
    close(c);
}

int fails = 0;
void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) fails++;
}

}  // namespace

int main() {
    if (!vj::AchHttp::available()) {
        std::printf("ach_http_test: no HTTP transport in this build (libcurl not found): skipped\n");
        return 77;
    }
    int port = 0;
    int ls = listenOnFreePort(port);
    if (ls < 0) {
        std::printf("FAIL cannot listen on loopback\n");
        return 1;
    }
    std::thread srv(serveOne, ls);

    vj::AchHttp http;
    std::atomic<bool> done{false};
    vj::HttpResponse got;
    vj::HttpRequest rq;
    rq.url = "http://127.0.0.1:" + std::to_string(port) + "/dorequest.php";
    rq.body = "r=ping&u=Someone&t=token123";
    rq.contentType = "application/x-www-form-urlencoded";
    rq.userAgent = "VirtualJaguar/test rcheevos/x";
    http.request(rq, [&](vj::HttpResponse r) {
        got = std::move(r);
        done = true;
    });
    for (int i = 0; i < 500 && !done; i++) {   // up to 5 s, pumping like the UI loop does
        http.pump();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    srv.join();
    close(ls);
    check(done, "the completion callback ran (on this thread, via pump)");
    check(got.status == 201, "HTTP status is delivered");
    check(got.body == "{\"Success\":true}", "response body is delivered");
    check(g_seen.compare(0, 24, "POST /dorequest.php HTTP") == 0, "it was a POST to the path");
    check(g_seen.find("r=ping&u=Someone&t=token123") != std::string::npos, "the POST body arrived intact");
    check(g_seen.find("application/x-www-form-urlencoded") != std::string::npos, "Content-Type was sent");
    check(g_seen.find("VirtualJaguar/test rcheevos/x") != std::string::npos, "User-Agent was sent");

    // Nothing is listening any more: a transport failure, not a hang.
    vj::HttpResponse refused = vj::httpPerform(rq);
    check(refused.status == 0 && !refused.error.empty(), "a refused connection reports an error and no status");

    // request() is called from two threads at once (the UI thread, and the emulation thread through rc_client's server
    // calls), the first ones racing to start the worker: every callback is delivered by pump(), nothing crashes.
    {
        vj::AchHttp shared;
        std::atomic<int> delivered{0};
        std::atomic<bool> go{false};
        auto fire = [&] {
            while (!go) std::this_thread::yield();
            for (int i = 0; i < 20; i++) {
                shared.request(rq, [&](vj::HttpResponse) { delivered++; });
            }
        };
        std::thread t1(fire), t2(fire);
        go = true;
        for (int i = 0; i < 1500 && delivered < 40; i++) {   // up to 15 s
            shared.pump();
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        t1.join();
        t2.join();
        check(delivered == 40, "40 requests from two threads were all delivered through pump");
        shared.shutdown();
    }

    return fails ? 1 : 0;
}
#endif
