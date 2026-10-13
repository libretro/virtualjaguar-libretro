#include "http.h"

#include <csignal>
#include <cstring>
#include <mutex>

#if defined(VJ_HTTP_CURL)
#include <curl/curl.h>
#elif defined(VJ_HTTP_WINHTTP)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winhttp.h>
#endif

namespace vj {
namespace http {

namespace {
[[maybe_unused]] bool schemeOk(const std::string& url) {
    return url.compare(0, 8, "https://") == 0 || url.compare(0, 7, "http://") == 0;
}
}  // namespace

#if defined(VJ_HTTP_CURL)

namespace {

struct Ctx {
    Response* resp;
    size_t maxBytes;
    const std::atomic<bool>* cancel;
    bool tooBig = false;
};

size_t onWrite(char* p, size_t sz, size_t n, void* ud) {
    Ctx* c = (Ctx*)ud;
    size_t len = sz * n;
    if (c->resp->body.size() + len > c->maxBytes) {
        c->tooBig = true;
        return 0;   // aborts the transfer
    }
    c->resp->body.insert(c->resp->body.end(), (unsigned char*)p, (unsigned char*)p + len);
    return len;
}

int onProgress(void* ud, curl_off_t, curl_off_t, curl_off_t, curl_off_t) {
    Ctx* c = (Ctx*)ud;
    return (c->cancel && c->cancel->load()) ? 1 : 0;
}

std::once_flag g_curlInit;

}  // namespace

bool available() { return true; }
const char* backendName() { return "libcurl"; }

bool get(const std::string& url, Response& resp, size_t maxBytes, const std::atomic<bool>* cancel) {
    resp = Response();
    if (!schemeOk(url)) {
        resp.error = "unsupported URL scheme";
        return false;
    }
    std::call_once(g_curlInit, [] {
        curl_global_init(CURL_GLOBAL_DEFAULT);
#ifndef _WIN32
        // CURLOPT_NOSIGNAL (set below, required for threads) also stops libcurl guarding against
        // SIGPIPE; a server dropping a TLS connection must not be able to kill a running game.
        std::signal(SIGPIPE, SIG_IGN);
#endif
    });
    CURL* h = curl_easy_init();
    if (!h) {
        resp.error = "curl_easy_init failed";
        return false;
    }
    Ctx ctx{&resp, maxBytes, cancel};
    char errbuf[CURL_ERROR_SIZE] = {0};
    curl_easy_setopt(h, CURLOPT_URL, url.c_str());
    curl_easy_setopt(h, CURLOPT_USERAGENT, "VirtualJaguar/" VJ_APP_VERSION " (+https://github.com/libretro/virtualjaguar-libretro)");
    curl_easy_setopt(h, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(h, CURLOPT_MAXREDIRS, 5L);
#if LIBCURL_VERSION_NUM >= 0x075500
    curl_easy_setopt(h, CURLOPT_PROTOCOLS_STR, "http,https");
    curl_easy_setopt(h, CURLOPT_REDIR_PROTOCOLS_STR, "http,https");
#endif
    curl_easy_setopt(h, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(h, CURLOPT_LOW_SPEED_LIMIT, 1L);   // stalled for 20 s -> give up
    curl_easy_setopt(h, CURLOPT_LOW_SPEED_TIME, 20L);
    curl_easy_setopt(h, CURLOPT_TIMEOUT, 120L);
    curl_easy_setopt(h, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(h, CURLOPT_ACCEPT_ENCODING, "");
    curl_easy_setopt(h, CURLOPT_WRITEFUNCTION, onWrite);
    curl_easy_setopt(h, CURLOPT_WRITEDATA, &ctx);
    curl_easy_setopt(h, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(h, CURLOPT_XFERINFOFUNCTION, onProgress);
    curl_easy_setopt(h, CURLOPT_XFERINFODATA, &ctx);
    curl_easy_setopt(h, CURLOPT_ERRORBUFFER, errbuf);
    CURLcode rc = curl_easy_perform(h);
    long status = 0;
    curl_easy_getinfo(h, CURLINFO_RESPONSE_CODE, &status);
    curl_easy_cleanup(h);
    if (rc != CURLE_OK) {
        if (ctx.tooBig) resp.error = "response too large";
        else if (cancel && cancel->load()) resp.error = "cancelled";
        else resp.error = errbuf[0] ? errbuf : curl_easy_strerror(rc);
        resp.body.clear();
        return false;
    }
    resp.status = (int)status;
    return true;
}

#elif defined(VJ_HTTP_WINHTTP)

namespace {

std::wstring widen(const std::string& s) {
    if (s.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w((size_t)n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
    return w;
}

struct Handle {
    HINTERNET h = nullptr;
    explicit Handle(HINTERNET v) : h(v) {}
    ~Handle() {
        if (h) WinHttpCloseHandle(h);
    }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
};

}  // namespace

bool available() { return true; }
const char* backendName() { return "WinHTTP"; }

bool get(const std::string& url, Response& resp, size_t maxBytes, const std::atomic<bool>* cancel) {
    resp = Response();
    if (!schemeOk(url)) {
        resp.error = "unsupported URL scheme";
        return false;
    }
    std::wstring wurl = widen(url);
    URL_COMPONENTS uc;
    std::memset(&uc, 0, sizeof uc);
    uc.dwStructSize = sizeof uc;
    uc.dwSchemeLength = (DWORD)-1;
    uc.dwHostNameLength = (DWORD)-1;
    uc.dwUrlPathLength = (DWORD)-1;
    uc.dwExtraInfoLength = (DWORD)-1;
    if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &uc)) {
        resp.error = "bad URL";
        return false;
    }
    std::wstring host(uc.lpszHostName, uc.dwHostNameLength);
    std::wstring path(uc.lpszUrlPath, uc.dwUrlPathLength);
    path.append(uc.lpszExtraInfo, uc.dwExtraInfoLength);
    bool secure = uc.nScheme == INTERNET_SCHEME_HTTPS;

    Handle session(WinHttpOpen(L"VirtualJaguar/" VJ_APP_VERSION, WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
                               WINHTTP_NO_PROXY_BYPASS, 0));
    if (!session.h) {
        resp.error = "WinHttpOpen failed";
        return false;
    }
    // resolve, connect, send, receive; each phase gets its own bound
    WinHttpSetTimeouts(session.h, 10000, 10000, 20000, 20000);
    DWORD maxRedir = 5;
    WinHttpSetOption(session.h, WINHTTP_OPTION_MAX_HTTP_AUTOMATIC_REDIRECTS, &maxRedir, sizeof maxRedir);
    Handle conn(WinHttpConnect(session.h, host.c_str(), uc.nPort, 0));
    if (!conn.h) {
        resp.error = "WinHttpConnect failed";
        return false;
    }
    Handle req(WinHttpOpenRequest(conn.h, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER,
                                  WINHTTP_DEFAULT_ACCEPT_TYPES, secure ? WINHTTP_FLAG_SECURE : 0));
    if (!req.h) {
        resp.error = "WinHttpOpenRequest failed";
        return false;
    }
    if (!WinHttpSendRequest(req.h, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(req.h, nullptr)) {
        resp.error = "request failed (error " + std::to_string((unsigned long)GetLastError()) + ")";
        return false;
    }
    DWORD status = 0, sz = sizeof status;
    if (!WinHttpQueryHeaders(req.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                             WINHTTP_HEADER_NAME_BY_INDEX, &status, &sz, WINHTTP_NO_HEADER_INDEX)) {
        resp.error = "no status line";
        return false;
    }
    std::vector<unsigned char> chunk(64 * 1024);
    for (;;) {
        if (cancel && cancel->load()) {
            resp.error = "cancelled";
            resp.body.clear();
            return false;
        }
        DWORD got = 0;
        if (!WinHttpReadData(req.h, chunk.data(), (DWORD)chunk.size(), &got)) {
            resp.error = "read failed (error " + std::to_string((unsigned long)GetLastError()) + ")";
            resp.body.clear();
            return false;
        }
        if (got == 0) break;
        if (resp.body.size() + got > maxBytes) {
            resp.error = "response too large";
            resp.body.clear();
            return false;
        }
        resp.body.insert(resp.body.end(), chunk.begin(), chunk.begin() + got);
    }
    resp.status = (int)status;
    return true;
}

#else  // no backend

bool available() { return false; }
const char* backendName() { return "none"; }

bool get(const std::string& url, Response& resp, size_t, const std::atomic<bool>*) {
    resp = Response();
    (void)url;
    resp.error = "this build has no HTTP support";
    return false;
}

#endif

}  // namespace http
}  // namespace vj
