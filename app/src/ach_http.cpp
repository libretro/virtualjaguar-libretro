#include "ach_http.h"

#include <atomic>
#include <mutex>
#include <utility>
#include <vector>

#if defined(VJ_HAVE_CURL)
#include <curl/curl.h>
#elif defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <winhttp.h>
#endif

namespace vj {

namespace {
// Set by shutdown(): lets a libcurl transfer in flight stop at once, so quitting
// the app is never held up by a dead network.
std::atomic<bool> g_abort{false};
}  // namespace

// ---------------------------------------------------------------------------------
// transport
// ---------------------------------------------------------------------------------
#if defined(VJ_HAVE_CURL)

bool AchHttp::available() { return true; }

namespace {

size_t curlWrite(char* p, size_t sz, size_t n, void* ud) {
    static_cast<std::string*>(ud)->append(p, sz * n);
    return sz * n;
}

int curlProgress(void*, curl_off_t, curl_off_t, curl_off_t, curl_off_t) { return g_abort.load() ? 1 : 0; }

}  // namespace

HttpResponse httpPerform(const HttpRequest& r) {
    static std::once_flag once;
    std::call_once(once, [] { curl_global_init(CURL_GLOBAL_DEFAULT); });

    HttpResponse out;
    CURL* c = curl_easy_init();
    if (!c) {
        out.error = "could not initialise libcurl";
        return out;
    }
    char err[CURL_ERROR_SIZE];
    err[0] = 0;
    struct curl_slist* hdr = nullptr;
    curl_easy_setopt(c, CURLOPT_URL, r.url.c_str());
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, curlWrite);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &out.body);
    curl_easy_setopt(c, CURLOPT_ERRORBUFFER, err);
    curl_easy_setopt(c, CURLOPT_USERAGENT, r.userAgent.c_str());
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(c, CURLOPT_MAXREDIRS, 3L);
    curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(c, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(c, CURLOPT_XFERINFOFUNCTION, curlProgress);
    if (!r.body.empty()) {
        curl_easy_setopt(c, CURLOPT_POST, 1L);
        curl_easy_setopt(c, CURLOPT_POSTFIELDS, r.body.c_str());
        curl_easy_setopt(c, CURLOPT_POSTFIELDSIZE_LARGE, (curl_off_t)r.body.size());
        if (!r.contentType.empty()) {
            std::string h = "Content-Type: " + r.contentType;
            hdr = curl_slist_append(hdr, h.c_str());
            curl_easy_setopt(c, CURLOPT_HTTPHEADER, hdr);
        }
    }
    CURLcode rc = curl_easy_perform(c);
    if (rc != CURLE_OK) {
        out.error = err[0] ? err : curl_easy_strerror(rc);
        out.body.clear();
    } else {
        long code = 0;
        curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &code);
        out.status = (int)code;
    }
    if (hdr) curl_slist_free_all(hdr);
    curl_easy_cleanup(c);
    return out;
}

#elif defined(_WIN32)

bool AchHttp::available() { return true; }

namespace {

std::wstring widen(const std::string& s) {
    if (s.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w((size_t)(n > 0 ? n : 0), L'\0');
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
    return w;
}

struct WinHandle {
    HINTERNET h = nullptr;
    explicit WinHandle(HINTERNET x = nullptr) : h(x) {}
    ~WinHandle() {
        if (h) WinHttpCloseHandle(h);
    }
    WinHandle(const WinHandle&) = delete;
    WinHandle& operator=(const WinHandle&) = delete;
};

}  // namespace

HttpResponse httpPerform(const HttpRequest& r) {
    HttpResponse out;
    std::wstring wurl = widen(r.url);
    URL_COMPONENTS uc;
    ZeroMemory(&uc, sizeof uc);
    uc.dwStructSize = sizeof uc;
    uc.dwHostNameLength = (DWORD)-1;      // point into wurl instead of copying
    uc.dwUrlPathLength = (DWORD)-1;
    uc.dwExtraInfoLength = (DWORD)-1;
    if (!WinHttpCrackUrl(wurl.c_str(), (DWORD)wurl.size(), 0, &uc) || !uc.lpszHostName) {
        out.error = "invalid URL";
        return out;
    }
    std::wstring host(uc.lpszHostName, uc.dwHostNameLength);
    std::wstring path;
    if (uc.lpszUrlPath) path.assign(uc.lpszUrlPath, uc.dwUrlPathLength);
    if (uc.lpszExtraInfo) path.append(uc.lpszExtraInfo, uc.dwExtraInfoLength);
    if (path.empty()) path = L"/";
    const bool secure = (uc.nScheme == INTERNET_SCHEME_HTTPS);

    WinHandle session(WinHttpOpen(widen(r.userAgent).c_str(), WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                  WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    if (!session.h) {
        out.error = "WinHttpOpen failed";
        return out;
    }
    WinHttpSetTimeouts(session.h, 10000, 10000, 30000, 30000);
    WinHandle conn(WinHttpConnect(session.h, host.c_str(), uc.nPort, 0));
    if (!conn.h) {
        out.error = "WinHttpConnect failed";
        return out;
    }
    const bool post = !r.body.empty();
    WinHandle req(WinHttpOpenRequest(conn.h, post ? L"POST" : L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER,
                                     WINHTTP_DEFAULT_ACCEPT_TYPES, secure ? WINHTTP_FLAG_SECURE : 0));
    if (!req.h) {
        out.error = "WinHttpOpenRequest failed";
        return out;
    }
    std::wstring headers;
    if (post && !r.contentType.empty()) headers = L"Content-Type: " + widen(r.contentType);
    BOOL ok = WinHttpSendRequest(req.h, headers.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : headers.c_str(),
                                 headers.empty() ? 0 : (DWORD)-1L,
                                 post ? (LPVOID)r.body.data() : WINHTTP_NO_REQUEST_DATA,
                                 post ? (DWORD)r.body.size() : 0, post ? (DWORD)r.body.size() : 0, 0);
    if (!ok || !WinHttpReceiveResponse(req.h, nullptr)) {
        out.error = "request failed (error " + std::to_string((unsigned long)GetLastError()) + ")";
        return out;
    }
    DWORD status = 0, sz = sizeof status;
    WinHttpQueryHeaders(req.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                        &status, &sz, WINHTTP_NO_HEADER_INDEX);
    out.status = (int)status;
    for (;;) {
        DWORD avail = 0;
        if (!WinHttpQueryDataAvailable(req.h, &avail) || avail == 0) break;
        std::vector<char> buf(avail);
        DWORD got = 0;
        if (!WinHttpReadData(req.h, buf.data(), avail, &got) || got == 0) break;
        out.body.append(buf.data(), got);
    }
    return out;
}

#else

bool AchHttp::available() { return false; }

HttpResponse httpPerform(const HttpRequest&) {
    HttpResponse out;
    out.error = "this build has no HTTP support (libcurl was not found at configure time)";
    return out;
}

#endif

// ---------------------------------------------------------------------------------
// worker + queue
// ---------------------------------------------------------------------------------
AchHttp::~AchHttp() { shutdown(); }

void AchHttp::ensureWorker() {
    if (started_) return;
    started_ = true;
    thread_ = std::thread([this] { worker(); });
}

void AchHttp::request(HttpRequest req, std::function<void(HttpResponse)> done) {
    if (!available()) {
        // No transport: answer from here, but still through pump() so callers
        // see the same asynchronous contract either way.
        HttpResponse r = httpPerform(req);
        std::lock_guard<std::mutex> lk(mu_);
        results_.push_back(Result{std::move(r), std::move(done)});
        return;
    }
    {
        std::lock_guard<std::mutex> lk(mu_);
        if (stop_) return;
        jobs_.push_back(Job{std::move(req), std::move(done)});
    }
    ensureWorker();
    cv_.notify_one();
}

void AchHttp::worker() {
    for (;;) {
        Job job;
        {
            std::unique_lock<std::mutex> lk(mu_);
            cv_.wait(lk, [this] { return stop_ || !jobs_.empty(); });
            if (stop_) return;
            job = std::move(jobs_.front());
            jobs_.pop_front();
        }
        HttpResponse resp = httpPerform(job.req);
        {
            std::lock_guard<std::mutex> lk(mu_);
            if (stop_) return;
            results_.push_back(Result{std::move(resp), std::move(job.done)});
        }
    }
}

void AchHttp::pump() {
    std::deque<Result> ready;
    {
        std::lock_guard<std::mutex> lk(mu_);
        if (results_.empty()) return;
        ready.swap(results_);
    }
    for (Result& r : ready)
        if (r.done) r.done(std::move(r.resp));
}

void AchHttp::shutdown() {
    g_abort = true;
    {
        std::lock_guard<std::mutex> lk(mu_);
        stop_ = true;
        jobs_.clear();
        results_.clear();
    }
    cv_.notify_all();
    if (thread_.joinable()) thread_.join();   // libcurl aborts at once; WinHTTP finishes (<= its 30 s timeout)
    started_ = false;
    g_abort = false;
}

}  // namespace vj
