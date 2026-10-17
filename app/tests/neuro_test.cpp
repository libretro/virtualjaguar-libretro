// Neuro API client tests (docs/neuro.md).  No window, no core, no internet, no Neuro.
//
//   neuro_test unit      JSON, WebSocket framing / handshake / URLs, the input queue, the actions and their
//                        validation, the Tempest 2000 profile, profile parsing, and the protocol (Link) against
//                        a fake server.
//   neuro_test loopback  a WebSocket server on 127.0.0.1 (a free port): the Agent connects, sends startup and
//                        actions/register, answers a force with an action, plays its buttons out frame by frame,
//                        re-registers on actions/reregister_all, and reconnects after the server drops it.
//                        Skipped (77) on Windows.
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include "neuro_actions.h"
#include "neuro_agent.h"
#include "neuro_json.h"
#include "neuro_link.h"
#include "neuro_ws.h"

#ifndef _WIN32
#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

using namespace vj;
using namespace vj::neuro;

static int g_fail = 0;
#define CHECK(c)                                                         \
    do {                                                                 \
        if (!(c)) {                                                      \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);     \
            g_fail++;                                                    \
        }                                                                \
    } while (0)

static Json parsed(const std::string& s) {
    Json j;
    std::string err;
    if (!Json::parse(s, j, err)) std::printf("FAIL parse %s: %s\n", s.c_str(), err.c_str()), g_fail++;
    return j;
}

static ControlSet keys(std::initializer_list<Ctl> cs) {
    ControlSet s;
    for (Ctl c : cs) s.set(c);
    return s;
}

// ---------------------------------------------------------------------------------------------------------------
static void testJson() {
    Json j = parsed("{\"a\": [1, 2.5, -3e2, true, false, null], \"s\": \"x\\\"\\n\\u00e9\\ud83d\\ude00\"}");
    CHECK(j["a"].items().size() == 6);
    CHECK(j["a"].items()[2].asNumber() == -300);
    CHECK(j["s"].asString() == "x\"\n\xc3\xa9\xf0\x9f\x98\x80");
    CHECK(j["missing"]["deeper"].isNull());
    CHECK(parsed(j.dump()).dump() == j.dump());
    Json o = Json::object();
    o.set("b", Json::number(3)).set("a", Json::string("t\x01"));
    CHECK(o.dump() == "{\"a\":\"t\\u0001\",\"b\":3}");
    Json bad;
    std::string err;
    CHECK(!Json::parse("{\"a\":1,}", bad, err));
    CHECK(!Json::parse("[1] 2", bad, err));
    CHECK(!Json::parse(std::string(40, '[') + std::string(40, ']'), bad, err));   // depth limit
}

static void testWsUrls() {
    CHECK(wsAccept("dGhlIHNhbXBsZSBub25jZQ==") == "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=");   // RFC 6455 1.3
    CHECK(base64("foobar") == "Zm9vYmFy" && base64("fo") == "Zm8=");
    WsUrl u;
    std::string err;
    CHECK(parseWsUrl("ws://localhost:8000", u, err) && u.host == "localhost" && u.port == 8000 && u.path == "/");
    CHECK(parseWsUrl("ws://[::1]:9/x?y", u, err) && u.host == "::1" && u.port == 9 && u.path == "/x?y");
    CHECK(parseWsUrl("127.0.0.1:1234", u, err) && u.port == 1234);
    CHECK(parseWsUrl("ws://example.org", u, err) && u.port == 80);
    CHECK(!parseWsUrl("wss://example.org", u, err) && err.find("TLS") != std::string::npos);
    CHECK(!parseWsUrl("ws://host:99999", u, err));
    CHECK(!parseWsUrl("http://host", u, err));
}

static void testWsFrames() {
    std::string err;
    // A masked client frame decodes back; a long one uses the 16-bit length; fragments are joined.
    const uint8_t mask[4] = {1, 2, 3, 4};
    FrameReader r;
    std::string big(300, 'z');
    std::string wire = encodeFrame(1, "hello", mask) + encodeFrame(1, big, mask);
    r.feed(wire.data(), 3);
    FrameReader::Msg m;
    CHECK(r.next(m, err) == 0);
    r.feed(wire.data() + 3, wire.size() - 3);
    CHECK(r.next(m, err) == 1 && m.opcode == 1 && m.payload == "hello");
    CHECK(r.next(m, err) == 1 && m.payload == big);
    std::string frag = std::string("\x01\x03" "abc", 5) + std::string("\x89\x01" "p", 3) + std::string("\x80\x02" "de", 4);
    r.feed(frag.data(), frag.size());
    CHECK(r.next(m, err) == 1 && m.opcode == 9 && m.payload == "p");   // a ping between fragments
    CHECK(r.next(m, err) == 1 && m.opcode == 1 && m.payload == "abcde");
    std::string cont = std::string("\x80\x01" "x", 3);   // a continuation with nothing to continue
    r.feed(cont.data(), cont.size());
    CHECK(r.next(m, err) == -1);
}

static void testQueue() {
    InputQueue q;
    CHECK(q.enqueue({{keys({C_B}), 2}, {ControlSet(), 1}, {keys({C_LEFT}), 1}}));
    CHECK(q.busy() && q.queuedFrames() == 4);
    q.setHold(keys({C_A}));
    CHECK(q.peek() == keys({C_A, C_B}));
    CHECK(q.frame() == keys({C_A, C_B}));
    CHECK(q.frame() == keys({C_A, C_B}));
    CHECK(q.frame() == keys({C_A}));
    CHECK(q.frame() == keys({C_A, C_LEFT}));
    CHECK(!q.busy() && q.frame() == keys({C_A}));
    CHECK(!q.enqueue({{keys({C_B}), kMaxQueuedFrames + 1}}));
    CHECK(!q.busy());
    q.clear();
    CHECK(!q.frame().any());
}

static unsigned drain(InputQueue& q, Ctl c, unsigned limit = 5000) {
    unsigned n = 0;
    for (unsigned i = 0; i < limit && q.busy(); i++)
        if (q.frame().test(c)) n++;
    return n;
}

static void testButtons() {
    Ctl c;
    CHECK(controlByName("B", c) && c == C_B);
    CHECK(controlByName("#", c) && c == C_HASH && controlName(C_STAR) == "star");
    CHECK(!controlByName("x", c));
    CHECK(describe(keys({C_LEFT, C_B})) == "left+b" && describe(ControlSet()) == "nothing");
}

static void testActions() {

    InputQueue q;
    ActionSet a(q);
    CHECK(a.has("press_buttons") && a.has("release_all") && !a.has("fire"));
    CHECK(a.definitions().items().size() == a.names().size());
    const Json defs = a.definitions();
    for (const Json& d : defs.items()) CHECK(d["schema"].isObject() || d["name"].asString() == "release_all");

    CHECK(a.execute("press_buttons", "{\"buttons\":[\"b\",\"left\"],\"frames\":5}").success);
    CHECK(q.queuedFrames() >= 5 && q.peek() == keys({C_B, C_LEFT}));
    CHECK(a.execute("press_buttons", "{\"buttons\":[\"b\"],\"frames\":5}").success);   // queued after the first
    CHECK(q.queuedFrames() >= 10);
    q.clear();
    CHECK(!a.execute("press_buttons", "{\"buttons\":[\"jump\"]}").success);
    CHECK(!a.execute("press_buttons", "{\"buttons\":[]}").success);
    CHECK(!a.execute("press_buttons", "not json").success);
    CHECK(!a.execute("press_buttons", "{\"buttons\":[\"a\"],\"frames\":100000}").success);
    std::string longSeq = "{\"steps\":[";
    for (int i = 0; i < 4; i++) longSeq += std::string(i ? "," : "") + "{\"buttons\":[\"a\"],\"frames\":91}";
    CHECK(!a.execute("button_sequence", longSeq + "]}").success);   // a sequence step is at most 90 frames
    ActionResult tooLong = a.execute("fire_all_day", "");
    CHECK(!tooLong.success);
    InputQueue q2;
    ActionSet a2(q2);
    CHECK(a2.execute("press_buttons", "{\"buttons\":[\"a\"],\"frames\":600}").success);
    CHECK(a2.execute("press_buttons", "{\"buttons\":[\"a\"],\"frames\":600}").success);
    CHECK(a2.execute("press_buttons", "{\"buttons\":[\"a\"],\"frames\":600}").success);
    ActionResult full = a2.execute("press_buttons", "{\"buttons\":[\"a\"],\"frames\":1}");
    CHECK(!full.success && full.message.find("1800 frames") != std::string::npos);
    CHECK(!a.execute("no_such_action", "").success);
    CHECK(!q.busy());

    CHECK(a.execute("button_sequence", "{\"steps\":[{\"buttons\":[\"up\"],\"frames\":2},{\"buttons\":[],\"frames\":3},"
                                       "{\"buttons\":[\"a\"]}]}").success);
    CHECK(drain(q, C_UP) == 2);
    CHECK(a.execute("hold_buttons", "{\"buttons\":[\"right\"]}").success && q.hold() == keys({C_RIGHT}));
    CHECK(a.execute("release_all", "").success && !q.hold().any());

    a.setActive(false);
    ActionResult r = a.execute("press_buttons", "{\"buttons\":[\"b\"]}");
    CHECK(r.success && !q.busy());   // nothing running: a harmless success, so Neuro does not retry forever
}

static void testProfiles() {
    std::vector<neuro::Profile> all;
    for (const std::string& src : builtinProfileSources()) {
        neuro::Profile p;
        std::string err;
        CHECK(parseProfile(src, p, err));
        if (!err.empty()) std::printf("FAIL built-in profile: %s\n", err.c_str());
        all.push_back(p);
    }
    const neuro::Profile* t = findProfile(all, "tempest 2000 (World)", 0);
    CHECK(t && t->gameName == "Tempest 2000");
    CHECK(!findProfile(all, "Alien vs Predator", 0));
    if (!t) return;
    InputQueue q;
    ActionSet a(q);
    a.setProfile(t);
    CHECK(a.has("fire") && a.has("superzapper") && a.has("move_left") && a.has("press_buttons"));
    CHECK(a.execute("fire", "{\"times\":4}").success);
    CHECK(drain(q, C_B) == 4u * 3u);
    CHECK(!a.execute("fire", "{\"times\":0}").success);
    CHECK(a.execute("superzapper", "").success && drain(q, C_C) > 0);

    neuro::Profile p;
    std::string err;
    const char* doc = "{\"game_name\":\"Test\",\"titles\":[\"Test Game\"],\"crc32\":[\"0x1234ABCD\"],"
                      "\"generic_actions\":false,\"watch\":[{\"name\":\"score\",\"address\":\"0x100\",\"size\":2,"
                      "\"format\":\"bcd\"},{\"name\":\"lives\",\"address\":256,\"size\":1,\"note\":\"(of 3)\"}],"
                      "\"actions\":[{\"name\":\"shoot\",\"description\":\"Shoot.\",\"steps\":[{\"buttons\":[\"a\"],"
                      "\"frames\":2}]}]}";
    CHECK(parseProfile(doc, p, err));
    CHECK(p.crcs.size() == 1 && p.crcs[0] == 0x1234ABCDu && !p.generic && p.watches.size() == 2);
    std::vector<neuro::Profile> one{p};
    CHECK(findProfile(one, "whatever", 0x1234ABCDu) == &one[0]);
    uint8_t ram[0x200] = {};
    ram[0x100] = 0x12;
    ram[0x101] = 0x34;
    CHECK(watchValue(p.watches[0], ram, sizeof ram) == "1234");
    CHECK(watchValue(p.watches[1], ram, sizeof ram) == "18");
    Watch far = p.watches[0];
    far.address = 0x1FF;
    CHECK(watchValue(far, ram, sizeof ram) == "?");
    a.setProfile(&one[0]);
    CHECK(a.has("shoot") && !a.has("press_buttons"));
    CHECK(!parseProfile("{\"titles\":[]}", p, err));
    CHECK(!parseProfile("{\"game_name\":\"X\",\"actions\":[{\"name\":\"Bad Name\",\"steps\":[]}]}", p, err));
}

static void testLinkForces(Link& l, ActionSet& a, std::vector<Json>& sent);
static void testLinkActions(Link& l, InputQueue& q, std::vector<Json>& sent);
static void testLinkProfileSwitch(Link& l, ActionSet& a, std::vector<Json>& sent);

// The protocol against a fake server that records what the client sends.
static void testLink() {
    InputQueue q;
    ActionSet a(q);
    Link l(a);
    std::vector<Json> sent;
    l.send = [&](const std::string& m) { sent.push_back(parsed(m)); };
    l.setGameName("Tempest 2000");
    l.forceInterval = 0.5;
    l.onConnected();
    CHECK(sent.size() >= 2 && sent[0]["command"].asString() == "startup" && sent[0]["game"].asString() == "Tempest 2000");
    CHECK(sent[1]["command"].asString() == "actions/register" && sent[1]["data"]["actions"].items().size() == a.names().size());
    sent.clear();

    testLinkForces(l, a, sent);
    testLinkActions(l, q, sent);
    testLinkProfileSwitch(l, a, sent);
}

static void testLinkForces(Link& l, ActionSet& a, std::vector<Json>& sent) {
    l.tick(10.0, true, "state");
    CHECK(sent.empty());   // the pause between moves first
    l.tick(10.6, true, "# state");
    CHECK(sent.size() == 1 && sent[0]["command"].asString() == "actions/force");
    CHECK(sent[0]["data"]["state"].asString() == "# state" && sent[0]["data"]["action_names"].items().size() == a.names().size());
    CHECK(l.forcePending());
    l.tick(20.0, true, "s");
    CHECK(sent.size() == 1);   // one force at a time
}

static void testLinkActions(Link& l, InputQueue& q, std::vector<Json>& sent) {
    l.onMessage("{\"command\":\"action\",\"data\":{\"id\":\"x1\",\"name\":\"press_buttons\",\"data\":\"{\\\"buttons\\\":[\\\"bogus\\\"]}\"}}");
    CHECK(sent.size() == 2 && sent[1]["command"].asString() == "action/result" && !sent[1]["data"]["success"].asBool());
    CHECK(sent[1]["data"]["id"].asString() == "x1" && l.forcePending());   // she retries a failed force
    l.onMessage("{\"command\":\"action\",\"data\":{\"id\":\"x2\",\"name\":\"press_buttons\",\"data\":\"{\\\"buttons\\\":[\\\"b\\\"],\\\"frames\\\":2}\"}}");
    CHECK(sent.size() == 3 && sent[2]["data"]["success"].asBool() && !l.forcePending());
    CHECK(q.busy() && l.actionsReceived() == 2);

    l.onMessage("{\"command\":\"actions/reregister_all\"}");
    CHECK(sent.size() == 4 && sent[3]["command"].asString() == "actions/register");
    l.onMessage("garbage");
    l.onMessage("{\"command\":\"shutdown/graceful\",\"data\":{\"wants_shutdown\":true}}");
    CHECK(sent.size() == 4);
}

static void testLinkProfileSwitch(Link& l, ActionSet& a, std::vector<Json>& sent) {
    neuro::Profile p;
    std::string err;
    if (!parseProfile("{\"game_name\":\"T\",\"titles\":[\"T\"],\"generic_actions\":false,\"actions\":[{\"name\":\"go\",\"description\":\"Go.\",\"steps\":[{\"buttons\":[\"a\"]}]}]}", p, err)) std::printf("FAIL profile: %s\n", err.c_str());
    a.setProfile(&p);
    l.actionsChanged();
    bool unreg = false, reg = false;
    for (size_t i = 4; i < sent.size(); i++) {
        if (sent[i]["command"].asString() == "actions/unregister") unreg = sent[i]["data"]["action_names"].items().size() == 4;
        if (sent[i]["command"].asString() == "actions/register") reg = sent[i]["data"]["actions"].items().size() == 1;
    }
    CHECK(unreg && reg);
    l.onDisconnected();
    CHECK(!l.connected() && !l.forcePending());
}

// ---------------------------------------------------------------------------------------------------------------
#ifndef _WIN32
namespace {

std::string serverFrame(const std::string& text) {   // unmasked, as a server sends it
    std::string f;
    f.push_back((char)0x81);
    if (text.size() < 126) {
        f.push_back((char)text.size());
    } else {
        f.push_back((char)126);
        f.push_back((char)(text.size() >> 8));
        f.push_back((char)(text.size() & 0xff));
    }
    return f + text;
}

struct FakeServer {
    int listenFd = -1, port = 0;

    bool open() {
        listenFd = socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        a.sin_port = 0;   // a free port
        if (bind(listenFd, (sockaddr*)&a, sizeof a) || listen(listenFd, 4)) return false;
        socklen_t n = sizeof a;
        getsockname(listenFd, (sockaddr*)&a, &n);
        port = ntohs(a.sin_port);
        return true;
    }
    ~FakeServer() {
        if (listenFd >= 0) ::close(listenFd);
    }

    // One client: the handshake, then fd (or -1).  A client that gives up mid-handshake is skipped.
    int accept(int ms) {
        const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
        while (std::chrono::steady_clock::now() < end) {
            pollfd p{listenFd, POLLIN, 0};
            if (::poll(&p, 1, 50) <= 0) continue;
            const int fd = ::accept(listenFd, nullptr, nullptr);
            if (fd >= 0 && handshake(fd)) return fd;
            if (fd >= 0) ::close(fd);
        }
        return -1;
    }

    static bool handshake(int fd) {
        std::string req;
        char buf[1024];
        while (req.find("\r\n\r\n") == std::string::npos) {
            const ssize_t n = ::recv(fd, buf, sizeof buf, 0);
            if (n <= 0) return false;
            req.append(buf, (size_t)n);
        }
        const size_t k = req.find("Sec-WebSocket-Key: ");
        if (k == std::string::npos) return false;
        const std::string key = req.substr(k + 19, req.find("\r\n", k) - k - 19);
        const std::string resp = "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                                 "Sec-WebSocket-Accept: " + wsAccept(key) + "\r\n\r\n";
        return ::send(fd, resp.data(), resp.size(), MSG_NOSIGNAL) == (ssize_t)resp.size();
    }
};

// Reads client messages from `fd` until one has `command` (its JSON), or the time is up (null).
Json waitFor(int fd, FrameReader& rd, const std::string& command, Agent& agent, std::vector<std::string>* seen,
             int ms = 5000) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (std::chrono::steady_clock::now() < end) {
        FrameReader::Msg m;
        std::string err;
        while (rd.next(m, err) == 1) {
            if (m.opcode != 1) continue;
            Json j = parsed(m.payload);
            if (seen) seen->push_back(j["command"].asString());
            if (j["command"].asString() == command) return j;
        }
        pollfd p{fd, POLLIN, 0};
        if (::poll(&p, 1, 10) > 0) {
            char buf[4096];
            ssize_t n = ::recv(fd, buf, sizeof buf, 0);
            if (n <= 0) return Json();
            rd.feed(buf, (size_t)n);
        }
        (void)agent;
    }
    return Json();
}

}  // namespace

static int loopback() {
    FakeServer srv;
    if (!srv.open()) {
        std::printf("FAIL cannot listen on 127.0.0.1\n");
        return 1;
    }
    Agent agent;
    agent.log = [](const std::string& l) { std::printf("  [agent] %s\n", l.c_str()); };
    AgentSettings s;
    s.enabled = true;
    s.url = "ws://127.0.0.1:" + std::to_string(srv.port);
    s.forceInterval = 0.05;
    agent.configure(s);

    GameState g;
    g.loaded = true;
    g.title = "Tempest 2000";
    std::atomic<bool> run{true};
    std::atomic<int> pressedB{0};
    // The app's loop, on its own thread: a UI tick and an emulated frame every ~5 ms.
    std::thread app([&] {
        double t = 0;
        while (run) {
            agent.tick(t, g);
            if (agent.frame().test(C_B)) pressedB++;
            t += 0.005;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    });

    int fd = srv.accept(5000);
    CHECK(fd >= 0);
    FrameReader rd;
    std::vector<std::string> seen;
    Json st = waitFor(fd, rd, "startup", agent, &seen);
    CHECK(st["game"].asString() == "Tempest 2000");
    Json reg = waitFor(fd, rd, "actions/register", agent, &seen);
    bool hasFire = false;
    for (const Json& a : reg["data"]["actions"].items()) hasFire |= a["name"].asString() == "fire";
    CHECK(hasFire);
    Json force = waitFor(fd, rd, "actions/force", agent, &seen);
    CHECK(force["data"]["state"].asString().find("Tempest 2000") != std::string::npos);

    std::string act = serverFrame("{\"command\":\"action\",\"data\":{\"id\":\"a1\",\"name\":\"fire\",\"data\":\"{\\\"times\\\":3}\"}}");
    ::send(fd, act.data(), act.size(), 0);
    Json res = waitFor(fd, rd, "action/result", agent, &seen);
    CHECK(res["data"]["id"].asString() == "a1" && res["data"]["success"].asBool());
    Json force2 = waitFor(fd, rd, "actions/force", agent, &seen);   // the next move, once the shots played out
    CHECK(force2["command"].asString() == "actions/force");
    CHECK(pressedB.load() == 9);

    std::string rr = serverFrame("{\"command\":\"actions/reregister_all\"}");
    ::send(fd, rr.data(), rr.size(), 0);
    CHECK(waitFor(fd, rd, "actions/register", agent, &seen)["command"].asString() == "actions/register");

    ::close(fd);   // the server goes away; the client comes back by itself
    int fd2 = srv.accept(8000);
    CHECK(fd2 >= 0);
    if (fd2 >= 0) {
        FrameReader rd2;
        CHECK(waitFor(fd2, rd2, "startup", agent, nullptr)["command"].asString() == "startup");
        ::close(fd2);
    }
    run = false;
    app.join();
    std::printf("loopback: client sent");
    for (const std::string& c : seen) std::printf(" %s", c.c_str());
    std::printf("\n");
    return g_fail ? 1 : 0;
}
#endif

int main(int argc, char** argv) {
    const std::string mode = argc > 1 ? argv[1] : "unit";
    if (mode == "loopback") {
#ifdef _WIN32
        std::printf("loopback: skipped on Windows\n");
        return 77;
#else
        int rc = loopback();
        std::printf(rc ? "neuro loopback: FAILED\n" : "neuro loopback: ok\n");
        return rc;
#endif
    }
    testJson();
    testWsUrls();
    testWsFrames();
    testQueue();
    testButtons();
    testActions();
    testProfiles();
    testLink();
    std::printf(g_fail ? "neuro unit: %d FAILED\n" : "neuro unit: ok\n", g_fail);
    return g_fail ? 1 : 0;
}
