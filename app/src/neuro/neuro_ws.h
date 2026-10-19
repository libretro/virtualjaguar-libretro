// A plain-text WebSocket client (RFC 6455) for the Neuro API (neuro_link.h).
//
// The Neuro API is "connect to a websocket URL and exchange JSON text frames"; the SDK's own test servers
// (Randy, Tony, Gary) and Neuro's backend all listen on ws:// (the SDK's convention is the environment variable
// NEURO_SDK_WS_URL, e.g. ws://localhost:8000).  wss:// is refused with a clear message: there is no TLS stack in
// the app.
//
// All socket work happens on one worker thread, so a slow or absent server can never stall a frame.  The owner
// (UI thread) calls send() and takes events with poll(); the worker connects, retries with a jittered back-off
// while the server is down, answers pings, and reports Connected / Message / Disconnected.  Nothing is buffered
// across a disconnect: what the owner sends while the link is down is dropped, because the owner re-announces
// everything (startup, actions) on every Connected event anyway.
//
// The link is plain text and unauthenticated; isLoopbackHost() is what the UI uses to warn about a server that is
// not on this machine.
#pragma once

#include <atomic>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace vj {
namespace neuro {

struct WsUrl {
    std::string host;
    int port = 80;
    std::string path = "/";
};
// "ws://host[:port][/path]"; a bare "host:port" is taken as ws://.  false + err for anything else.
bool parseWsUrl(const std::string& url, WsUrl& out, std::string& err);
// "localhost", 127.x.x.x, ::1 (and "*.localhost"): traffic that never leaves this machine.
bool isLoopbackHost(const std::string& host);
// A URL that parses and names a host outside this machine: ws:// is plaintext, so everything sent to it can be read
// and changed on the way.
bool wsUrlIsRemote(const std::string& url);

// ---- the pure framing layer (unit-tested in tests/neuro_test.cpp) ----
enum : uint8_t { kOpCont = 0, kOpText = 1, kOpBinary = 2, kOpClose = 8, kOpPing = 9, kOpPong = 10 };
// One client frame: FIN set, masked with `mask` (a client must mask).
std::string encodeFrame(uint8_t opcode, const std::string& payload, const uint8_t mask[4]);
std::string base64(const std::string& bytes);
std::string sha1(const std::string& bytes);   // 20 raw bytes
std::string wsAccept(const std::string& key);   // base64(sha1(key + the RFC 6455 GUID))

// Splits a peer's byte stream into messages; continuation frames are reassembled.  The client reads SERVER frames,
// which RFC 6455 5.1 says are never masked; `fromClient` is the other end (the tests' fake server) and requires the
// mask.  Reserved bits are errors either way: no extension is negotiated.
class FrameReader {
public:
    struct Msg {
        uint8_t opcode = 0;
        std::string payload;
    };
    // Neuro's messages are a few hundred bytes (an action's JSON); a megabyte is generous and bounds what a
    // misbehaving server can make us buffer.
    static constexpr size_t kMaxMessage = 1u << 20;
    explicit FrameReader(bool fromClient = false) : fromClient_(fromClient) {}
    void feed(const char* data, size_t n) { buf_.append(data, n); }
    // 1 = `out` holds the next complete message, 0 = need more bytes, -1 = protocol error (err says why).
    int next(Msg& out, std::string& err);

private:
    bool fromClient_;
    std::string buf_;
    std::string partial_;
    uint8_t partialOp_ = 0;
    bool inFragment_ = false;
    int controlOrData(uint8_t op, bool fin, std::string& payload, Msg& out, std::string& err);
};

// Test seam: the name-lookup helper thread sleeps this long before it looks up, standing in for a resolver that hangs
// (tests/neuro_test.cpp checks that stop() does not wait for it).  0 in the app.
extern std::atomic<int> g_testLookupDelayMs;

// ---- the client ----
class WsClient {
public:
    enum class State { Stopped, Connecting, Open, Waiting };   // Waiting = down, retrying later
    struct Event {
        enum Kind { Connected, Message, Disconnected } kind = Message;
        std::string text;   // the message, or why the link went down
    };

    ~WsClient() { stop(); }
    void start(const std::string& url);   // (re)starts the worker for this URL
    // Closes the link and joins the worker.  Every wait the worker makes is cut into 50 ms slices that look at the
    // stop flag, and the name lookup (getaddrinfo cannot be interrupted) runs on a helper thread the worker simply
    // abandons, so this returns in about 50 ms even for a host that does not resolve.  Queued events are dropped.
    void stop();
    bool running() const { return worker_.joinable(); }
    const std::string& url() const { return url_; }

    void send(const std::string& text);   // one text frame; dropped while the link is not Open
    bool poll(Event& ev);                 // the next event, if any

    State state() const { return state_.load(); }
    std::string lastError() const;
    uint64_t sentCount() const { return sent_.load(); }
    uint64_t receivedCount() const { return received_.load(); }

    // Each queue holds at most this many bytes (and events at most 4096 entries): an owner that stops polling, or a
    // server that floods, costs a bounded amount of memory.  Over the cap the oldest Message events go (never the
    // Connected / Disconnected ones) and send() drops the new text.
    static constexpr size_t kMaxQueuedBytes = 4u << 20;

private:
    void run(WsUrl u);
    bool session(const WsUrl& u, std::string& why);   // one connection, until it drops; false = never opened
    bool pump(intptr_t fd, FrameReader& rd, std::string& why);
    bool flushOutbox(intptr_t fd, std::string& why);
    void push(Event::Kind k, const std::string& text);
    void setError(const std::string& e);
    void waitRetry(int ms);

    std::string url_;
    std::thread worker_;
    std::atomic<bool> stop_{false};
    std::atomic<State> state_{State::Stopped};
    std::atomic<uint64_t> sent_{0}, received_{0};
    mutable std::mutex mu_;
    std::deque<std::string> outbox_;
    std::deque<Event> events_;
    std::string lastError_;
    size_t eventBytes_ = 0, outboxBytes_ = 0;   // what the two queues hold, against kMaxQueuedBytes
};

}  // namespace neuro
}  // namespace vj
