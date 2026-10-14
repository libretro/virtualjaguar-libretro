// Discord local RPC client: framing, paths, rate limit, transports, and the worker thread.  See discord_ipc.h.
#include "discord_ipc.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <thread>

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#else
#  include <errno.h>
#  include <fcntl.h>
#  include <poll.h>
#  include <sys/socket.h>
#  include <sys/types.h>
#  include <sys/un.h>
#  include <unistd.h>
#endif

namespace vj {
namespace discord {

// ---------------------------------------------------------------------------------------------
// frames
// ---------------------------------------------------------------------------------------------
static void putU32(std::string& s, uint32_t v) {
    s.push_back((char)(v & 0xFF));
    s.push_back((char)((v >> 8) & 0xFF));
    s.push_back((char)((v >> 16) & 0xFF));
    s.push_back((char)((v >> 24) & 0xFF));
}

static uint32_t getU32(const char* p) {
    return (uint32_t)(unsigned char)p[0] | ((uint32_t)(unsigned char)p[1] << 8) | ((uint32_t)(unsigned char)p[2] << 16) |
           ((uint32_t)(unsigned char)p[3] << 24);
}

std::string encodeFrame(uint32_t op, const std::string& payload) {
    std::string out;
    out.reserve(8 + payload.size());
    putU32(out, op);
    putU32(out, (uint32_t)payload.size());
    out += payload;
    return out;
}

void FrameDecoder::feed(const void* data, size_t n) {
    if (bad_ || !n) return;
    buf_.append((const char*)data, n);
}

bool FrameDecoder::next(Frame& out) {
    if (bad_ || buf_.size() < 8) return false;
    const uint32_t op = getU32(buf_.data());
    const uint32_t len = getU32(buf_.data() + 4);
    if (len > kMaxPayload) {
        bad_ = true;
        buf_.clear();
        return false;
    }
    if (buf_.size() < 8 + (size_t)len) return false;
    out.op = op;
    out.payload.assign(buf_, 8, len);
    buf_.erase(0, 8 + (size_t)len);
    return true;
}

// ---------------------------------------------------------------------------------------------
// JSON helpers
// ---------------------------------------------------------------------------------------------
std::string jsonEscape(const std::string& s) {
    std::string o;
    o.reserve(s.size() + 8);
    for (char ch : s) {
        const unsigned char c = (unsigned char)ch;
        switch (c) {
        case '"': o += "\\\""; break;
        case '\\': o += "\\\\"; break;
        case '\n': o += "\\n"; break;
        case '\r': o += "\\r"; break;
        case '\t': o += "\\t"; break;
        default:
            if (c < 0x20 || c == 0x7F) {
                char b[8];
                std::snprintf(b, sizeof b, "\\u%04x", (unsigned)c);
                o += b;
            } else {
                o.push_back(ch);
            }
        }
    }
    return o;
}

std::string jsonString(const std::string& s) { return "\"" + jsonEscape(s) + "\""; }

std::string truncateUtf8(const std::string& s, size_t maxBytes) {
    if (s.size() <= maxBytes) return s;
    size_t n = maxBytes;
    // Back up over continuation bytes (10xxxxxx) so the cut lands on a character boundary.
    while (n > 0 && ((unsigned char)s[n] & 0xC0) == 0x80) n--;
    return s.substr(0, n);
}

static size_t findKey(const std::string& json, const char* key) {
    const std::string pat = std::string("\"") + key + "\"";
    size_t p = 0;
    while ((p = json.find(pat, p)) != std::string::npos) {
        size_t q = p + pat.size();
        while (q < json.size() && (json[q] == ' ' || json[q] == '\t')) q++;
        if (q < json.size() && json[q] == ':') {
            q++;
            while (q < json.size() && (json[q] == ' ' || json[q] == '\t')) q++;
            return q;
        }
        p += pat.size();
    }
    return std::string::npos;
}

bool jsonFindString(const std::string& json, const char* key, std::string& out) {
    size_t q = findKey(json, key);
    if (q == std::string::npos || q >= json.size() || json[q] != '"') return false;
    q++;
    out.clear();
    while (q < json.size() && json[q] != '"') {
        if (json[q] == '\\' && q + 1 < json.size()) {
            q++;
            switch (json[q]) {
            case 'n': out.push_back('\n'); break;
            case 't': out.push_back('\t'); break;
            case 'r': out.push_back('\r'); break;
            default: out.push_back(json[q]); break;   // \" \\ \/ ; \uXXXX is left to the caller (never needed here)
            }
        } else {
            out.push_back(json[q]);
        }
        q++;
    }
    return q < json.size();
}

bool jsonFindInt(const std::string& json, const char* key, long long& out) {
    size_t q = findKey(json, key);
    if (q == std::string::npos || q >= json.size()) return false;
    bool neg = false;
    if (json[q] == '-') { neg = true; q++; }
    if (q >= json.size() || json[q] < '0' || json[q] > '9') return false;
    long long v = 0;
    while (q < json.size() && json[q] >= '0' && json[q] <= '9' && v < 100000000000000000LL) v = v * 10 + (json[q++] - '0');
    out = neg ? -v : v;
    return true;
}

std::string handshakePayload(const std::string& appId) { return "{\"v\":1,\"client_id\":" + jsonString(appId) + "}"; }

std::string setActivityPayload(unsigned long pid, const std::string& nonce, const std::string& activityJson) {
    return "{\"cmd\":\"SET_ACTIVITY\",\"args\":{\"pid\":" + std::to_string(pid) +
           ",\"activity\":" + (activityJson.empty() ? std::string("null") : activityJson) + "},\"nonce\":" + jsonString(nonce) + "}";
}

// ---------------------------------------------------------------------------------------------
// paths
// ---------------------------------------------------------------------------------------------
Platform hostPlatform() {
#ifdef _WIN32
    return Platform::Windows;
#else
    return Platform::Unix;
#endif
}

std::vector<std::string> socketCandidates(Platform p, const std::function<std::string(const char*)>& getenv) {
    std::vector<std::string> out;
    auto add = [&](const std::string& s) {
        if (std::find(out.begin(), out.end(), s) == out.end()) out.push_back(s);
    };
    if (p == Platform::Windows) {
        for (int n = 0; n < 10; n++) add("\\\\.\\pipe\\discord-ipc-" + std::to_string(n));
        return out;
    }
    struct Base {
        std::string dir;
        bool xdg;
    };
    std::vector<Base> bases;
    const char* const names[] = {"XDG_RUNTIME_DIR", "TMPDIR", "TMP", "TEMP"};
    for (const char* name : names) {
        std::string d = getenv ? getenv(name) : std::string();
        while (!d.empty() && d.back() == '/') d.pop_back();   // "/tmp/" must not become "/tmp//discord-ipc-0"
        if (d.empty()) continue;
        bases.push_back({d, std::strcmp(name, "XDG_RUNTIME_DIR") == 0});
    }
    bases.push_back({"/tmp", false});
    for (int n = 0; n < 10; n++) {
        const std::string leaf = "/discord-ipc-" + std::to_string(n);
        for (const Base& b : bases) {
            add(b.dir + leaf);
            if (b.xdg) {   // the Flatpak and Snap Discord keep their socket in a folder of their own
                add(b.dir + "/app/com.discordapp.Discord" + leaf);
                add(b.dir + "/snap.discord" + leaf);
            }
        }
    }
    return out;
}

// ---------------------------------------------------------------------------------------------
// rate limiter
// ---------------------------------------------------------------------------------------------
void RateLimiter::expire(double now) {
    while (!times_.empty() && now - times_.front() >= window_) times_.pop_front();
}

bool RateLimiter::tryAcquire(double now) {
    expire(now);
    if (times_.size() >= max_) return false;
    times_.push_back(now);
    return true;
}

double RateLimiter::waitSeconds(double now) {
    expire(now);
    if (times_.size() < max_) return 0;
    return std::max(0.0, window_ - (now - times_.front()));
}

// ---------------------------------------------------------------------------------------------
// transports
// ---------------------------------------------------------------------------------------------
namespace {

using Clock = std::chrono::steady_clock;
double nowSeconds() { return std::chrono::duration<double>(Clock::now().time_since_epoch()).count(); }

#ifdef _WIN32

class PipeTransport : public Transport {
public:
    explicit PipeTransport(HANDLE h) : h_(h) {}
    ~PipeTransport() override { if (h_ != INVALID_HANDLE_VALUE) CloseHandle(h_); }
    bool write(const void* data, size_t n, int timeoutMs) override {
        (void)timeoutMs;   // a blocking write; the client's bounded join covers a wedged Discord
        const char* p = (const char*)data;
        while (n) {
            DWORD w = 0;
            if (!WriteFile(h_, p, (DWORD)n, &w, nullptr) || w == 0) return false;
            p += w;
            n -= w;
        }
        return true;
    }
    int read(void* buf, size_t cap, int timeoutMs) override {
        const double deadline = nowSeconds() + timeoutMs / 1000.0;
        for (;;) {
            DWORD avail = 0;
            if (!PeekNamedPipe(h_, nullptr, 0, nullptr, &avail, nullptr)) return -1;   // broken pipe: Discord went away
            if (avail > 0) {
                DWORD got = 0;
                if (!ReadFile(h_, buf, (DWORD)std::min<size_t>(cap, avail), &got, nullptr)) return -1;
                return (int)got;
            }
            if (nowSeconds() >= deadline) return 0;
            Sleep(5);
        }
    }

private:
    HANDLE h_;
};

}  // namespace

std::unique_ptr<Transport> connectTransport(const std::string& path) {
    HANDLE h = CreateFileA(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return nullptr;
    return std::unique_ptr<Transport>(new PipeTransport(h));
}

namespace {

#else   // POSIX

class UnixTransport : public Transport {
public:
    explicit UnixTransport(int fd) : fd_(fd) {}
    ~UnixTransport() override { if (fd_ >= 0) ::close(fd_); }
    bool write(const void* data, size_t n, int timeoutMs) override {
        const double deadline = nowSeconds() + timeoutMs / 1000.0;
        const char* p = (const char*)data;
        while (n) {
#ifdef MSG_NOSIGNAL
            ssize_t w = ::send(fd_, p, n, MSG_NOSIGNAL);   // Linux: a dead peer is an error, not SIGPIPE
#else
            ssize_t w = ::send(fd_, p, n, 0);              // macOS: SO_NOSIGPIPE was set on the socket
#endif
            if (w > 0) {
                p += w;
                n -= (size_t)w;
                continue;
            }
            if (w < 0 && errno == EINTR) continue;
            if (w < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                const double left = deadline - nowSeconds();
                if (left <= 0) return false;
                struct pollfd pf = {fd_, POLLOUT, 0};
                ::poll(&pf, 1, (int)(left * 1000) + 1);
                continue;
            }
            return false;
        }
        return true;
    }
    int read(void* buf, size_t cap, int timeoutMs) override {
        struct pollfd pf = {fd_, POLLIN, 0};
        int r = ::poll(&pf, 1, timeoutMs);
        if (r < 0) return errno == EINTR ? 0 : -1;
        if (r == 0) return 0;
        ssize_t g = ::recv(fd_, buf, cap, 0);
        if (g > 0) return (int)g;
        if (g < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) return 0;
        return -1;   // 0 = orderly close
    }

private:
    int fd_;
};

}  // namespace

std::unique_ptr<Transport> connectTransport(const std::string& path) {
    struct sockaddr_un addr;
    std::memset(&addr, 0, sizeof addr);
    addr.sun_family = AF_UNIX;
    if (path.empty() || path.size() >= sizeof addr.sun_path) return nullptr;   // 104 bytes on macOS
    std::memcpy(addr.sun_path, path.c_str(), path.size() + 1);
    int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return nullptr;
#ifdef SO_NOSIGPIPE
    int one = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
#endif
    ::fcntl(fd, F_SETFD, FD_CLOEXEC);
    ::fcntl(fd, F_SETFL, ::fcntl(fd, F_GETFL, 0) | O_NONBLOCK);   // a full backlog must not block the worker
    if (::connect(fd, (struct sockaddr*)&addr, sizeof addr) != 0) {
        ::close(fd);
        return nullptr;
    }
    return std::unique_ptr<Transport>(new UnixTransport(fd));
}

namespace {

#endif

}  // namespace

// ---------------------------------------------------------------------------------------------
// the client
// ---------------------------------------------------------------------------------------------
struct Client::Shared {
    std::mutex m;
    std::condition_variable cv;        // wakes the worker (new activity, stop)
    std::condition_variable doneCv;    // the worker finished
    Options opt;
    bool started = false;
    bool quit = false;
    bool done = false;
    bool wantedSet = false;            // setActivity was ever called
    bool dirty = false;
    std::string wanted, fallback;
    std::string lastSent;
    std::atomic<int> status{(int)Status::Stopped};
    std::atomic<unsigned> sent{0}, errors{0};
    std::thread th;
};

namespace {

// Everything the worker owns.  Free functions on a shared_ptr<Shared>: the worker can outlive stop() (detached
// when Discord is wedged), so it must not touch the Client.
struct Conn {
    std::unique_ptr<Transport> t;
    FrameDecoder dec;
    bool sentOnThis = false;           // something has been written on this connection (a clear is only needed then)
    std::string pendingFallback;       // fallback for the last SET_ACTIVITY written, until it is answered
    std::string lastNonce;
};

bool writeFrame(Conn& c, uint32_t op, const std::string& payload, int timeoutMs) {
    const std::string f = encodeFrame(op, payload);
    return c.t->write(f.data(), f.size(), timeoutMs);
}

enum class React { Ok, Dropped, Fatal };

// One decoded frame outside the handshake.
React handleFrame(Conn& c, const Frame& f, int ioMs, std::string& errorEvt, long long& closeCode) {
    errorEvt.clear();
    switch (f.op) {
    case OpPing: return writeFrame(c, OpPong, f.payload, ioMs) ? React::Ok : React::Dropped;
    case OpClose:
        closeCode = 0;
        jsonFindInt(f.payload, "code", closeCode);
        return closeCode == 4000 ? React::Fatal : React::Dropped;
    case OpFrame: {
        std::string evt;
        if (jsonFindString(f.payload, "evt", evt) && evt == "ERROR") errorEvt = f.payload;
        return React::Ok;
    }
    default: return React::Ok;
    }
}

}  // namespace

static void workerMain(std::shared_ptr<Client::Shared> s) {
    Client::Options opt;
    {
        std::lock_guard<std::mutex> lk(s->m);
        opt = s->opt;
    }
    RateLimiter lim(opt.maxUpdates, opt.windowSeconds);
    const std::vector<std::string> paths =
        !opt.paths.empty() ? opt.paths
                           : socketCandidates(hostPlatform(), [](const char* n) {
                                 const char* v = std::getenv(n);
                                 return std::string(v ? v : "");
                             });
    Conn c;
    double nextAttempt = 0;
    unsigned long nonceN = 0;
    bool fatal = false;

    auto quitting = [&]() {
        std::lock_guard<std::mutex> lk(s->m);
        return s->quit;
    };
    auto drop = [&]() {
        c.t.reset();
        c.dec = FrameDecoder();
        c.sentOnThis = false;
        c.pendingFallback.clear();
        s->status = (int)Status::Searching;
        nextAttempt = nowSeconds() + opt.retrySeconds;
    };

    while (!quitting() && !fatal) {
        if (!c.t) {
            const double now = nowSeconds();
            if (now < nextAttempt) {
                std::unique_lock<std::mutex> lk(s->m);
                s->cv.wait_for(lk, std::chrono::milliseconds(100), [&] { return s->quit; });
                continue;
            }
            std::unique_ptr<Transport> t;
            for (const std::string& p : paths) {
                t = connectTransport(p);
                if (t) break;
            }
            if (!t) {
                s->status = (int)Status::Searching;
                nextAttempt = nowSeconds() + opt.retrySeconds;
                continue;
            }
            c.t = std::move(t);
            c.dec = FrameDecoder();
            if (!writeFrame(c, OpHandshake, handshakePayload(opt.appId), opt.ioTimeoutMs)) {
                drop();
                continue;
            }
            // Wait for READY (or a close).
            bool ready = false;
            const double deadline = nowSeconds() + opt.ioTimeoutMs * 2 / 1000.0;
            char buf[4096];
            while (!ready && !fatal && c.t && nowSeconds() < deadline && !quitting()) {
                int r = c.t->read(buf, sizeof buf, 100);
                if (r < 0) { drop(); break; }
                if (r > 0) c.dec.feed(buf, (size_t)r);
                if (c.dec.bad()) { drop(); break; }
                Frame f;
                while (c.t && c.dec.next(f)) {
                    if (f.op == OpClose) {
                        long long code = 0;
                        jsonFindInt(f.payload, "code", code);
                        if (code == 4000) { fatal = true; s->status = (int)Status::InvalidAppId; }
                        else drop();
                        break;
                    }
                    if (f.op == OpPing) { writeFrame(c, OpPong, f.payload, opt.ioTimeoutMs); continue; }
                    std::string evt;
                    if (f.op == OpFrame && jsonFindString(f.payload, "evt", evt) && evt == "READY") { ready = true; break; }
                }
            }
            if (fatal) break;
            if (c.t && !ready) drop();
            if (!c.t) continue;
            s->status = (int)Status::Connected;
            std::lock_guard<std::mutex> lk(s->m);
            if (s->wantedSet) s->dirty = true;   // a fresh connection has no presence: send the current one
            continue;
        }

        // ---- connected: read, then send ----
        char buf[4096];
        int r = c.t->read(buf, sizeof buf, 100);
        if (r < 0) { drop(); continue; }
        if (r > 0) c.dec.feed(buf, (size_t)r);
        if (c.dec.bad()) { drop(); continue; }
        Frame f;
        while (c.t && c.dec.next(f)) {
            std::string errPayload;
            long long code = 0;
            React rc = handleFrame(c, f, opt.ioTimeoutMs, errPayload, code);
            if (rc == React::Fatal) { fatal = true; s->status = (int)Status::InvalidAppId; break; }
            if (rc == React::Dropped) { drop(); break; }
            if (!errPayload.empty()) {
                s->errors++;
                std::lock_guard<std::mutex> lk(s->m);
                // The first form was refused (a bad image URL, an unknown field): once, try the plain one.
                if (!c.pendingFallback.empty() && !s->dirty) {
                    s->wanted = c.pendingFallback;
                    s->fallback.clear();
                    s->dirty = true;
                }
                c.pendingFallback.clear();
            }
        }
        if (!c.t || fatal) continue;

        std::string toSend, fb;
        bool have = false;
        {
            std::lock_guard<std::mutex> lk(s->m);
            if (s->dirty) {
                if (s->wanted.empty() && !c.sentOnThis) {
                    s->dirty = false;   // nothing shown, nothing to clear
                } else if (lim.tryAcquire(nowSeconds())) {
                    toSend = s->wanted;
                    fb = s->fallback;
                    s->dirty = false;
                    have = true;
                }
            }
        }
        if (have) {
            c.lastNonce = "vj" + std::to_string(++nonceN);
            if (!writeFrame(c, OpFrame, setActivityPayload(opt.pid, c.lastNonce, toSend), opt.ioTimeoutMs)) {
                std::lock_guard<std::mutex> lk(s->m);
                s->dirty = true;   // resend on the next connection
                drop();
                continue;
            }
            c.sentOnThis = true;
            c.pendingFallback = fb;
            s->sent++;
            std::lock_guard<std::mutex> lk(s->m);
            s->lastSent = toSend;
        }
    }

    // ---- shutting down: take the presence down with us ----
    if (c.t && !fatal && c.sentOnThis) {
        const std::string payload = setActivityPayload(opt.pid, "vjclear", std::string());
        if (writeFrame(c, OpFrame, payload, 150)) {
            s->sent++;
            std::lock_guard<std::mutex> lk(s->m);
            s->lastSent.clear();
        }
        // Give Discord a moment to take it before the socket closes.
        char buf[1024];
        c.t->read(buf, sizeof buf, 20);
    }
    c.t.reset();
    if (!fatal) s->status = (int)Status::Stopped;
    {
        std::lock_guard<std::mutex> lk(s->m);
        s->done = true;
    }
    s->doneCv.notify_all();
}

Client::Client() : s_(std::make_shared<Shared>()) {}
Client::~Client() { stop(); }

void Client::start(const Options& o) {
    std::lock_guard<std::mutex> lk(s_->m);
    if (s_->started) return;
    s_->opt = o;
    s_->started = true;
    s_->quit = false;
    s_->done = false;
    s_->status = (int)Status::Searching;
    std::shared_ptr<Shared> sh = s_;
    s_->th = std::thread([sh]() { workerMain(sh); });
}

void Client::setActivity(const std::string& activityJson, const std::string& fallbackJson) {
    {
        std::lock_guard<std::mutex> lk(s_->m);
        s_->wanted = activityJson;
        s_->fallback = fallbackJson;
        s_->wantedSet = true;
        s_->dirty = true;
    }
    s_->cv.notify_all();
}

void Client::stop(int timeoutMs) {
    std::thread th;
    {
        std::unique_lock<std::mutex> lk(s_->m);
        if (!s_->started) return;
        s_->quit = true;
        s_->cv.notify_all();
        s_->doneCv.wait_for(lk, std::chrono::milliseconds(timeoutMs), [&] { return s_->done; });
        th = std::move(s_->th);
        s_->started = false;
        if (!s_->done) {
            lk.unlock();
            if (th.joinable()) th.detach();   // Discord is wedged: the worker ends on its own, holding its own state
            s_ = std::make_shared<Shared>();  // a later start() must not share state with that straggler
            return;
        }
    }
    if (th.joinable()) th.join();
}

Status Client::status() const { return (Status)s_->status.load(); }
bool Client::running() const {
    std::lock_guard<std::mutex> lk(s_->m);
    return s_->started;
}
unsigned Client::sent() const { return s_->sent.load(); }
unsigned Client::errors() const { return s_->errors.load(); }
std::string Client::lastSentJson() const {
    std::lock_guard<std::mutex> lk(s_->m);
    return s_->lastSent;
}

}  // namespace discord
}  // namespace vj
