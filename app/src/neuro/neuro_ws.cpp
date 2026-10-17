#include "neuro_ws.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <random>

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <winsock2.h>
#  include <ws2tcpip.h>
typedef SOCKET sock_t;
#  define VJ_BAD_SOCK INVALID_SOCKET
#  define vj_poll WSAPoll
#  define vj_close closesocket
static bool wouldBlock() {
    const int e = WSAGetLastError();
    return e == WSAEWOULDBLOCK || e == WSAEINPROGRESS;
}
#else
#  include <errno.h>
#  include <fcntl.h>
#  include <netdb.h>
#  include <poll.h>
#  include <sys/socket.h>
#  include <sys/types.h>
#  include <unistd.h>
typedef int sock_t;
#  define VJ_BAD_SOCK (-1)
#  define vj_poll poll
#  define vj_close close
static bool wouldBlock() { return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINPROGRESS; }
#endif

namespace vj {
namespace neuro {

// ---------------------------------------------------------------------------------------------------------------
// URL
// ---------------------------------------------------------------------------------------------------------------
namespace {

// "  WS://host/x " -> "host/x"; false for wss:// and other schemes.
bool stripScheme(const std::string& url, std::string& rest, std::string& err) {
    rest = url;
    while (!rest.empty() && std::isspace((unsigned char)rest.back())) rest.pop_back();
    while (!rest.empty() && std::isspace((unsigned char)rest.front())) rest.erase(0, 1);
    std::string lower = rest;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    if (lower.compare(0, 6, "wss://") == 0) return (err = "wss:// (TLS) is not supported; use a ws:// URL", false);
    if (lower.compare(0, 5, "ws://") == 0) rest = rest.substr(5);
    else if (lower.find("://") != std::string::npos) return (err = "not a ws:// URL", false);
    return true;
}

// "host:port", "[v6]:port", "host" -> host and the port text ("" = none).  False when malformed.
bool splitHostPort(const std::string& hostport, std::string& host, std::string& portText) {
    if (!hostport.empty() && hostport[0] == '[') {
        const size_t rb = hostport.find(']');
        if (rb == std::string::npos) return false;
        host = hostport.substr(1, rb - 1);
        const std::string after = hostport.substr(rb + 1);
        if (after.empty()) return true;
        if (after[0] != ':' || after.size() == 1) return false;
        portText = after.substr(1);
        return true;
    }
    const size_t colon = hostport.rfind(':');
    host = hostport.substr(0, colon);
    if (colon == std::string::npos) return true;
    portText = hostport.substr(colon + 1);
    return !portText.empty();
}

}  // namespace

bool parseWsUrl(const std::string& url, WsUrl& out, std::string& err) {
    std::string rest, portText;
    if (!stripScheme(url, rest, err)) return false;
    WsUrl u;
    const size_t slash = rest.find('/');
    if (slash != std::string::npos) u.path = rest.substr(slash);
    if (!splitHostPort(rest.substr(0, slash), u.host, portText)) return (err = "bad host or port in " + url, false);
    if (!portText.empty()) {
        char* end = nullptr;
        const long port = std::strtol(portText.c_str(), &end, 10);
        if (*end || port < 1 || port > 65535) return (err = "bad port in " + url, false);
        u.port = (int)port;
    }
    if (u.host.empty()) return (err = "no host in " + url, false);
    out = u;
    return true;
}

// ---------------------------------------------------------------------------------------------------------------
// framing
// ---------------------------------------------------------------------------------------------------------------
std::string encodeFrame(uint8_t opcode, const std::string& payload, const uint8_t mask[4]) {
    std::string f;
    f.push_back((char)(0x80 | (opcode & 0x0F)));
    const uint64_t n = payload.size();
    if (n < 126) {
        f.push_back((char)(0x80 | n));
    } else if (n <= 0xFFFF) {
        f.push_back((char)(0x80 | 126));
        f.push_back((char)(n >> 8));
        f.push_back((char)(n & 0xFF));
    } else {
        f.push_back((char)(0x80 | 127));
        for (int i = 7; i >= 0; i--) f.push_back((char)((n >> (8 * i)) & 0xFF));
    }
    f.append((const char*)mask, 4);
    for (size_t i = 0; i < payload.size(); i++) f.push_back((char)(payload[i] ^ mask[i & 3]));
    return f;
}

int FrameReader::next(Msg& out, std::string& err) {
    for (;;) {
        if (buf_.size() < 2) return 0;
        const uint8_t b0 = (uint8_t)buf_[0], b1 = (uint8_t)buf_[1];
        const bool fin = (b0 & 0x80) != 0, masked = (b1 & 0x80) != 0;
        const uint8_t op = b0 & 0x0F;
        uint64_t len = b1 & 0x7F;
        size_t hdr = 2;
        if (len == 126) {
            if (buf_.size() < 4) return 0;
            len = ((uint64_t)(uint8_t)buf_[2] << 8) | (uint8_t)buf_[3];
            hdr = 4;
        } else if (len == 127) {
            if (buf_.size() < 10) return 0;
            len = 0;
            for (int i = 0; i < 8; i++) len = (len << 8) | (uint8_t)buf_[2 + i];
            hdr = 10;
        }
        if (len > kMaxMessage) return (err = "frame too large", -1);
        const size_t maskLen = masked ? 4 : 0;
        if (buf_.size() < hdr + maskLen + len) return 0;
        std::string payload = buf_.substr(hdr + maskLen, (size_t)len);
        if (masked)
            for (size_t i = 0; i < payload.size(); i++) payload[i] ^= buf_[hdr + (i & 3)];
        buf_.erase(0, hdr + maskLen + (size_t)len);
        const int r = controlOrData(op, fin, payload, out, err);
        if (r != 0) return r;
    }
}

int FrameReader::controlOrData(uint8_t op, bool fin, std::string& payload, Msg& out, std::string& err) {
    if (op >= 8) {   // control frames may arrive between fragments
        if (!fin || payload.size() > 125) return (err = "bad control frame", -1);
        out.opcode = op;
        out.payload = std::move(payload);
        return 1;
    }
    if (op == kOpCont) {
        if (!inFragment_) return (err = "continuation without a start", -1);
    } else {
        if (inFragment_) return (err = "new message inside a fragmented one", -1);
        if (op != kOpText && op != kOpBinary) return (err = "unknown opcode", -1);
        partialOp_ = op;
        partial_.clear();
        inFragment_ = true;
    }
    if (partial_.size() + payload.size() > kMaxMessage) return (err = "message too large", -1);
    partial_ += payload;
    if (!fin) return 0;
    inFragment_ = false;
    out.opcode = partialOp_;
    out.payload = std::move(partial_);
    partial_.clear();
    return 1;
}

std::string base64(const std::string& in) {
    static const char* k = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string o;
    size_t i = 0;
    for (; i + 2 < in.size(); i += 3) {
        const uint32_t v = ((uint8_t)in[i] << 16) | ((uint8_t)in[i + 1] << 8) | (uint8_t)in[i + 2];
        o += k[v >> 18];
        o += k[(v >> 12) & 63];
        o += k[(v >> 6) & 63];
        o += k[v & 63];
    }
    if (i + 1 == in.size()) {
        const uint32_t v = (uint8_t)in[i] << 16;
        o += k[v >> 18];
        o += k[(v >> 12) & 63];
        o += "==";
    } else if (i + 2 == in.size()) {
        const uint32_t v = ((uint8_t)in[i] << 16) | ((uint8_t)in[i + 1] << 8);
        o += k[v >> 18];
        o += k[(v >> 12) & 63];
        o += k[(v >> 6) & 63];
        o += '=';
    }
    return o;
}

static uint32_t rol(uint32_t v, int n) { return (v << n) | (v >> (32 - n)); }

static void sha1Block(uint32_t h[5], const uint8_t* p) {
    uint32_t w[80];
    for (int i = 0; i < 16; i++) w[i] = ((uint32_t)p[4 * i] << 24) | (p[4 * i + 1] << 16) | (p[4 * i + 2] << 8) | p[4 * i + 3];
    for (int i = 16; i < 80; i++) w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
    for (int i = 0; i < 80; i++) {
        uint32_t f, k;
        if (i < 20) f = (b & c) | (~b & d), k = 0x5A827999;
        else if (i < 40) f = b ^ c ^ d, k = 0x6ED9EBA1;
        else if (i < 60) f = (b & c) | (b & d) | (c & d), k = 0x8F1BBCDC;
        else f = b ^ c ^ d, k = 0xCA62C1D6;
        const uint32_t t = rol(a, 5) + f + e + k + w[i];
        e = d, d = c, c = rol(b, 30), b = a, a = t;
    }
    h[0] += a, h[1] += b, h[2] += c, h[3] += d, h[4] += e;
}

std::string sha1(const std::string& in) {
    uint32_t h[5] = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0};
    std::string m = in;
    const uint64_t bits = (uint64_t)in.size() * 8;
    m.push_back((char)0x80);
    while (m.size() % 64 != 56) m.push_back('\0');
    for (int i = 7; i >= 0; i--) m.push_back((char)((bits >> (8 * i)) & 0xFF));
    for (size_t i = 0; i < m.size(); i += 64) sha1Block(h, (const uint8_t*)m.data() + i);
    std::string o;
    for (uint32_t v : h)
        for (int i = 3; i >= 0; i--) o.push_back((char)((v >> (8 * i)) & 0xFF));
    return o;
}

std::string wsAccept(const std::string& key) { return base64(sha1(key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11")); }

// ---------------------------------------------------------------------------------------------------------------
// sockets
// ---------------------------------------------------------------------------------------------------------------
namespace {

std::mt19937& rng() {
    static thread_local std::mt19937 g{std::random_device{}()};
    return g;
}

void netInit() {
#ifdef _WIN32
    static bool done = false;
    if (!done) {
        WSADATA d;
        WSAStartup(MAKEWORD(2, 2), &d);
        done = true;
    }
#endif
}

void setNonBlocking(sock_t s) {
#ifdef _WIN32
    u_long on = 1;
    ioctlsocket(s, FIONBIO, &on);
#else
    fcntl(s, F_SETFL, fcntl(s, F_GETFL, 0) | O_NONBLOCK);
#endif
}

// Waits for `events` on s, in slices so a stop request is seen; 1 = ready, 0 = timeout / stop, -1 = error.
int waitFor(sock_t s, short events, int timeoutMs, const std::atomic<bool>& stop) {
    for (int waited = 0; waited < timeoutMs && !stop.load(); waited += 50) {
        pollfd p;
        p.fd = s;
        p.events = events;
        p.revents = 0;
        const int r = vj_poll(&p, 1, 50);
        if (r < 0) return -1;
        if (r > 0) return (p.revents & (POLLERR | POLLNVAL)) && !(p.revents & events) ? -1 : 1;
    }
    return 0;
}

bool sendAll(sock_t s, const std::string& data, const std::atomic<bool>& stop) {
    size_t off = 0;
    while (off < data.size()) {
#ifdef MSG_NOSIGNAL
        const int flags = MSG_NOSIGNAL;   // Linux: a dead peer is an error, not SIGPIPE
#else
        const int flags = 0;              // macOS: SO_NOSIGPIPE is set on the socket; Windows has no SIGPIPE
#endif
        const int n = (int)::send(s, data.data() + off, (int)(data.size() - off), flags);
        if (n > 0) {
            off += (size_t)n;
            continue;
        }
        if (n < 0 && wouldBlock() && waitFor(s, POLLOUT, 5000, stop) == 1) continue;
        return false;
    }
    return true;
}

sock_t connectTo(const WsUrl& u, const std::atomic<bool>& stop, std::string& why) {
    addrinfo hints;
    std::memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* res = nullptr;
    if (getaddrinfo(u.host.c_str(), std::to_string(u.port).c_str(), &hints, &res) != 0 || !res) {
        why = "cannot resolve " + u.host;
        return VJ_BAD_SOCK;
    }
    sock_t s = VJ_BAD_SOCK;
    why = "connection refused";
    for (addrinfo* a = res; a && !stop.load(); a = a->ai_next) {
        s = ::socket(a->ai_family, a->ai_socktype, a->ai_protocol);
        if (s == VJ_BAD_SOCK) continue;
#ifdef SO_NOSIGPIPE
        int one = 1;
        ::setsockopt(s, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
#endif
        setNonBlocking(s);
        const int r = ::connect(s, a->ai_addr, (int)a->ai_addrlen);
        if (r == 0 || (wouldBlock() && waitFor(s, POLLOUT, 3000, stop) == 1)) {
            int soErr = 0;
            socklen_t len = sizeof soErr;
            getsockopt(s, SOL_SOCKET, SO_ERROR, (char*)&soErr, &len);
            if (soErr == 0) break;
        }
        vj_close(s);
        s = VJ_BAD_SOCK;
    }
    freeaddrinfo(res);
    return s;
}

std::string randomKey() {
    std::string raw;
    for (int i = 0; i < 16; i++) raw.push_back((char)(rng()() & 0xFF));
    return base64(raw);
}

// Reads the HTTP response head; whatever follows it (early frames) is left in `rest`.
bool readHandshake(sock_t s, const std::string& key, std::string& rest, const std::atomic<bool>& stop, std::string& why) {
    std::string head;
    char buf[2048];
    while (head.find("\r\n\r\n") == std::string::npos) {
        if (head.size() > 16384 || waitFor(s, POLLIN, 5000, stop) != 1) return (why = "no handshake reply", false);
        const int n = (int)::recv(s, buf, sizeof buf, 0);
        if (n <= 0) return (why = "closed during the handshake", false);
        head.append(buf, (size_t)n);
    }
    const size_t end = head.find("\r\n\r\n") + 4;
    rest = head.substr(end);
    head.resize(end);
    if (head.compare(0, 12, "HTTP/1.1 101") != 0) return (why = "server refused the upgrade: " + head.substr(0, head.find('\r')), false);
    std::string lower = head;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    const size_t k = lower.find("sec-websocket-accept:");
    if (k == std::string::npos) return (why = "no Sec-WebSocket-Accept", false);
    size_t v = k + 21;
    while (v < head.size() && head[v] == ' ') v++;
    const std::string accept = head.substr(v, head.find('\r', v) - v);
    if (accept != wsAccept(key)) return (why = "bad Sec-WebSocket-Accept", false);
    return true;
}

std::string clientFrame(uint8_t op, const std::string& payload) {
    uint8_t mask[4];
    const uint32_t r = rng()();
    std::memcpy(mask, &r, 4);
    return encodeFrame(op, payload, mask);
}

}  // namespace

// ---------------------------------------------------------------------------------------------------------------
// the client
// ---------------------------------------------------------------------------------------------------------------
void WsClient::start(const std::string& url) {
    stop();
    url_ = url;
    WsUrl u;
    std::string err;
    if (!parseWsUrl(url, u, err)) {
        setError(err);
        return;
    }
    netInit();
    stop_ = false;
    state_ = State::Connecting;
    worker_ = std::thread([this, u] { run(u); });
}

void WsClient::stop() {
    stop_ = true;
    if (worker_.joinable()) worker_.join();
    state_ = State::Stopped;
    std::lock_guard<std::mutex> l(mu_);
    outbox_.clear();
}

void WsClient::send(const std::string& text) {
    if (state_.load() != State::Open) return;
    std::lock_guard<std::mutex> l(mu_);
    outbox_.push_back(text);
}

bool WsClient::poll(Event& ev) {
    std::lock_guard<std::mutex> l(mu_);
    if (events_.empty()) return false;
    ev = std::move(events_.front());
    events_.pop_front();
    return true;
}

std::string WsClient::lastError() const {
    std::lock_guard<std::mutex> l(mu_);
    return lastError_;
}

void WsClient::push(Event::Kind k, const std::string& text) {
    std::lock_guard<std::mutex> l(mu_);
    Event e;
    e.kind = k;
    e.text = text;
    events_.push_back(std::move(e));
    if (events_.size() > 4096) events_.pop_front();   // an owner that stopped polling must not grow us forever
}

void WsClient::setError(const std::string& e) {
    std::lock_guard<std::mutex> l(mu_);
    lastError_ = e;
}

void WsClient::waitRetry(int ms) {
    for (int t = 0; t < ms && !stop_.load(); t += 50) std::this_thread::sleep_for(std::chrono::milliseconds(50));
}

void WsClient::run(WsUrl u) {
    int backoff = 1000;
    while (!stop_.load()) {
        state_ = State::Connecting;
        std::string why;
        const bool opened = session(u, why);
        if (stop_.load()) break;
        setError(why);
        if (opened) {
            push(Event::Disconnected, why);
            backoff = 1000;
        }
        state_ = State::Waiting;
        waitRetry(backoff);
        backoff = std::min(backoff * 2, 10000);
    }
    state_ = State::Stopped;
}

bool WsClient::session(const WsUrl& u, std::string& why) {
    sock_t s = connectTo(u, stop_, why);
    if (s == VJ_BAD_SOCK) return false;
    const std::string key = randomKey();
    const std::string hostHdr = u.host.find(':') != std::string::npos ? "[" + u.host + "]" : u.host;
    const std::string req = "GET " + u.path + " HTTP/1.1\r\nHost: " + hostHdr + ":" + std::to_string(u.port) +
                            "\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: " + key +
                            "\r\nSec-WebSocket-Version: 13\r\nUser-Agent: VirtualJaguar-Neuro\r\n\r\n";
    std::string rest;
    if (!sendAll(s, req, stop_) || !readHandshake(s, key, rest, stop_, why)) {
        if (why.empty()) why = "handshake failed";
        vj_close(s);
        return false;
    }
    {
        std::lock_guard<std::mutex> l(mu_);
        outbox_.clear();
        lastError_.clear();
    }
    state_ = State::Open;
    push(Event::Connected, "");
    FrameReader rd;
    rd.feed(rest.data(), rest.size());
    pump((intptr_t)s, rd, why);
    state_ = State::Waiting;
    {
        std::lock_guard<std::mutex> l(mu_);
        outbox_.clear();
    }
    vj_close(s);
    return true;
}

bool WsClient::flushOutbox(intptr_t fd, std::string& why) {
    std::deque<std::string> out;
    {
        std::lock_guard<std::mutex> l(mu_);
        out.swap(outbox_);
    }
    for (const std::string& m : out) {
        if (!sendAll((sock_t)fd, clientFrame(kOpText, m), stop_)) return (why = "send failed", false);
        sent_++;
    }
    return true;
}

bool WsClient::pump(intptr_t fd, FrameReader& rd, std::string& why) {
    const sock_t s = (sock_t)fd;
    char buf[8192];
    while (!stop_.load()) {
        if (!flushOutbox(fd, why)) return false;
        FrameReader::Msg m;
        std::string err;
        int r;
        while ((r = rd.next(m, err)) == 1) {
            if (m.opcode == kOpText || m.opcode == kOpBinary) {
                received_++;
                push(Event::Message, m.payload);
            } else if (m.opcode == kOpPing) {
                sendAll(s, clientFrame(kOpPong, m.payload), stop_);
            } else if (m.opcode == kOpClose) {
                sendAll(s, clientFrame(kOpClose, m.payload.substr(0, 2)), stop_);
                return (why = "the server closed the connection", false);
            }
        }
        if (r < 0) return (why = "protocol error: " + err, false);
        const int w = waitFor(s, POLLIN, 50, stop_);
        if (w < 0) return (why = "socket error", false);
        if (w == 0) continue;
        const int n = (int)::recv(s, buf, sizeof buf, 0);
        if (n == 0) return (why = "the server closed the connection", false);
        if (n < 0) {
            if (wouldBlock()) continue;
            return (why = "connection lost", false);
        }
        rd.feed(buf, (size_t)n);
    }
    sendAll(s, clientFrame(kOpClose, std::string("\x03\xe8", 2)), stop_);   // 1000 normal closure
    why = "stopped";
    return true;
}

}  // namespace neuro
}  // namespace vj
