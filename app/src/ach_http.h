// A minimal asynchronous HTTP client for RetroAchievements (#836).  SDL3 has no
// HTTP, so this wraps libcurl where it was found at configure time and WinHTTP
// on Windows; with neither, request() completes immediately with an error and
// the achievements UI says so (the emulator is unaffected).
//
// One worker thread runs the requests one after another.  Completions are
// queued and delivered by pump(), which the owner calls from its main loop, so
// every callback runs on the main thread: nothing here touches the emulation
// thread, and a slow or dead network never stalls a frame.
//
// Deliberately NOT shared with the library branch's HTTP helper (#838): the two
// features are developed in parallel and must not depend on each other.
#pragma once

#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

namespace vj {

struct HttpRequest {
    std::string url;
    std::string body;          // POST body; empty = GET
    std::string contentType;
    std::string userAgent;
};

struct HttpResponse {
    int status = 0;            // HTTP status; 0 = no response (see error)
    std::string body;
    std::string error;         // transport failure text, empty when a response arrived
};

class AchHttp {
public:
    AchHttp() = default;
    ~AchHttp();

    // True when this build can reach the network at all.
    static bool available();

    // Queue a request.  `done` runs later, inside pump(), on the caller's thread.
    void request(HttpRequest req, std::function<void(HttpResponse)> done);
    // Deliver finished requests.  Cheap when idle.
    void pump();
    // Stop the worker; pending completions are dropped (their callbacks never run).
    void shutdown();

private:
    struct Job {
        HttpRequest req;
        std::function<void(HttpResponse)> done;
    };
    struct Result {
        HttpResponse resp;
        std::function<void(HttpResponse)> done;
    };
    void worker();
    void ensureWorker();

    std::mutex mu_;
    std::condition_variable cv_;
    std::deque<Job> jobs_;
    std::deque<Result> results_;
    std::thread thread_;
    bool stop_ = false;
    bool started_ = false;
};

// One blocking request (used by the worker; exposed for tests).
HttpResponse httpPerform(const HttpRequest& req);

}  // namespace vj
