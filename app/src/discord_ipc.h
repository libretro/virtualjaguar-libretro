// Discord Rich Presence over Discord's local RPC socket (#833).  A small self-contained client: no
// discord-rpc library (deprecated), no Game SDK (a binary blob with a restrictive licence).
//
// The wire format (https://docs.discord.com/developers/topics/rpc, "IPC transport"):
//   frame     u32 opcode LE, u32 payload length LE, then that many bytes of JSON
//   opcodes   0 HANDSHAKE   1 FRAME   2 CLOSE   3 PING   4 PONG
//   transport a Unix domain socket  <$XDG_RUNTIME_DIR|$TMPDIR|$TMP|$TEMP|/tmp>/discord-ipc-0..9  (macOS, Linux;
//             the snap and flatpak Discord keep theirs one folder down), or the named pipe
//             \\.\pipe\discord-ipc-0..9  (Windows)
//   start     op 0 {"v":1,"client_id":"<application id>"}  ->  op 1 {"evt":"READY",...}
//   presence  op 1 {"cmd":"SET_ACTIVITY","args":{"pid":N,"activity":{...}},"nonce":"..."}
// No OAuth is needed for SET_ACTIVITY on the local socket (the docs' AUTHORIZE note is about the other commands).
//
// Everything here is independent of the App, SDL and ImGui, so the unit test links this file alone.
// Threading: Client runs one worker thread; the UI thread only calls setActivity() / status(), which take a
// mutex for a few instructions.  Nothing the UI or the emulation loop does can block on Discord.
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace vj {
namespace discord {

enum Op : uint32_t { OpHandshake = 0, OpFrame = 1, OpClose = 2, OpPing = 3, OpPong = 4 };

// ---- frames -----------------------------------------------------------------------------------
std::string encodeFrame(uint32_t op, const std::string& payload);

struct Frame {
    uint32_t op = 0;
    std::string payload;
};

// Incremental decoder: feed() takes whatever read() returned (any split, any number of frames), next() hands
// out whole frames.  A length above kMaxPayload poisons the stream (bad() = true): Discord never sends that,
// so the connection is dropped instead of buffering without bound.
class FrameDecoder {
public:
    static constexpr uint32_t kMaxPayload = 64 * 1024;
    void feed(const void* data, size_t n);
    bool next(Frame& out);
    bool bad() const { return bad_; }
    size_t buffered() const { return buf_.size(); }

private:
    std::string buf_;
    bool bad_ = false;
};

// ---- JSON helpers -------------------------------------------------------------------------------
// Escapes `"` `\` and control characters (as \u00XX); UTF-8 passes through untouched.
std::string jsonEscape(const std::string& s);
// "\"" + jsonEscape(s) + "\""
std::string jsonString(const std::string& s);
// Cut to at most maxBytes without splitting a UTF-8 sequence.
std::string truncateUtf8(const std::string& s, size_t maxBytes);
// Minimal readers for the few flat fields we look at in Discord's replies ("evt", "nonce", "code", ...).  They
// find the first `"key":` and read a string / integer after it; not a general parser, not used on anything
// that matters beyond a log line and the READY / ERROR / close-code checks.
bool jsonFindString(const std::string& json, const char* key, std::string& out);
bool jsonFindInt(const std::string& json, const char* key, long long& out);

// ---- payloads ---------------------------------------------------------------------------------------
std::string handshakePayload(const std::string& appId);
// activityJson empty = clear the presence ("activity":null).
std::string setActivityPayload(unsigned long pid, const std::string& nonce, const std::string& activityJson);

// ---- where Discord's socket may be ---------------------------------------------------------------
enum class Platform { Unix, Windows };
Platform hostPlatform();
// `getenv` returns "" for an unset variable.  Order: socket number first (0..9), then folder, so a Discord on
// ipc-0 in a snap folder beats a stale ipc-1 in /tmp.
std::vector<std::string> socketCandidates(Platform p, const std::function<std::string(const char*)>& getenv);

// ---- rate limit --------------------------------------------------------------------------------------
// Discord lets a client update its presence about 5 times per 20 s.  tryAcquire() records a send when one is
// allowed; waitSeconds() says how long until the next would be.  The clock is injected (seconds, monotonic).
class RateLimiter {
public:
    RateLimiter(unsigned maxEvents = 4, double windowSeconds = 20.0) : max_(maxEvents), window_(windowSeconds) {}
    bool tryAcquire(double now);
    double waitSeconds(double now);
    void reset() { times_.clear(); }

private:
    void expire(double now);
    unsigned max_;
    double window_;
    std::deque<double> times_;
};

// ---- the transport ----------------------------------------------------------------------------------
class Transport {
public:
    virtual ~Transport() {}
    // true = every byte written.  false = closed, or not accepted within timeoutMs.
    virtual bool write(const void* data, size_t n, int timeoutMs) = 0;
    // > 0 bytes read, 0 nothing within timeoutMs, < 0 closed / failed.
    virtual int read(void* buf, size_t cap, int timeoutMs) = 0;
};
std::unique_ptr<Transport> connectTransport(const std::string& path);

// ---- the client ----------------------------------------------------------------------------------------
enum class Status {
    Stopped,      // not started
    Searching,    // Discord is not running (or not reachable): retrying
    Connected,    // handshake answered with READY
    InvalidAppId, // Discord closed the connection with 4000 "Invalid Client ID": not retried
};

class Client {
public:
    struct Options {
        std::string appId;
        unsigned long pid = 0;
        std::vector<std::string> paths;     // empty = socketCandidates() for this OS
        double retrySeconds = 15.0;         // between connection attempts while Discord is not running
        unsigned maxUpdates = 4;            // rate limit: updates per window
        double windowSeconds = 20.0;
        int ioTimeoutMs = 1500;             // one write / the READY wait
    };
    Client();
    ~Client();
    Client(const Client&) = delete;
    Client& operator=(const Client&) = delete;

    void start(const Options& o);           // spawns the worker; no-op when running
    // The newest wanted presence.  `fallbackJson` (optional) is sent instead if Discord answers the first with
    // an ERROR (a rejected image URL, say).  Empty `activityJson` = clear.  Replaces anything not yet sent.
    void setActivity(const std::string& activityJson, const std::string& fallbackJson = std::string());
    // Clears the presence, closes, joins -- all within about `timeoutMs`; a wedged Discord can delay the quit by
    // that much and no more (the worker is detached if it will not finish).
    void stop(int timeoutMs = 250);
    Status status() const;
    bool running() const;
    unsigned sent() const;                  // SET_ACTIVITY frames written (tests, status line)
    unsigned errors() const;                // ERROR replies seen
    std::string lastSentJson() const;       // the last activity written ("" = a clear)

    struct Shared;   // the worker's state (public only so the worker function can name it)

private:
    std::shared_ptr<Shared> s_;
};

}  // namespace discord
}  // namespace vj
