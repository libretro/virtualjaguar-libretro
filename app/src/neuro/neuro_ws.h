// A plain-text WebSocket client (RFC 6455) for the Neuro API (neuro_link.h).
//
// The Neuro API is "connect to a websocket URL and exchange JSON text frames"; the SDK's own test servers
// (Randy, Tony, Gary) and Neuro's backend all listen on ws:// (the SDK's convention is the environment variable
// NEURO_SDK_WS_URL, e.g. ws://localhost:8000).  wss:// is refused with a clear message: there is no TLS stack in
// the app.
//
// All socket work happens on one worker thread, so a slow or absent server can never stall a frame.  The owner
// (UI thread) calls send() and takes events with poll(); the worker connects, retries with a back-off while the
// server is down, answers pings, and reports Connected / Message / Disconnected.  Nothing is buffered across a
// disconnect: what the owner sends while the link is down is dropped, because the owner re-announces everything
// (startup, actions) on every Connected event anyway.
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

// ---- the pure framing layer (unit-tested in tests/neuro_test.cpp) ----
enum : uint8_t { kOpCont = 0, kOpText = 1, kOpBinary = 2, kOpClose = 8, kOpPing = 9, kOpPong = 10 };
// One client frame: FIN set, masked with `mask` (a client must mask).
std::string encodeFrame(uint8_t opcode, const std::string& payload, const uint8_t mask[4]);
std::string base64(const std::string& bytes);
std::string sha1(const std::string& bytes);   // 20 raw bytes
std::string wsAccept(const std::string& key);   // base64(sha1(key + the RFC 6455 GUID))

// Splits a server byte stream into messages; continuation frames are reassembled.
class FrameReader {
public:
    struct Msg {
        uint8_t opcode = 0;
        std::string payload;
    };
    static constexpr size_t kMaxMessage = 16u << 20;
    void feed(const char* data, size_t n) { buf_.append(data, n); }
    // 1 = `out` holds the next complete message, 0 = need more bytes, -1 = protocol error (err says why).
    int next(Msg& out, std::string& err);

private:
    std::string buf_;
    std::string partial_;
    uint8_t partialOp_ = 0;
    bool inFragment_ = false;
    int controlOrData(uint8_t op, bool fin, std::string& payload, Msg& out, std::string& err);
};

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
    void stop();                          // closes the link and joins the worker (bounded: <= ~250 ms)
    bool running() const { return worker_.joinable(); }
    const std::string& url() const { return url_; }

    void send(const std::string& text);   // one text frame; dropped while the link is not Open
    bool poll(Event& ev);                 // the next event, if any

    State state() const { return state_.load(); }
    std::string lastError() const;
    uint64_t sentCount() const { return sent_.load(); }
    uint64_t receivedCount() const { return received_.load(); }

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
};

}  // namespace neuro
}  // namespace vj
