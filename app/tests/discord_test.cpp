// Discord Rich Presence (#833): pure pieces (`unit`) and a fake Discord IPC server on a Unix socket (`loopback`).
#include "discord_ipc.h"
#include "discord_presence.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#ifndef _WIN32
#  include <fcntl.h>
#  include <poll.h>
#  include <sys/socket.h>
#  include <sys/stat.h>
#  include <sys/un.h>
#  include <unistd.h>
#endif

using namespace vj::discord;

static int g_checks = 0, g_fail = 0;
#define CHECK(c) do { g_checks++; if (!(c)) { g_fail++; std::fprintf(stderr, "FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)
#define CHECK_EQ(a, b) do { g_checks++; std::string _a = (a), _b = (b); if (_a != _b) { g_fail++; \
    std::fprintf(stderr, "FAIL %s:%d  %s\n   got:  %s\n   want: %s\n", __FILE__, __LINE__, #a, _a.c_str(), _b.c_str()); } } while (0)

static bool has(const std::string& s, const std::string& sub) { return s.find(sub) != std::string::npos; }

// ---- a strict-enough JSON validator, so the builders cannot emit something Discord's parser would refuse ----
struct JsonCheck {
    const std::string& s;
    size_t i = 0;
    explicit JsonCheck(const std::string& str) : s(str) {}
    void ws() { while (i < s.size() && (s[i] == ' ' || s[i] == '\n' || s[i] == '\t' || s[i] == '\r')) i++; }
    bool lit(const char* w) { size_t n = std::strlen(w); if (s.compare(i, n, w) == 0) { i += n; return true; } return false; }
    bool str() {
        if (i >= s.size() || s[i] != '"') return false;
        i++;
        while (i < s.size() && s[i] != '"') {
            if ((unsigned char)s[i] < 0x20) return false;   // raw control character
            if (s[i] == '\\') {
                i++;
                if (i >= s.size()) return false;
                if (s[i] == 'u') { if (i + 4 >= s.size()) return false; i += 4; }
                else if (!std::strchr("\"\\/bfnrt", s[i])) return false;
            }
            i++;
        }
        if (i >= s.size()) return false;
        i++;
        return true;
    }
    bool num() {
        size_t st = i;
        if (i < s.size() && s[i] == '-') i++;
        while (i < s.size() && ((s[i] >= '0' && s[i] <= '9') || s[i] == '.')) i++;
        return i > st;
    }
    bool val() {
        ws();
        if (i >= s.size()) return false;
        if (s[i] == '{') {
            i++; ws();
            if (i < s.size() && s[i] == '}') { i++; return true; }
            for (;;) {
                ws();
                if (!str()) return false;
                ws();
                if (i >= s.size() || s[i] != ':') return false;
                i++;
                if (!val()) return false;
                ws();
                if (i < s.size() && s[i] == ',') { i++; continue; }
                if (i < s.size() && s[i] == '}') { i++; return true; }
                return false;
            }
        }
        if (s[i] == '[') {
            i++; ws();
            if (i < s.size() && s[i] == ']') { i++; return true; }
            for (;;) {
                if (!val()) return false;
                ws();
                if (i < s.size() && s[i] == ',') { i++; continue; }
                if (i < s.size() && s[i] == ']') { i++; return true; }
                return false;
            }
        }
        if (s[i] == '"') return str();
        if (lit("true") || lit("false") || lit("null")) return true;
        return num();
    }
    bool valid() { if (!val()) return false; ws(); return i == s.size(); }
};
static bool validJson(const std::string& s) { JsonCheck j(s); return j.valid(); }

// ---------------------------------------------------------------------------------------------
static void testFrames() {
    const std::string f = encodeFrame(OpHandshake, "{\"v\":1}");
    CHECK(f.size() == 8 + 7);
    CHECK((unsigned char)f[0] == 0 && (unsigned char)f[4] == 7 && f[5] == 0 && f[6] == 0 && f[7] == 0);   // op 0, length 7, little-endian
    CHECK(f.substr(8) == "{\"v\":1}");
    // The example in Discord's docs: opcode 0, length 45 -> 00 00 00 00 2D 00 00 00
    const std::string big = encodeFrame(OpFrame, std::string(300, 'x'));
    CHECK((unsigned char)big[0] == 1 && (unsigned char)big[4] == 0x2C && (unsigned char)big[5] == 0x01);   // 300 = 0x012C

    FrameDecoder d;
    Frame o;
    CHECK(!d.next(o));
    const std::string two = encodeFrame(OpFrame, "{\"evt\":\"READY\"}") + encodeFrame(OpPing, "ab");
    for (size_t i = 0; i < two.size(); i++) {   // one byte at a time: any split must work
        d.feed(&two[i], 1);
        if (i + 1 < encodeFrame(OpFrame, "{\"evt\":\"READY\"}").size()) CHECK(!d.next(o));
    }
    CHECK(d.next(o) && o.op == OpFrame && o.payload == "{\"evt\":\"READY\"}");
    CHECK(d.next(o) && o.op == OpPing && o.payload == "ab");
    CHECK(!d.next(o) && d.buffered() == 0);
    // Both at once, and an empty payload.
    FrameDecoder d2;
    const std::string both = encodeFrame(OpPong, "") + encodeFrame(OpClose, "{\"code\":4000}");
    d2.feed(both.data(), both.size());
    CHECK(d2.next(o) && o.op == OpPong && o.payload.empty());
    CHECK(d2.next(o) && o.op == OpClose);
    // A hostile length poisons the stream instead of buffering for ever.
    FrameDecoder d3;
    std::string evil = encodeFrame(OpFrame, "x");
    evil[7] = 0x7F;   // length ~2 GiB
    d3.feed(evil.data(), evil.size());
    CHECK(!d3.next(o) && d3.bad());
    d3.feed("zzzz", 4);
    CHECK(!d3.next(o) && d3.buffered() == 0);
}

static void testJson() {
    CHECK_EQ(jsonEscape("plain"), "plain");
    CHECK_EQ(jsonEscape("a\"b\\c"), "a\\\"b\\\\c");
    CHECK_EQ(jsonEscape("line\nnext\ttab\r"), "line\\nnext\\ttab\\r");
    CHECK_EQ(jsonEscape(std::string("nul\x01x", 5)), "nul\\u0001x");
    CHECK_EQ(jsonEscape("caf\xC3\xA9 \xE2\x80\x94 \xF0\x9F\x95\xB9"), "caf\xC3\xA9 \xE2\x80\x94 \xF0\x9F\x95\xB9");   // UTF-8 passes through
    CHECK_EQ(jsonString("a\"b"), "\"a\\\"b\"");
    // truncate on a character boundary
    CHECK_EQ(truncateUtf8("abcdef", 4), "abcd");
    CHECK_EQ(truncateUtf8("abc", 10), "abc");
    CHECK_EQ(truncateUtf8("ab\xC3\xA9" "cd", 3), "ab");              // would cut the 2-byte e-acute in half
    CHECK_EQ(truncateUtf8("ab\xC3\xA9" "cd", 4), "ab\xC3\xA9");
    CHECK_EQ(truncateUtf8("\xE2\x80\x94\xE2\x80\x94", 5), "\xE2\x80\x94");
    std::string s;
    long long n = 0;
    const std::string ready = "{\"cmd\":\"DISPATCH\",\"data\":{\"v\":1,\"config\":{}},\"evt\":\"READY\",\"nonce\":null}";
    CHECK(jsonFindString(ready, "evt", s) && s == "READY");
    CHECK(!jsonFindString(ready, "nonce", s));   // null, not a string
    const std::string close = "{\"code\": 4000, \"message\": \"Invalid Client ID\"}";
    CHECK(jsonFindInt(close, "code", n) && n == 4000);
    CHECK(jsonFindString(close, "message", s) && s == "Invalid Client ID");
    CHECK(!jsonFindInt(close, "missing", n));
    CHECK(jsonFindString("{\"evt\":\"a\\\"b\"}", "evt", s) && s == "a\"b");

    CHECK_EQ(handshakePayload("123456789012345678"), "{\"v\":1,\"client_id\":\"123456789012345678\"}");
    const std::string p = setActivityPayload(4242, "n1", "{\"type\":0}");
    CHECK_EQ(p, "{\"cmd\":\"SET_ACTIVITY\",\"args\":{\"pid\":4242,\"activity\":{\"type\":0}},\"nonce\":\"n1\"}");
    CHECK(validJson(p));
    const std::string clear = setActivityPayload(4242, "n2", "");
    CHECK_EQ(clear, "{\"cmd\":\"SET_ACTIVITY\",\"args\":{\"pid\":4242,\"activity\":null},\"nonce\":\"n2\"}");
    CHECK(validJson(clear));
}

static void testPaths() {
    std::map<std::string, std::string> env;
    auto get = [&](const char* k) { auto it = env.find(k); return it == env.end() ? std::string() : it->second; };
    // Nothing set: /tmp only, 10 sockets.
    std::vector<std::string> v = socketCandidates(Platform::Unix, get);
    CHECK(v.size() == 10);
    CHECK(v.front() == "/tmp/discord-ipc-0" && v.back() == "/tmp/discord-ipc-9");
    // XDG first, with the Flatpak and Snap folders; TMPDIR's trailing slash does not double up; duplicates drop.
    env["XDG_RUNTIME_DIR"] = "/run/user/1000";
    env["TMPDIR"] = "/var/folders/ab/T/";
    env["TMP"] = "/tmp";
    v = socketCandidates(Platform::Unix, get);
    CHECK(v[0] == "/run/user/1000/discord-ipc-0");
    CHECK(v[1] == "/run/user/1000/app/com.discordapp.Discord/discord-ipc-0");
    CHECK(v[2] == "/run/user/1000/snap.discord/discord-ipc-0");
    CHECK(v[3] == "/var/folders/ab/T/discord-ipc-0");
    CHECK(v[4] == "/tmp/discord-ipc-0");
    CHECK(v[5] == "/run/user/1000/discord-ipc-1");   // socket number is the outer loop
    CHECK(v.size() == 50);
    for (const std::string& p : v) CHECK(!has(p, "//"));
    int tmpCount = 0;
    for (const std::string& p : v) if (p == "/tmp/discord-ipc-0") tmpCount++;
    CHECK(tmpCount == 1);
    // macOS TMPDIR with a trailing slash is the common case.
    env.clear();
    env["TMPDIR"] = "/var/folders/zz/T/";
    v = socketCandidates(Platform::Unix, get);
    CHECK(v[0] == "/var/folders/zz/T/discord-ipc-0");
    // Windows: named pipes.
    v = socketCandidates(Platform::Windows, get);
    CHECK(v.size() == 10 && v[0] == "\\\\.\\pipe\\discord-ipc-0" && v[9] == "\\\\.\\pipe\\discord-ipc-9");
}

static void testRateLimiter() {
    RateLimiter r(3, 20.0);
    CHECK(r.tryAcquire(0.0));
    CHECK(r.tryAcquire(1.0));
    CHECK(r.tryAcquire(2.0));
    CHECK(!r.tryAcquire(3.0));
    CHECK(r.waitSeconds(3.0) > 16.9 && r.waitSeconds(3.0) < 17.1);   // until the first one (t=0) is 20 s old
    CHECK(!r.tryAcquire(19.9));
    CHECK(r.tryAcquire(20.0));
    CHECK(!r.tryAcquire(20.5));                                      // t=1 and t=2 still count
    CHECK(r.waitSeconds(21.0) < 1e-9);                               // t=1 expired at 21
    CHECK(r.tryAcquire(21.0));
    RateLimiter d;   // defaults stay under Discord's 5 per 20 s
    int ok = 0;
    for (int i = 0; i < 10; i++) if (d.tryAcquire(i * 0.1)) ok++;
    CHECK(ok == 4);
}

static PresenceInput game() {
    PresenceInput in;
    in.playing = true;
    in.title = "Tempest 2000";
    in.startUnix = 1700000000;
    in.version = "3.8.1";
    return in;
}

static void testActivity() {
    CHECK_EQ(displayTitle("Tempest 2000 (World)"), "Tempest 2000");
    CHECK_EQ(displayTitle("Iron Soldier 2 (World) (Rev 1) [!]"), "Iron Soldier 2");
    CHECK_EQ(displayTitle("Myst (USA) (Disc 2)"), "Myst (Disc 2)");
    CHECK_EQ(displayTitle("  Doom   (USA, Europe)  "), "Doom");
    CHECK_EQ(displayTitle("(Unknown)"), "(Unknown)");   // nothing but a tag: keep it rather than a blank line
    CHECK_EQ(displayTitle("Broken (title"), "Broken (title");
    CHECK_EQ(displayTitle(""), "");

    PresenceInput in = game();
    CHECK_EQ(stateLine(in), "Atari Jaguar");
    in.disc = true;
    CHECK_EQ(stateLine(in), "Jaguar CD");
    in.paused = true;
    CHECK_EQ(stateLine(in), "Jaguar CD | Paused");
    in.paused = false;
    in.disc = false;
    in.netplay = "hosting";
    CHECK_EQ(stateLine(in), "Atari Jaguar | Netplay: hosting");
    in.netplay = "joined";
    CHECK_EQ(stateLine(in), "Atari Jaguar | Netplay: joined");
    in.netplay = "";
    in.hardcore = true;
    in.achTotal = 30;
    in.achUnlocked = 12;
    CHECK_EQ(stateLine(in), "Atari Jaguar | Hardcore | 12/30 achievements");

    // The plain case: no art URL, no RA game -> asset keys, one button.
    in = game();
    std::string a = buildActivity(in);
    CHECK(validJson(a));
    CHECK(has(a, "\"type\":0"));
    CHECK(has(a, "\"details\":\"Tempest 2000\""));
    CHECK(has(a, "\"state\":\"Atari Jaguar\""));
    CHECK(has(a, "\"timestamps\":{\"start\":1700000000}"));
    CHECK(has(a, "\"large_image\":\"jaguar\""));
    CHECK(has(a, "\"large_text\":\"Tempest 2000\""));
    CHECK(has(a, "\"small_image\":\"app\""));
    CHECK(has(a, "\"small_text\":\"Virtual Jaguar 3.8.1\""));
    CHECK(has(a, "\"buttons\":[{\"label\":\"Get Virtual Jaguar\",\"url\":\"https://jaguar.provenance-emu.com/app.html\"}]"));
    CHECK(!has(a, "retroachievements"));

    // Box art URL as the large image; the plain form (the fallback) uses the asset key again.
    in.artUrl = "https://thumbnails.libretro.com/Atari%20-%20Jaguar/Named_Boxarts/Tempest%202000%20%28World%29.png";
    a = buildActivity(in);
    CHECK(validJson(a));
    CHECK(has(a, "\"large_image\":\"https://thumbnails.libretro.com/"));
    CHECK(!has(buildActivity(in, false), "thumbnails"));
    CHECK(has(buildActivity(in, false), "\"large_image\":\"jaguar\""));
    in.showArt = false;   // the setting
    CHECK(has(buildActivity(in), "\"large_image\":\"jaguar\""));
    in.showArt = true;
    // A disc without art (or with art off) shows the Jaguar CD picture, not the cartridge one.
    in.disc = true;
    in.artUrl.clear();
    CHECK(has(buildActivity(in), "\"large_image\":\"jaguarcd\""));
    in.artUrl = "https://example.com/a.png";
    CHECK(has(buildActivity(in), "\"large_image\":\"https://example.com/a.png\""));
    in.showArt = false;
    CHECK(has(buildActivity(in), "\"large_image\":\"jaguarcd\""));
    in.showArt = true;
    in.disc = false;

    // A RetroAchievements game adds the second button.
    in.raGameId = 1234;
    a = buildActivity(in);
    CHECK(validJson(a));
    CHECK(has(a, "{\"label\":\"View on RetroAchievements\",\"url\":\"https://retroachievements.org/game/1234\"}"));
    // Buttons off: no key at all.
    in.showButtons = false;
    a = buildActivity(in);
    CHECK(validJson(a));
    CHECK(!has(a, "buttons"));
    in.showButtons = true;

    // The library / no game: browsing, never the game's art or RA button, and the elapsed timer is the app's.
    PresenceInput lib;
    lib.startUnix = 1700000100;
    lib.version = "3.8.1";
    lib.artUrl = "https://example.com/x.png";
    lib.raGameId = 9;
    a = buildActivity(lib);
    CHECK(validJson(a));
    CHECK(has(a, "\"details\":\"Browsing the library\""));
    CHECK(!has(a, "retroachievements"));
    CHECK(has(a, "\"large_text\":\"Virtual Jaguar\""));
    CHECK(has(a, "\"large_image\":\"library\""));
    lib.startUnix = 0;
    CHECK(!has(buildActivity(lib), "timestamps"));

    // Escaping: quotes, backslashes, control characters, UTF-8 in a title must not break the JSON.
    in = game();
    in.title = "Rock \"n\" Roll \\ Racing\nLine2 \xE2\x80\x94 caf\xC3\xA9";
    a = buildActivity(in);
    CHECK(validJson(a));
    CHECK(has(a, "Rock \\\"n\\\" Roll \\\\ Racing\\nLine2 \xE2\x80\x94 caf\xC3\xA9"));

    // Limits: 128 bytes on a character boundary; a 1-byte field is padded to 2.
    in = game();
    in.title.clear();
    for (int i = 0; i < 100; i++) in.title += "\xC3\xA9";   // 200 bytes
    a = buildActivity(in);
    CHECK(validJson(a));
    std::string det;
    CHECK(jsonFindString(a, "details", det) && det.size() <= 128 && det.size() % 2 == 0);
    in.title = "X";
    a = buildActivity(in);
    CHECK(jsonFindString(a, "details", det) && det.size() == 3 && det.compare(0, 1, "X") == 0);   // "X" + no-break space
    CHECK(validJson(a));
}

static void testArtUrl() {
    const std::string root = "/data/";
    const std::string up = "https://thumbnails.libretro.com/";
    const std::string mirror = "https://jaguar.provenance-emu.com/thumbnails/";
    std::vector<std::string> present;
    auto exists = [&](const std::string& p) {
        for (const std::string& s : present) if (s == p) return true;
        return false;
    };
    // Nothing cached: no URL (Discord would show a broken image for a 404).
    CHECK(chooseArtUrl(vj::lib::Section::Cart, "Tempest 2000 (World)", root, up, mirror, exists).empty());
    present.push_back("/data/thumbnails/Atari - Jaguar/Named_Boxarts/Tempest 2000 (World).png");
    CHECK_EQ(chooseArtUrl(vj::lib::Section::Cart, "Tempest 2000 (World)", root, up, mirror, exists),
             "https://thumbnails.libretro.com/Atari%20-%20Jaguar/Named_Boxarts/Tempest%202000%20%28World%29.png");
    // Unidentified (no dat name) -> none, even if something is cached under another name.
    CHECK(chooseArtUrl(vj::lib::Section::Cart, "", root, up, mirror, exists).empty());
    // A disc whose picture came from the site mirror.
    present.clear();
    present.push_back("/data/thumbnails/Atari - Jaguar CD/Named_Boxarts/Myst (USA).png");
    CHECK_EQ(chooseArtUrl(vj::lib::Section::Disc, "Myst (USA)", root, up, mirror, exists),
             "https://jaguar.provenance-emu.com/thumbnails/Atari%20-%20Jaguar%20CD/Named_Boxarts/Myst%20%28USA%29.png");
    // Upstream wins when both are cached.
    present.push_back("/data/thumbnails/Atari - Jaguar/Named_Boxarts/Myst (USA).png");
    CHECK(has(chooseArtUrl(vj::lib::Section::Disc, "Myst (USA)", root, up, mirror, exists), "thumbnails.libretro.com"));
    // The mirror is for discs only.
    CHECK(chooseArtUrl(vj::lib::Section::Cart, "Myst (USA)", "/nowhere/", up, mirror, exists).empty());
    // BIOS entries never.
    CHECK(chooseArtUrl(vj::lib::Section::Bios, "Anything", root, up, mirror, exists).empty());
}

static void testPresenceManager() {
    Presence p;
    PresenceSettings s;
    CHECK(!p.available());
    CHECK(p.statusText().find("no Discord application ID") != std::string::npos);
    s.appId = "";
    p.configure(s);
    CHECK(!p.active());
    p.update(game());   // no-op without a client
    s.appId = "123";
    s.instance = 2;
    p.configure(s);
    CHECK(!p.active() && !p.available());
    CHECK(p.statusText().find("first window") != std::string::npos);
    s.instance = 1;
    s.enabled = false;
    p.configure(s);
    CHECK(!p.active());
    CHECK_EQ(p.statusText(), "Off.");
    p.shutdown();
}

static int runUnit() {
    testFrames();
    testJson();
    testPaths();
    testRateLimiter();
    testActivity();
    testArtUrl();
    testPresenceManager();
    std::printf("discord unit: %d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}

// ---------------------------------------------------------------------------------------------
// loopback
// ---------------------------------------------------------------------------------------------
#ifndef _WIN32

static double nowSec() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

template <class F>
static bool waitFor(F f, double seconds = 5.0) {
    const double end = nowSec() + seconds;
    while (nowSec() < end) {
        if (f()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return f();
}

// A one-client-at-a-time fake Discord.
class FakeDiscord {
public:
    bool listenAt(const std::string& path) {
        path_ = path;
        ::unlink(path.c_str());
        lfd_ = ::socket(AF_UNIX, SOCK_STREAM, 0);
        struct sockaddr_un a;
        std::memset(&a, 0, sizeof a);
        a.sun_family = AF_UNIX;
        std::snprintf(a.sun_path, sizeof a.sun_path, "%s", path.c_str());
        if (::bind(lfd_, (struct sockaddr*)&a, sizeof a) != 0 || ::listen(lfd_, 4) != 0) return false;
        th_ = std::thread([this] { loop(); });
        return true;
    }
    ~FakeDiscord() { stop(); }
    void stop() {
        quit_ = true;
        if (th_.joinable()) th_.join();
        if (lfd_ >= 0) { ::close(lfd_); lfd_ = -1; }
        if (cfd_ >= 0) { ::close(cfd_); cfd_ = -1; }
        ::unlink(path_.c_str());
    }
    // behaviour knobs
    std::atomic<bool> errorOnActivity{false};   // answer SET_ACTIVITY with evt ERROR
    std::atomic<bool> rejectClientId{false};    // answer the handshake with CLOSE 4000
    void sendFrame(uint32_t op, const std::string& payload) {
        std::lock_guard<std::mutex> lk(wm_);
        if (cfd_ < 0) return;
        const std::string f = encodeFrame(op, payload);
        ::send(cfd_, f.data(), f.size(), 0);
    }
    void dropClient() {
        std::lock_guard<std::mutex> lk(wm_);
        if (cfd_ >= 0) { ::close(cfd_); cfd_ = -1; }
    }
    std::vector<Frame> frames() { std::lock_guard<std::mutex> lk(m_); return got_; }
    size_t handshakes() { std::lock_guard<std::mutex> lk(m_); return handshakes_; }
    std::vector<std::string> activities() {   // payloads of SET_ACTIVITY frames
        std::vector<std::string> v;
        for (const Frame& f : frames()) if (f.op == OpFrame && has(f.payload, "SET_ACTIVITY")) v.push_back(f.payload);
        return v;
    }
    size_t pongs() { size_t n = 0; for (const Frame& f : frames()) if (f.op == OpPong) n++; return n; }

private:
    void loop() {
        while (!quit_) {
            struct pollfd pf = {lfd_, POLLIN, 0};
            if (::poll(&pf, 1, 50) <= 0) continue;
            int fd = ::accept(lfd_, nullptr, nullptr);
            if (fd < 0) continue;
            { std::lock_guard<std::mutex> lk(wm_); cfd_ = fd; }
            FrameDecoder dec;
            bool shaken = false;
            while (!quit_) {
                int cur;
                { std::lock_guard<std::mutex> lk(wm_); cur = cfd_; }
                if (cur != fd) break;   // dropClient()
                struct pollfd cp = {fd, POLLIN, 0};
                if (::poll(&cp, 1, 20) <= 0) continue;
                char buf[4096];
                ssize_t n = ::recv(fd, buf, sizeof buf, 0);
                if (n <= 0) break;
                dec.feed(buf, (size_t)n);
                Frame f;
                while (dec.next(f)) {
                    { std::lock_guard<std::mutex> lk(m_); got_.push_back(f); }
                    if (f.op == OpHandshake && !shaken) {
                        { std::lock_guard<std::mutex> lk(m_); handshakes_++; }
                        shaken = true;
                        if (rejectClientId) sendFrame(OpClose, "{\"code\":4000,\"message\":\"Invalid Client ID\"}");
                        else sendFrame(OpFrame, "{\"cmd\":\"DISPATCH\",\"data\":{\"v\":1},\"evt\":\"READY\",\"nonce\":null}");
                    } else if (f.op == OpFrame && has(f.payload, "SET_ACTIVITY")) {
                        std::string nonce;
                        jsonFindString(f.payload, "nonce", nonce);
                        if (errorOnActivity)
                            sendFrame(OpFrame, "{\"cmd\":\"SET_ACTIVITY\",\"evt\":\"ERROR\",\"data\":{\"code\":4000,\"message\":\"bad\"},\"nonce\":\"" + nonce + "\"}");
                        else
                            sendFrame(OpFrame, "{\"cmd\":\"SET_ACTIVITY\",\"data\":{},\"evt\":null,\"nonce\":\"" + nonce + "\"}");
                    }
                }
            }
            std::lock_guard<std::mutex> lk(wm_);
            if (cfd_ == fd) { ::close(fd); cfd_ = -1; }
        }
    }
    std::string path_;
    int lfd_ = -1, cfd_ = -1;
    std::thread th_;
    std::atomic<bool> quit_{false};
    std::mutex m_, wm_;
    std::vector<Frame> got_;
    size_t handshakes_ = 0;
};

static Client::Options opts(const std::string& path) {
    Client::Options o;
    o.appId = "111222333444555666";
    o.pid = 4242;
    o.paths = {path};
    o.retrySeconds = 0.2;
    o.maxUpdates = 3;
    o.windowSeconds = 1.0;
    o.ioTimeoutMs = 1000;
    return o;
}

static int runLoopback() {
    // sun_path is 104 bytes on macOS: a short, private folder (never the long $TMPDIR).
    std::string dir = "/tmp/vjd-" + std::to_string((long)::getpid());
    ::mkdir(dir.c_str(), 0700);
    const std::string sock = dir + "/discord-ipc-0";

    // 1. Discord not running: the client searches, never blocks, and finds it when it appears.
    {
        Client c;
        c.start(opts(sock));
        CHECK(waitFor([&] { return c.status() == Status::Searching; }));
        c.setActivity("{\"type\":0,\"details\":\"queued while Discord was closed\"}");
        FakeDiscord d;
        CHECK(d.listenAt(sock));
        CHECK(waitFor([&] { return c.status() == Status::Connected; }));
        // handshake: op 0, v1, client_id
        CHECK(waitFor([&] { return d.handshakes() == 1; }));
        const std::vector<Frame> fr = d.frames();
        CHECK(!fr.empty() && fr[0].op == OpHandshake);
        CHECK(fr[0].payload == "{\"v\":1,\"client_id\":\"111222333444555666\"}");
        // the presence set while it was down arrives after connecting
        CHECK(waitFor([&] { return d.activities().size() == 1; }));
        std::vector<std::string> acts = d.activities();
        CHECK(validJson(acts[0]));
        CHECK(has(acts[0], "\"pid\":4242"));
        CHECK(has(acts[0], "queued while Discord was closed"));
        std::string nonce;
        CHECK(jsonFindString(acts[0], "nonce", nonce) && !nonce.empty());

        // 2. ping -> pong with the same payload
        d.sendFrame(OpPing, "hello");
        CHECK(waitFor([&] { return d.pongs() == 1; }));
        bool echoed = false;
        for (const Frame& f : d.frames()) if (f.op == OpPong && f.payload == "hello") echoed = true;
        CHECK(echoed);

        // 3. rate limit (3 per 1 s window here): a burst is coalesced, the newest one wins
        const size_t before = d.activities().size();   // 1
        for (int i = 1; i <= 6; i++) c.setActivity("{\"type\":0,\"details\":\"burst " + std::to_string(i) + "\"}");
        CHECK(waitFor([&] { return has(c.lastSentJson(), "burst 6"); }, 4.0));
        CHECK(waitFor([&] { const auto v = d.activities(); return !v.empty() && has(v.back(), "burst 6"); }, 4.0));   // the fake server may lag on a loaded machine
        acts = d.activities();
        CHECK(acts.size() - before <= 3);                       // never above the window's allowance
        CHECK(has(acts.back(), "burst 6"));                     // and it ends on the latest
        CHECK(acts.size() - before >= 1);

        // 4. a refused activity falls back to the plain form, once
        d.errorOnActivity = true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1100));   // let the window open
        const size_t b2 = d.activities().size();
        c.setActivity("{\"type\":0,\"details\":\"with art\",\"assets\":{\"large_image\":\"https://x/y.png\"}}",
                      "{\"type\":0,\"details\":\"with art\",\"assets\":{\"large_image\":\"jaguar\"}}");
        CHECK(waitFor([&] { return d.activities().size() >= b2 + 2; }, 4.0));
        acts = d.activities();
        CHECK(has(acts[b2], "https://x/y.png"));
        CHECK(has(acts[b2 + 1], "\"large_image\":\"jaguar\""));
        CHECK(c.errors() >= 1);
        std::this_thread::sleep_for(std::chrono::milliseconds(400));
        CHECK(d.activities().size() == b2 + 2);   // the fallback's own ERROR does not loop
        d.errorOnActivity = false;

        // 5. a dropped connection is re-made and the current presence is sent again
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        d.dropClient();
        CHECK(waitFor([&] { return d.handshakes() == 2; }, 5.0));
        CHECK(waitFor([&] { return c.status() == Status::Connected; }));
        CHECK(waitFor([&] { return d.activities().size() >= b2 + 3; }, 4.0));
        CHECK(has(d.activities().back(), "\"large_image\":\"jaguar\""));

        // 6. stop(): the presence is cleared (activity null) and stop returns quickly
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        const double t0 = nowSec();
        c.stop(250);
        CHECK(nowSec() - t0 < 1.0);
        CHECK(c.status() == Status::Stopped);
        CHECK(waitFor([&] { return has(d.activities().back(), "\"activity\":null"); }, 2.0));
        d.stop();
    }

    // 7. Invalid Client ID: closed with 4000, reported, and not retried.
    {
        FakeDiscord d;
        d.rejectClientId = true;
        CHECK(d.listenAt(sock));
        Client c;
        c.start(opts(sock));
        CHECK(waitFor([&] { return c.status() == Status::InvalidAppId; }));
        std::this_thread::sleep_for(std::chrono::milliseconds(700));
        CHECK(d.handshakes() == 1);   // no retry storm (0.2 s retry would have tried ~3 more times)
        c.stop(250);
        d.stop();
    }

    // 8. Nothing set, Discord up: stop() sends no clear (there is nothing to clear).
    {
        FakeDiscord d;
        CHECK(d.listenAt(sock));
        Client c;
        c.start(opts(sock));
        CHECK(waitFor([&] { return c.status() == Status::Connected; }));
        c.stop(250);
        CHECK(d.activities().empty());
        d.stop();
    }

    // 9. A Discord that dies while we write must not kill us (SIGPIPE).
    {
        FakeDiscord d;
        CHECK(d.listenAt(sock));
        Client c;
        c.start(opts(sock));
        CHECK(waitFor([&] { return c.status() == Status::Connected; }));
        d.stop();   // gone
        for (int i = 0; i < 3; i++) {
            c.setActivity("{\"type\":0,\"details\":\"into the void " + std::to_string(i) + "\"}");
            std::this_thread::sleep_for(std::chrono::milliseconds(150));
        }
        CHECK(waitFor([&] { return c.status() == Status::Searching; }, 3.0));
        c.stop(250);
    }

    ::rmdir(dir.c_str());
    std::printf("discord loopback: %d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}

#else
static int runLoopback() {
    std::printf("discord loopback: skipped on Windows (named pipes)\n");
    return 77;
}
#endif

int main(int argc, char** argv) {
    const std::string mode = argc > 1 ? argv[1] : "unit";
    if (mode == "unit") return runUnit();
    if (mode == "loopback") return runLoopback();
    std::fprintf(stderr, "usage: vj_discord_test unit|loopback\n");
    return 2;
}
