// Neuro API client tests (docs/neuro.md).  No window, no core, no internet, no Neuro.
//
//   neuro_test unit      JSON, WebSocket framing / handshake / URLs, the input queue, the actions and their
//                        validation, the Tempest 2000 profile, profile parsing, the protocol (Link) against a
//                        fake server, and the Agent's input gating (menu open, P1 not a pad).
//   neuro_test loopback  a WebSocket server on 127.0.0.1 (a free port): the Agent connects, sends startup and
//                        actions/register, answers a force with an action, plays its buttons out frame by frame,
//                        re-registers on actions/reregister_all, lets go of a held button when the server drops
//                        it, and reconnects; hardcore blocks it (no connect, input released, connects again when
//                        hardcore ends); a wrong Sec-WebSocket-Accept, an oversize / masked / reserved-bit frame
//                        and a flood are handled.  Skipped (77) on Windows.
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <mutex>
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
    // Unpaired surrogates become U+FFFD instead of invalid UTF-8; a pair that follows one still pairs.
    CHECK(parsed("\"\\ud83d\"").asString() == "\xEF\xBF\xBD");                       // lone high
    CHECK(parsed("\"\\ude00\"").asString() == "\xEF\xBF\xBD");                       // lone low
    CHECK(parsed("\"a\\ud83dz\"").asString() == "a\xEF\xBF\xBD" "z");               // high, then plain text
    CHECK(parsed("\"\\ud83d\\u0041\"").asString() == "\xEF\xBF\xBD" "A");          // high, then another escape
    CHECK(parsed("\"\\ud83d\\ud83d\\ude00\"").asString() == "\xEF\xBF\xBD\xf0\x9f\x98\x80");   // high, then a real pair
    CHECK(!Json::parse("\"\\ud83d\\u12\"", bad, err));                               // a truncated escape is an error
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
    CHECK(!parseWsUrl("ftp://host/x", u, err) && !parseWsUrl("file:///etc/passwd", u, err));
    CHECK(!parseWsUrl("https://host:8000", u, err) && err.find("ws://") != std::string::npos);
    CHECK(!parseWsUrl("", u, err) && !parseWsUrl("ws://", u, err) && !parseWsUrl("ws://:80", u, err));
    CHECK(parseWsUrl("WS://Host", u, err) && u.host == "Host");   // the scheme is not case sensitive
    // A "://" in the path is the path's, not a scheme.
    CHECK(parseWsUrl("localhost:8000/go?to=http://x", u, err) && u.host == "localhost" && u.path == "/go?to=http://x");
    CHECK(parseWsUrl("ws://localhost/go?to=http://x", u, err) && u.path == "/go?to=http://x");

    CHECK(isLoopbackHost("localhost") && isLoopbackHost("LOCALHOST") && isLoopbackHost("127.0.0.1") &&
          isLoopbackHost("127.9.9.9") && isLoopbackHost("::1") && isLoopbackHost("neuro.localhost"));
    CHECK(!isLoopbackHost("192.168.1.5") && !isLoopbackHost("example.org") && !isLoopbackHost("127.0.0.1.evil.org") &&
          !isLoopbackHost("128.0.0.1") && !isLoopbackHost("127.0.0.1x") && !isLoopbackHost(""));
    CHECK(!wsUrlIsRemote("ws://localhost:8000") && !wsUrlIsRemote("ws://[::1]:9") && !wsUrlIsRemote("garbage://x"));
    CHECK(wsUrlIsRemote("ws://192.168.1.5:8000") && wsUrlIsRemote("neuro.example.org:80"));
}

static void testWsFrames() {
    std::string err;
    // A masked client frame decodes back; a long one uses the 16-bit length; fragments are joined.
    const uint8_t mask[4] = {1, 2, 3, 4};
    FrameReader r(true);   // reading what a client sent: masked
    std::string big(300, 'z');
    std::string wire = encodeFrame(1, "hello", mask) + encodeFrame(1, big, mask);
    r.feed(wire.data(), 3);
    FrameReader::Msg m;
    CHECK(r.next(m, err) == 0);
    r.feed(wire.data() + 3, wire.size() - 3);
    CHECK(r.next(m, err) == 1 && m.opcode == 1 && m.payload == "hello");
    CHECK(r.next(m, err) == 1 && m.payload == big);
    FrameReader sr;   // server frames are unmasked
    std::string frag = std::string("\x01\x03" "abc", 5) + std::string("\x89\x01" "p", 3) + std::string("\x80\x02" "de", 4);
    sr.feed(frag.data(), frag.size());
    CHECK(sr.next(m, err) == 1 && m.opcode == 9 && m.payload == "p");   // a ping between fragments
    CHECK(sr.next(m, err) == 1 && m.opcode == 1 && m.payload == "abcde");
    std::string cont = std::string("\x80\x01" "x", 3);   // a continuation with nothing to continue
    sr.feed(cont.data(), cont.size());
    CHECK(sr.next(m, err) == -1);

    // What a SERVER may send: unmasked, no reserved bits, at most kMaxMessage.
    {
        FrameReader c;
        const std::string ok = std::string("\x81\x02" "hi", 4);
        c.feed(ok.data(), ok.size());
        CHECK(c.next(m, err) == 1 && m.payload == "hi");
    }
    {
        FrameReader c;
        const std::string masked = encodeFrame(1, "hi", mask);   // a client frame arriving at a client
        c.feed(masked.data(), masked.size());
        CHECK(c.next(m, err) == -1 && err.find("masked") != std::string::npos);
        FrameReader srv(true);   // and an unmasked frame arriving at a server
        const std::string plain = std::string("\x81\x02" "hi", 4);
        srv.feed(plain.data(), plain.size());
        CHECK(srv.next(m, err) == -1 && err.find("unmasked") != std::string::npos);
    }
    for (int rsv : {0x40, 0x20, 0x10}) {   // RSV1-3: an extension nobody negotiated
        FrameReader c;
        const std::string f = std::string(1, (char)(0x81 | rsv)) + std::string("\x02" "hi", 3);
        c.feed(f.data(), f.size());
        CHECK(c.next(m, err) == -1 && err.find("reserved") != std::string::npos);
    }
    {
        CHECK(FrameReader::kMaxMessage <= (1u << 20));   // Neuro's messages are tiny; a server does not get to make us buffer more
        // The length alone is enough: the header of a too-big frame is refused before any payload is waited for.
        FrameReader c;
        std::string hdr = "\x82\x7f";
        const uint64_t n = (uint64_t)FrameReader::kMaxMessage + 1;
        for (int i = 7; i >= 0; i--) hdr.push_back((char)((n >> (8 * i)) & 0xFF));
        c.feed(hdr.data(), hdr.size());
        CHECK(c.next(m, err) == -1 && err.find("too large") != std::string::npos);
        FrameReader fits;   // exactly the limit is fine (and waits for its payload)
        hdr.resize(2);
        const uint64_t lim = FrameReader::kMaxMessage;
        for (int i = 7; i >= 0; i--) hdr.push_back((char)((lim >> (8 * i)) & 0xFF));
        fits.feed(hdr.data(), hdr.size());
        CHECK(fits.next(m, err) == 0);
    }
    {
        // Fragments that each fit but add up to too much.
        FrameReader c;
        const std::string chunk(FrameReader::kMaxMessage / 2 + 1, 'x');
        std::string wire = std::string("\x01\x7f", 2);
        auto len64 = [](std::string& w, uint64_t n) {
            for (int i = 7; i >= 0; i--) w.push_back((char)((n >> (8 * i)) & 0xFF));
        };
        len64(wire, chunk.size());
        wire += chunk;
        wire += std::string("\x00\x7f", 2);
        len64(wire, chunk.size());
        wire += chunk;
        c.feed(wire.data(), wire.size());
        CHECK(c.next(m, err) == -1 && err.find("too large") != std::string::npos);
    }
    {
        FrameReader c;   // control frames: no fragmenting, at most 125 bytes
        const std::string frag = std::string("\x09\x00", 2);   // a ping with FIN clear
        c.feed(frag.data(), frag.size());
        CHECK(c.next(m, err) == -1);
        FrameReader d;
        const std::string longPing = std::string("\x89\x7e\x00\x7e", 4) + std::string(126, 'p');
        d.feed(longPing.data(), longPing.size());
        CHECK(d.next(m, err) == -1);
    }
}

// stop() must not wait for a name lookup that hangs: the worker abandons it (g_testLookupDelayMs stands in for the
// resolver; no DNS is involved).
static void testStopIsPrompt() {
    g_testLookupDelayMs = 3000;
    WsClient c;
    c.start("ws://localhost:8000");
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    const auto t0 = std::chrono::steady_clock::now();
    c.stop();
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    CHECK(ms < 1000);
    CHECK(!c.running() && c.state() == WsClient::State::Stopped);
    g_testLookupDelayMs = 0;
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

// The Agent against a link that is "connected" by hand (no socket): Player 1's buttons that cannot reach the game
// (the in-game menu is open, P1 is a mouse) are refused with the reason and never queued, and what was queued is
// dropped, so the server is not told "success" for input that would be thrown away.
static void testAgentGating() {
    Agent agent;
    std::vector<Json> sent;
    agent.link().send = [&](const std::string& m) { sent.push_back(parsed(m)); };
    AgentSettings s;
    s.enabled = true;
    s.url = "ws://127.0.0.1:9";   // nothing listens; the worker just fails to connect
    agent.configure(s);
    GameState g;
    g.loaded = true;
    g.title = "Tempest 2000";
    agent.tick(0.0, g);
    agent.link().onConnected();
    sent.clear();

    const char* press = "{\"command\":\"action\",\"data\":{\"id\":\"1\",\"name\":\"press_buttons\",\"data\":\"{\\\"buttons\\\":[\\\"b\\\"],\\\"frames\\\":30}\"}}";
    agent.link().onMessage(press);
    CHECK(sent.size() == 1 && sent[0]["data"]["success"].asBool());
    CHECK(agent.frame().test(C_B));   // playing

    // The menu opens: what is queued goes, the AI is told, and nothing new is accepted.
    sent.clear();
    g.inputBlock = "the in-game menu is open";
    agent.tick(0.1, g);
    CHECK(!agent.frame().any() && !agent.pressedNow().any());
    CHECK(sent.size() == 1 && sent[0]["command"].asString() == "context" &&
          sent[0]["data"]["message"].asString().find("menu is open") != std::string::npos);
    sent.clear();
    agent.link().onMessage(press);
    CHECK(sent.size() == 1 && sent[0]["command"].asString() == "action/result" && !sent[0]["data"]["success"].asBool());
    CHECK(sent[0]["data"]["message"].asString().find("menu is open") != std::string::npos);
    CHECK(!agent.frame().any() && !agent.pressedNow().any());   // nothing was queued behind the menu
    sent.clear();
    agent.link().onMessage("{\"command\":\"action\",\"data\":{\"id\":\"2\",\"name\":\"release_all\"}}");
    CHECK(sent.size() == 1 && sent[0]["data"]["success"].asBool());   // letting go is always fine

    // The menu closes: input flows again.
    sent.clear();
    g.inputBlock = nullptr;
    agent.tick(0.2, g);
    CHECK(sent.size() == 1 && sent[0]["command"].asString() == "context");
    sent.clear();
    agent.link().onMessage(press);
    CHECK(sent.size() == 1 && sent[0]["data"]["success"].asBool() && agent.frame().test(C_B));

    // Player 1 is a mouse: the same refusal, with its own reason.
    sent.clear();
    g.inputBlock = "Player 1 is set to a mouse, which has no controller buttons";
    agent.tick(0.3, g);
    sent.clear();
    agent.link().onMessage(press);
    CHECK(sent.size() == 1 && !sent[0]["data"]["success"].asBool() &&
          sent[0]["data"]["message"].asString().find("mouse") != std::string::npos);
    CHECK(!agent.frame().any());

    // No moves are asked for while the input is blocked.
    agent.link().forceInterval = 0;
    sent.clear();
    agent.tick(10.0, g);
    agent.tick(20.0, g);
    for (const Json& j : sent) CHECK(j["command"].asString() != "actions/force");
    g.inputBlock = nullptr;
    agent.tick(30.0, g);
    agent.tick(31.0, g);
    bool forced = false;
    for (const Json& j : sent) forced |= j["command"].asString() == "actions/force";
    CHECK(forced);

    // Hardcore: the Agent reports itself blocked, presses nothing, and says why.
    std::vector<std::string> toasts;
    agent.notify = [&](const std::string& t) { toasts.push_back(t); };
    agent.link().onMessage(press);
    CHECK(agent.frame().test(C_B));
    s.hardcore = true;
    agent.configure(s);
    CHECK(agent.blockedByHardcore() && !agent.allowed() && !agent.driving());
    CHECK(!agent.frame().any() && !agent.pressedNow().any() && !agent.link().connected());
    CHECK(agent.statusText().find("hardcore") != std::string::npos);
    bool told = false;
    for (const std::string& t : toasts) told |= t.find("hardcore") != std::string::npos;
    CHECK(told);
    s.hardcore = false;
    agent.configure(s);
    CHECK(agent.allowed() && !agent.blockedByHardcore());
    agent.configure(AgentSettings());   // closes the worker
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
    std::string acceptOverride;   // != "": answer the handshake with this Sec-WebSocket-Accept instead of the right one

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
            if (fd >= 0 && handshake(fd, acceptOverride)) return fd;
            if (fd >= 0) ::close(fd);
        }
        return -1;
    }

    static bool handshake(int fd, const std::string& acceptOverride) {
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
                                 "Sec-WebSocket-Accept: " + (acceptOverride.empty() ? wsAccept(key) : acceptOverride) + "\r\n\r\n";
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

// The server went away for good (it saw the client close), within `ms`.
static bool peerClosed(int fd, int ms) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (std::chrono::steady_clock::now() < end) {
        pollfd p{fd, POLLIN, 0};
        if (::poll(&p, 1, 20) > 0) {
            char buf[512];
            if (::recv(fd, buf, sizeof buf, 0) <= 0) return true;
        }
    }
    return false;
}

static bool waitEvent(WsClient& c, WsClient::Event::Kind kind, WsClient::Event& out, int ms) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (std::chrono::steady_clock::now() < end) {
        WsClient::Event ev;
        while (c.poll(ev))
            if (ev.kind == kind) return (out = ev, true);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return false;
}

// The client against a server that misbehaves.
static void loopbackHostile() {
    FakeServer srv;
    if (!srv.open()) {
        std::printf("FAIL cannot listen on 127.0.0.1\n");
        g_fail++;
        return;
    }
    const std::string url = "ws://127.0.0.1:" + std::to_string(srv.port);

    // A server that answers the upgrade with the wrong Sec-WebSocket-Accept is not a WebSocket server we can trust.
    {
        srv.acceptOverride = "AAAAAAAAAAAAAAAAAAAAAAAAAAA=";
        WsClient c;
        c.start(url);
        const int fd = srv.accept(5000);
        CHECK(fd >= 0);
        WsClient::Event ev;
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        CHECK(!waitEvent(c, WsClient::Event::Connected, ev, 200));
        CHECK(c.state() != WsClient::State::Open);
        CHECK(c.lastError().find("Sec-WebSocket-Accept") != std::string::npos);
        if (fd >= 0) {
            CHECK(peerClosed(fd, 2000));   // it hung up
            ::close(fd);
        }
        c.stop();
        srv.acceptOverride.clear();
    }

    // Frames a server must not send: the link drops, with a reason, and the client comes back.
    struct Bad {
        const char* what;
        std::string bytes;
        const char* needle;
    };
    std::string big = "\x82\x7f";
    const uint64_t over = (uint64_t)FrameReader::kMaxMessage + 1;
    for (int i = 7; i >= 0; i--) big.push_back((char)((over >> (8 * i)) & 0xFF));
    const uint8_t mk[4] = {9, 8, 7, 6};
    const Bad bad[] = {
        {"oversize frame", big, "too large"},
        {"masked server frame", encodeFrame(kOpText, "{}", mk), "masked"},
        {"reserved bits", std::string("\xC1\x02" "{}", 4), "reserved"},
    };
    WsClient c;
    c.start(url);
    for (const Bad& b : bad) {
        const int fd = srv.accept(8000);
        CHECK(fd >= 0);
        if (fd < 0) break;
        WsClient::Event ev;
        CHECK(waitEvent(c, WsClient::Event::Connected, ev, 3000));
        ::send(fd, b.bytes.data(), b.bytes.size(), MSG_NOSIGNAL);
        const bool dropped = waitEvent(c, WsClient::Event::Disconnected, ev, 3000);
        CHECK(dropped);
        if (dropped && ev.text.find(b.needle) == std::string::npos)
            std::printf("FAIL %s: dropped with \"%s\", wanted \"%s\"\n", b.what, ev.text.c_str(), b.needle), g_fail++;
        ::close(fd);
    }

    // A flood while nobody polls: the queue holds a bounded number of bytes, and the link's own events survive.
    {
        const int fd = srv.accept(8000);
        CHECK(fd >= 0);
        if (fd >= 0) {
            const std::string payload(200000, 'f');
            std::string frame = std::string("\x81\x7f", 2);
            for (int i = 7; i >= 0; i--) frame.push_back((char)((payload.size() >> (8 * i)) & 0xFF));
            frame += payload;
            for (int i = 0; i < 40; i++) {   // 8 MB, twice the cap
                size_t off = 0;
                while (off < frame.size()) {
                    pollfd p{fd, POLLOUT, 0};
                    if (::poll(&p, 1, 2000) <= 0) break;
                    const ssize_t n = ::send(fd, frame.data() + off, frame.size() - off, MSG_NOSIGNAL);
                    if (n <= 0) break;
                    off += (size_t)n;
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            size_t bytes = 0, messages = 0;
            bool connectedKept = false;
            WsClient::Event ev;
            while (c.poll(ev)) {
                if (ev.kind == WsClient::Event::Message) bytes += ev.text.size(), messages++;
                if (ev.kind == WsClient::Event::Connected) connectedKept = true;
            }
            CHECK(connectedKept);
            CHECK(messages > 0 && messages < 40);
            CHECK(bytes <= WsClient::kMaxQueuedBytes);
            ::close(fd);
        }
    }
    c.stop();
}

// Hardcore, from the start: the Agent never connects; when hardcore ends it does.
static void loopbackHardcoreFirst() {
    FakeServer srv;
    if (!srv.open()) {
        std::printf("FAIL cannot listen on 127.0.0.1\n");
        g_fail++;
        return;
    }
    Agent agent;
    AgentSettings s;
    s.enabled = true;
    s.hardcore = true;
    s.url = "ws://127.0.0.1:" + std::to_string(srv.port);
    agent.configure(s);
    GameState g;
    g.loaded = true;
    g.title = "Tempest 2000";
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(2500);   // longer than a retry
    while (std::chrono::steady_clock::now() < end) {
        agent.tick(0.0, g);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    CHECK(srv.accept(100) < 0);   // nobody knocked
    CHECK(!agent.link().connected() && !agent.driving());
    s.hardcore = false;
    agent.configure(s);
    agent.tick(1.0, g);
    const int fd = srv.accept(5000);
    CHECK(fd >= 0);
    if (fd >= 0) ::close(fd);
    agent.configure(AgentSettings());
}

static int loopback() {
    loopbackHardcoreFirst();
    loopbackHostile();
    FakeServer srv;
    if (!srv.open()) {
        std::printf("FAIL cannot listen on 127.0.0.1\n");
        return 1;
    }
    Agent agent;
    agent.log = [](const std::string& l) { std::printf("  [agent] %s\n", l.c_str()); };
    std::mutex toastMu;
    std::vector<std::string> toasts;
    agent.notify = [&](const std::string& t) {
        std::lock_guard<std::mutex> l(toastMu);
        toasts.push_back(t);
    };
    AgentSettings s;
    s.enabled = true;
    s.url = "ws://127.0.0.1:" + std::to_string(srv.port);
    s.forceInterval = 0.05;
    agent.configure(s);

    GameState g;
    g.loaded = true;
    g.title = "Tempest 2000";
    std::atomic<bool> run{true}, hardcore{false};
    std::atomic<int> pressedB{0};
    std::atomic<bool> rightNow{false}, upNow{false};
    // The app's loop, on its own thread: a UI tick and an emulated frame every ~5 ms.  The settings go through
    // configure() every iteration, as the app does.
    std::thread app([&] {
        double t = 0;
        AgentSettings cfg = s;
        while (run) {
            cfg.hardcore = hardcore.load();
            agent.configure(cfg);
            agent.tick(t, g);
            const ControlSet pressed = agent.frame();
            if (pressed.test(C_B)) pressedB++;
            rightNow = pressed.test(C_RIGHT);
            upNow = pressed.test(C_UP);
            t += 0.005;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    });
    auto waitFlag = [](std::atomic<bool>& f, bool want, int ms) {
        const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
        while (std::chrono::steady_clock::now() < end && f.load() != want) std::this_thread::sleep_for(std::chrono::milliseconds(5));
        return f.load() == want;
    };

    int fd = srv.accept(5000);
    CHECK(fd >= 0);
    FrameReader rd(true);
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

    // hold_buttons, then the server drops the link: the button must not stay held.
    std::string hold = serverFrame("{\"command\":\"action\",\"data\":{\"id\":\"h1\",\"name\":\"hold_buttons\",\"data\":\"{\\\"buttons\\\":[\\\"right\\\"]}\"}}");
    ::send(fd, hold.data(), hold.size(), 0);
    CHECK(waitFor(fd, rd, "action/result", agent, &seen)["data"]["success"].asBool());
    CHECK(waitFlag(rightNow, true, 2000));
    ::close(fd);   // the server goes away; the client comes back by itself
    CHECK(waitFlag(rightNow, false, 3000));
    int fd2 = srv.accept(8000);
    CHECK(fd2 >= 0);
    CHECK(!rightNow.load());
    if (fd2 >= 0) {
        FrameReader rd2(true);
        CHECK(waitFor(fd2, rd2, "startup", agent, nullptr)["command"].asString() == "startup");

        // RetroAchievements hardcore turns on: the link is closed and the held button let go.
        std::string holdUp = serverFrame("{\"command\":\"action\",\"data\":{\"id\":\"h2\",\"name\":\"hold_buttons\",\"data\":\"{\\\"buttons\\\":[\\\"up\\\"]}\"}}");
        ::send(fd2, holdUp.data(), holdUp.size(), 0);
        CHECK(waitFor(fd2, rd2, "action/result", agent, nullptr)["data"]["success"].asBool());
        CHECK(waitFlag(upNow, true, 2000));
        hardcore = true;
        CHECK(waitFlag(upNow, false, 2000));
        CHECK(peerClosed(fd2, 3000));
        ::close(fd2);
        CHECK(srv.accept(2500) < 0);   // and it stays away (a retry would have come by now)
        CHECK(!upNow.load());

        // Hardcore ends: it connects again.
        hardcore = false;
        int fd3 = srv.accept(5000);
        CHECK(fd3 >= 0);
        if (fd3 >= 0) {
            FrameReader rd3(true);
            CHECK(waitFor(fd3, rd3, "startup", agent, nullptr)["command"].asString() == "startup");
            CHECK(!upNow.load() && !rightNow.load());
            ::close(fd3);
        }
    }
    run = false;
    app.join();
    {
        std::lock_guard<std::mutex> l(toastMu);
        bool conn = false, disc = false, hc = false;
        for (const std::string& t : toasts) {
            conn |= t.find("connected:") != std::string::npos;
            disc |= t.find("disconnected") != std::string::npos;
            hc |= t.find("hardcore") != std::string::npos;
        }
        CHECK(conn && disc && hc);
    }
    agent.configure(AgentSettings());
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
    testAgentGating();
    testStopIsPrompt();
    std::printf(g_fail ? "neuro unit: %d FAILED\n" : "neuro unit: ok\n", g_fail);
    return g_fail ? 1 : 0;
}
