// Netplay tests (the link-cable window's engine; see netplay.h).  No window, no GL, no internet.
//
//   vj_netplay_test unit ROM            parsers, then the REAL core in this process: the settings the
//                                       window makes reach the core (loopback / host / join, device,
//                                       a port outside the option's list, changing the endpoint while
//                                       connected), and the log tap reports link UP / DOWN.
//   vj_netplay_test pair EXE ROM [voicemodem]
//                                       two app processes on localhost, one Host and one Join through
//                                       the command-line twin of the window; both must report the link UP.
//                                       Does not use UDP broadcast: the client is given the address.
//   vj_netplay_test control EXE ROM     the same with nobody to talk to: must FAIL (so the check can fail).
//   vj_netplay_test discovery EXE ROM   a host and a client that must FIND it by LAN discovery and join
//                                       it.  Exit 77 (ctest: skipped) when this machine does not deliver
//                                       UDP broadcast to itself -- CI runners often do not.
//
// Ports are picked at run time from 20000-29999 (below every OS's ephemeral range, and probed free):
// a hard-coded port, or an ephemeral one, collides with a concurrent run.
#include "core_host.h"
#include "netplay.h"
#include "paths.h"

extern "C" {
#include "jlink_discover.h"   // the core's beacon codec (JLinkDiscEncode): to play a LAN host without broadcast
}

#include <SDL3/SDL.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <vector>

#ifdef _WIN32
#  include <winsock2.h>
#  include <ws2tcpip.h>
typedef SOCKET sock_t;
static const sock_t kBadSock = INVALID_SOCKET;
static void closeSock(sock_t s) { closesocket(s); }
#else
#  include <arpa/inet.h>
#  include <fcntl.h>
#  include <netinet/in.h>
#  include <sys/socket.h>
#  include <unistd.h>
typedef int sock_t;
static const sock_t kBadSock = -1;
static void closeSock(sock_t s) { close(s); }
#endif

using namespace vj;
namespace fs = std::filesystem;

static int g_fail = 0, g_checks = 0;

#define CHECK(cond)                                                                    \
    do {                                                                               \
        g_checks++;                                                                    \
        if (!(cond)) {                                                                 \
            g_fail++;                                                                  \
            std::fprintf(stderr, "FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);       \
        }                                                                              \
    } while (0)

#define CHECK_EQ(a, b)                                                                 \
    do {                                                                               \
        g_checks++;                                                                    \
        std::string va_ = (a), vb_ = (b);                                              \
        if (va_ != vb_) {                                                              \
            g_fail++;                                                                  \
            std::fprintf(stderr, "FAIL %s:%d  %s\n   got      '%s'\n   expected '%s'\n", __FILE__, __LINE__, #a, \
                         va_.c_str(), vb_.c_str());                                    \
        }                                                                              \
    } while (0)

// ---- sockets ----------------------------------------------------------------------------------
static void netInit() {
#ifdef _WIN32
    WSADATA w;
    WSAStartup(MAKEWORD(2, 2), &w);
#endif
}

static void setNonBlocking(sock_t s) {
#ifdef _WIN32
    u_long nb = 1;
    ioctlsocket(s, FIONBIO, &nb);
#else
    fcntl(s, F_SETFL, fcntl(s, F_GETFL, 0) | O_NONBLOCK);
#endif
}

static sockaddr_in loopbackAddr(int port) {
    sockaddr_in sa;
    std::memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET;
    sa.sin_port = htons((unsigned short)port);
    sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    return sa;
}

static bool bindsOn(int type, int port) {
    sock_t s = socket(AF_INET, type, 0);
    if (s == kBadSock) return false;
    sockaddr_in sa = loopbackAddr(port);
    sa.sin_addr.s_addr = htonl(INADDR_ANY);
    bool ok = bind(s, (sockaddr*)&sa, sizeof sa) == 0;
    closeSock(s);
    return ok;
}

// A port that is free for TCP and UDP right now.  20000-29999: below the ephemeral ranges.
static int pickFreePort(int avoid = 0) {
    unsigned long long seed = SDL_GetPerformanceCounter();
    for (int i = 0; i < 200; i++) {
        int port = 20000 + (int)((seed + (unsigned long long)i * 7919ull) % 10000ull);
        if (port == avoid) continue;
        if (bindsOn(SOCK_STREAM, port) && bindsOn(SOCK_DGRAM, port)) return port;
    }
    return 0;
}

static sock_t listenOn(int port) {
    sock_t s = socket(AF_INET, SOCK_STREAM, 0);
    if (s == kBadSock) return kBadSock;
    sockaddr_in sa = loopbackAddr(port);
    if (bind(s, (sockaddr*)&sa, sizeof sa) != 0 || listen(s, 4) != 0) {
        closeSock(s);
        return kBadSock;
    }
    setNonBlocking(s);
    return s;
}

static sock_t connectTo(int port) {
    sock_t s = socket(AF_INET, SOCK_STREAM, 0);
    if (s == kBadSock) return kBadSock;
    sockaddr_in sa = loopbackAddr(port);
    if (connect(s, (sockaddr*)&sa, sizeof sa) != 0) {
        closeSock(s);
        return kBadSock;
    }
    return s;
}

// ---- helpers ----------------------------------------------------------------------------------
static std::string optionOf(CoreHost& h, const char* key) {
    for (const OptionDef& d : h.optionDefs())
        if (d.key == key) return h.optionValue(d);
    return "(no such option)";
}

static void frames(CoreHost& h, Netplay& np, int n) {
    for (int i = 0; i < n; i++) {
        h.runFrame();
        np.afterFrame();
    }
}

// Run until `pred` holds, a frame at a time, for at most `maxMs` of wall time (TCP needs real time).
static bool runUntil(CoreHost& h, Netplay& np, const std::function<bool()>& pred, int maxMs = 4000) {
    const auto t0 = std::chrono::steady_clock::now();
    while (!pred()) {
        if (std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count() > maxMs)
            return false;
        h.runFrame();
        np.afterFrame();
        SDL_Delay(1);
    }
    return true;
}

static void setEnv(const char* k, const std::string& v) {
#ifdef _WIN32
    _putenv_s(k, v.c_str());
#else
    setenv(k, v.c_str(), 1);
#endif
}

// ---- pure parsers -----------------------------------------------------------------------------
static void testParsers() {
    // The exact format jlink.c's JLinkStatsPoll() writes (libretro.c prefixes "[NETLINK] ").
    const std::string line =
        "[NETLINK] link stats 5s: hops/s=18 hop=1.2ms wait/hop=0.4ms slipped=11% (10/90) timeouts=1 "
        "budget_cut=2 budget_us=9000 (emu=4200us margin_adj=+0us) wait/frame=500us idle_wait=3/7";
    LinkStats s;
    CHECK(Netplay::parseStatsLine(line, s));
    CHECK(s.valid);
    CHECK(s.windowSec == 5);
    CHECK(s.hopsPerSec == 18);
    CHECK(s.hopMs > 1.19 && s.hopMs < 1.21);
    CHECK(s.waitPerHopMs > 0.39 && s.waitPerHopMs < 0.41);
    CHECK(s.slippedPct == 11 && s.slipped == 10 && s.hops == 90);
    CHECK(s.timeouts == 1 && s.budgetCut == 2 && s.budgetUs == 9000);
    CHECK(s.idleHits == 3 && s.idleWaits == 7);
    CHECK(s.raw.rfind("link stats 5s", 0) == 0);

    LinkStats bad;
    CHECK(!Netplay::parseStatsLine("[NETLINK] link UP (tcp_server, device=jaglink)", bad));
    CHECK(!Netplay::parseStatsLine("link stats 5s: nothing useful", bad));
    CHECK(!bad.valid);

    // The host picker: presets stay, discovered peers become rows; a differing device is tagged.
    OptionDef host;
    host.key = "virtualjaguar_netlink_host";
    host.values = {{"127.0.0.1", "127.0.0.1 (localhost)"},
                   {"192.168.1.42", "studio - 192.168.1.42"},
                   {"10.0.0.7", "10.0.0.7 - 10.0.0.7 (Voice Modem)"},
                   {"127.0.0.1", "devbox - 127.0.0.1"},   // a peer on this very machine
                   {"jaghub.local", "jaghub.local (host machine named 'jaghub' on the LAN)"},
                   {"vj_netlink.txt", "From file"}};
    OptionDef other;
    other.key = "virtualjaguar_netlink_port";
    other.values = {{"42171", ""}};
    std::vector<DiscoveredHost> found = Netplay::hostsFromOptions({other, host});
    CHECK(found.size() == 3);
    if (found.size() == 3) {
        CHECK_EQ(found[0].address, "192.168.1.42");
        CHECK_EQ(found[0].name, "studio");
        CHECK_EQ(found[0].device, "");
        CHECK_EQ(found[1].address, "10.0.0.7");
        CHECK_EQ(found[1].device, "Voice Modem");
        CHECK_EQ(found[1].name, "");
        CHECK_EQ(found[2].address, "127.0.0.1");
        CHECK_EQ(found[2].name, "devbox");
    }
    CHECK(Netplay::hostsFromOptions({other}).empty());

    OptionDef r;
    r.info = "Restart required: applied when content is loaded. Something.";
    CHECK(Netplay::optionNeedsRestart(r));
    OptionDef live;
    live.info = "Takes effect immediately.";
    live.desc = "Network Link";
    CHECK(!Netplay::optionNeedsRestart(live));
    OptionDef suffix;
    suffix.desc = "Cart BIOS Type (Restart)";
    CHECK(Netplay::optionNeedsRestart(suffix));

    CHECK(Netplay::portIsOptionValue(42171) && Netplay::portIsOptionValue(42174));
    CHECK(!Netplay::portIsOptionValue(42170) && !Netplay::portIsOptionValue(42175) && !Netplay::portIsOptionValue(23456));

    // Local addresses: IPv4 dotted quads, never loopback or link-local.
    std::vector<LanAddress> lan = Netplay::lanAddresses();
    for (const LanAddress& a : lan) {
        unsigned p[4] = {0, 0, 0, 0};
        CHECK(std::sscanf(a.ip.c_str(), "%u.%u.%u.%u", &p[0], &p[1], &p[2], &p[3]) == 4);
        CHECK(p[0] != 127 && !(p[0] == 169 && p[1] == 254));
    }
    std::printf("netplay: %zu LAN address(es) on this machine\n", lan.size());
}

// ---- the real core ------------------------------------------------------------------------------
static void testCore(const std::string& rom) {
    std::error_code ec;
    fs::path tmp = fs::temp_directory_path(ec) / ("vj-netplay-test-" + std::to_string((long long)std::time(nullptr)));
    fs::create_directories(tmp / "saves", ec);
    fs::create_directories(tmp / "system", ec);
    const std::string sys = (tmp / "system").string() + "/", sav = (tmp / "saves").string() + "/";

    // Keep the core's LAN discovery (and wire-speed negotiation) on a port of ours.
    int discPort = pickFreePort();
    CHECK(discPort != 0);
    setEnv("VJ_DISC_PORT", std::to_string(discPort));

    CoreHost& host = CoreHost::instance();
    host.setDirectories(sys, sav);
    host.setLogVerbose(std::getenv("VJ_TEST_VERBOSE") != nullptr);   // the core's INFO lines, for debugging
    host.inputPoll = []() {};
    Netplay np(host);
    np.setSystemDir(sys);
    host.onLog = [&np](int level, const char* text) { np.onCoreLog(level, text); };

    std::string err;
    if (!host.loadContent(rom, err)) {
        std::fprintf(stderr, "FAIL load failed: %s\n", err.c_str());
        g_fail++;
        return;
    }
    frames(host, np, 5);
    CHECK(np.link() == LinkState::Off);
    CHECK_EQ(optionOf(host, "virtualjaguar_netlink"), "auto");   // the default: nothing configured, nothing listening

    // ---- loopback: the cheapest proof that the log tap sees the core's link UP ------------------
    {
        NetSettings s;
        s.role = NetRole::Loopback;
        std::vector<std::string> restart;
        CHECK(np.start(s, err, &restart));
        CHECK(restart.empty());   // the link options are live: nothing to offer a restart for
        CHECK_EQ(optionOf(host, "virtualjaguar_netlink"), "loopback");
        CHECK(runUntil(host, np, [&] { return np.link() == LinkState::Up; }));
        CHECK_EQ(np.linkDevice(), "jaglink");
    }

    // ---- host on a port the option does not offer (VJ_NETLINK_PORT path) -------------------------
    const int hostPort = pickFreePort(discPort);
    CHECK(hostPort != 0 && !Netplay::portIsOptionValue(hostPort));
    {
        NetSettings s;
        s.role = NetRole::Host;
        s.port = hostPort;
        s.device = NetDevice::VoiceModem;
        CHECK(np.start(s, err));
        frames(host, np, 5);
        CHECK_EQ(optionOf(host, "virtualjaguar_netlink"), "tcp_server");
        CHECK_EQ(optionOf(host, "virtualjaguar_uart_device"), "voicemodem");
        CHECK(np.link() == LinkState::Waiting);
        // ...and it really listens there: a partner connecting brings the link up as a Voice Modem.
        sock_t partner = connectTo(hostPort);
        CHECK(partner != kBadSock);
        CHECK(runUntil(host, np, [&] { return np.link() == LinkState::Up; }));
        CHECK_EQ(np.linkDevice(), "voicemodem");
        if (partner != kBadSock) closeSock(partner);
        CHECK(runUntil(host, np, [&] { return np.link() == LinkState::Lost; }));
        CHECK(np.statusText().find("disconnected") != std::string::npos);
    }

    // ---- moving the host to another port while hosting: bounces through "disabled" ---------------
    {
        const int port2 = pickFreePort(discPort);
        NetSettings s = np.settings();
        s.port = port2;
        s.device = NetDevice::JagLink;
        CHECK(np.start(s, err));
        CHECK_EQ(optionOf(host, "virtualjaguar_netlink"), "disabled");   // the first step
        frames(host, np, 3);
        CHECK_EQ(optionOf(host, "virtualjaguar_netlink"), "tcp_server");
        frames(host, np, 3);
        sock_t partner = connectTo(port2);
        CHECK(partner != kBadSock);
        CHECK(runUntil(host, np, [&] { return np.link() == LinkState::Up; }));
        CHECK_EQ(np.linkDevice(), "jaglink");
        if (partner != kBadSock) closeSock(partner);
    }

    // ---- join: the address goes through the core's vj_netlink.txt route, the port through the env ---
    {
        const int port3 = pickFreePort(discPort);
        sock_t srv = listenOn(port3);
        CHECK(srv != kBadSock);
        NetSettings s;
        s.role = NetRole::Join;
        s.address = "127.0.0.1";
        s.port = port3;
        CHECK(np.start(s, err));
        CHECK_EQ(optionOf(host, "virtualjaguar_netlink"), "disabled");   // a switch of mode bounces too
        frames(host, np, 3);
        CHECK_EQ(optionOf(host, "virtualjaguar_netlink"), "tcp_client");
        CHECK_EQ(optionOf(host, "virtualjaguar_netlink_host"), "vj_netlink.txt");
        {
            std::ifstream f(sys + "vj_netlink.txt");
            std::string line;
            std::getline(f, line);
            CHECK_EQ(line, "127.0.0.1");
        }
        sock_t peer = kBadSock;
        bool up = runUntil(host, np, [&] {
            if (peer == kBadSock) peer = accept(srv, nullptr, nullptr);
            return np.link() == LinkState::Up;
        });
        CHECK(up);
        if (!up) std::fprintf(stderr, "  join status: %s\n", np.statusText().c_str());
        CHECK(peer != kBadSock);
        CHECK(np.statusText().find("127.0.0.1") != std::string::npos);
        if (peer != kBadSock) closeSock(peer);
        closeSock(srv);
    }

    // ---- looking for hosts: the core's discovery list reaches Netplay::discovered() ---------------------
    // No broadcast needed: the core listens on a UDP port, so a beacon sent straight to it is a host
    // "seen on the LAN".  This is the real SET_CORE_OPTIONS_V2 re-push path, end to end.
    {
        NetSettings s;
        s.role = NetRole::Join;   // no address: just look
        s.port = pickFreePort(discPort);
        CHECK(np.start(s, err));
        frames(host, np, 5);
        CHECK(np.discovered().empty());
        sock_t udp = socket(AF_INET, SOCK_DGRAM, 0);
        CHECK(udp != kBadSock);
        uint8_t pkt[JLINK_DISC_PKT_LEN];
        size_t n = JLinkDiscEncode(pkt, sizeof pkt, JLINK_DISC_DEV_VOICEMODEM, 42171, "teststation");
        CHECK(n == JLINK_DISC_PKT_LEN);
        sockaddr_in to = loopbackAddr(discPort);
        unsigned pushes = host.optionPushes();
        auto lastSend = std::chrono::steady_clock::time_point();
        bool seen = runUntil(host, np, [&] {
            auto now = std::chrono::steady_clock::now();
            if (udp != kBadSock && now - lastSend > std::chrono::milliseconds(200)) {
                sendto(udp, (const char*)pkt, (int)n, 0, (sockaddr*)&to, sizeof to);
                lastSend = now;
            }
            return !np.discovered().empty();
        }, 6000);
        CHECK(seen);
        CHECK(host.optionPushes() > pushes);   // the core re-pushed its options
        std::vector<DiscoveredHost> found = np.discovered();
        CHECK(found.size() == 1);
        if (found.size() == 1) {
            CHECK_EQ(found[0].address, "127.0.0.1");
            CHECK_EQ(found[0].name, "teststation");
            CHECK_EQ(found[0].device, "Voice Modem");   // we are a JagLink: the core tags the mismatch
        }
        if (udp != kBadSock) closeSock(udp);
    }

    // ---- bad input is refused, not applied -----------------------------------------------------------
    {
        NetSettings s;
        s.role = NetRole::Join;
        s.address = "10.0.0.1:42171";   // a port belongs in the Port box
        CHECK(!np.start(s, err) && !err.empty());
        s.address = "two words";
        CHECK(!np.start(s, err));
        s.address = "10.0.0.1";
        s.port = 0;
        CHECK(!np.start(s, err));
    }

    // ---- disconnect hands the options back to what the user had --------------------------------------
    np.stop();
    CHECK(np.link() == LinkState::Off && !np.configured());
    frames(host, np, 5);
    CHECK_EQ(optionOf(host, "virtualjaguar_netlink"), "auto");
    CHECK(std::getenv("VJ_NETLINK_PORT") == nullptr || std::getenv("VJ_NETLINK_PORT")[0] == 0);

    host.onLog = nullptr;
    host.unloadContent();
    fs::remove_all(tmp, ec);   // our own temp dir only
}

// ---- two processes ------------------------------------------------------------------------------
struct Child {
    SDL_Process* p = nullptr;
    int code = -1;
    bool done = false;
};

static Child spawn(const std::vector<std::string>& args) {
    std::vector<const char*> argv;
    for (const std::string& a : args) argv.push_back(a.c_str());
    argv.push_back(nullptr);
    Child c;
    c.p = SDL_CreateProcess(argv.data(), false);   // stdout/stderr are inherited: they land in the ctest log
    if (!c.p) std::fprintf(stderr, "FAIL could not start %s: %s\n", args[0].c_str(), SDL_GetError());
    return c;
}

static bool poll(Child& c) {
    if (!c.p || c.done) return true;
    int code = 0;
    if (SDL_WaitProcess(c.p, false, &code)) {
        c.done = true;
        c.code = code;
    }
    return c.done;
}

static void reap(Child& c) {
    if (!c.p) return;
    if (!c.done) {
        SDL_KillProcess(c.p, true);
        int code = 0;
        SDL_WaitProcess(c.p, true, &code);
        c.code = -2;
        c.done = true;
    }
    SDL_DestroyProcess(c.p);
    c.p = nullptr;
}

static std::vector<std::string> appArgs(const std::string& exe, const std::string& rom, const std::vector<std::string>& extra) {
    std::vector<std::string> a = {exe, "--headless", "--frames", "300"};
    a.insert(a.end(), extra.begin(), extra.end());
    a.push_back(rom);
    return a;
}

// Waits for both; false on timeout.
static bool waitBoth(Child& a, Child& b, int seconds) {
    const auto t0 = std::chrono::steady_clock::now();
    while (!(poll(a) && poll(b))) {
        if (std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() > seconds) return false;
        SDL_Delay(50);
    }
    return true;
}

static int runPair(const std::string& exe, const std::string& rom, const std::string& device) {
    const int port = pickFreePort();
    const int disc = pickFreePort(port);
    if (!port || !disc) {
        std::fprintf(stderr, "FAIL no free port\n");
        return 1;
    }
    setEnv("VJ_DISC_PORT", std::to_string(disc));   // inherited by both children
    const std::string p = std::to_string(port);
    Child host = spawn(appArgs(exe, rom, {"--netplay", "host", "--netplay-port", p, "--netplay-device", device,
                                          "--netplay-expect-link", "30"}));
    SDL_Delay(300);
    Child join = spawn(appArgs(exe, rom, {"--netplay", "join", "--netplay-addr", "127.0.0.1", "--netplay-port", p,
                                          "--netplay-device", device, "--netplay-expect-link", "30"}));
    bool finished = waitBoth(host, join, 90);
    reap(host);
    reap(join);
    CHECK(finished);
    CHECK(host.code == 0);
    CHECK(join.code == 0);
    return g_fail ? 1 : 0;
}

static int runControl(const std::string& exe, const std::string& rom) {
    const int port = pickFreePort();
    const int disc = pickFreePort(port);
    setEnv("VJ_DISC_PORT", std::to_string(disc));
    const std::string p = std::to_string(port);
    // Nobody is listening on the port, and nobody joins the host: neither may claim a link.
    Child join = spawn(appArgs(exe, rom, {"--netplay", "join", "--netplay-addr", "127.0.0.1", "--netplay-port", p,
                                          "--netplay-expect-link", "3"}));
    Child host = spawn(appArgs(exe, rom, {"--netplay", "host", "--netplay-port", std::to_string(pickFreePort(port)),
                                          "--netplay-expect-link", "3"}));
    bool finished = waitBoth(host, join, 60);
    reap(host);
    reap(join);
    CHECK(finished);
    CHECK(join.code != 0 && join.code != -2);   // exited by itself, with a failure
    CHECK(host.code != 0 && host.code != -2);
    return g_fail ? 1 : 0;
}

static int runDiscovery(const std::string& exe, const std::string& rom) {
    const int port = pickFreePort();
    const int disc = pickFreePort(port);
    setEnv("VJ_DISC_PORT", std::to_string(disc));
    const std::string p = std::to_string(port);
    Child host = spawn(appArgs(exe, rom, {"--netplay", "host", "--netplay-port", p, "--netplay-expect-link", "40"}));
    SDL_Delay(300);
    Child join = spawn(appArgs(exe, rom, {"--netplay", "join-discovered", "--netplay-port", p, "--netplay-expect-link",
                                          "30", "--netplay-expect-discovery", "10"}));
    const auto t0 = std::chrono::steady_clock::now();
    while (!poll(join)) {
        if (std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() > 90) break;
        SDL_Delay(50);
    }
    const int jc = join.done ? join.code : -2;
    if (jc == 77) {
        reap(host);
        reap(join);
        std::printf("netplay discovery: SKIP (no host was discovered; UDP broadcast not delivered here)\n");
        return 77;
    }
    // The host leaves once its partner has been seen.
    const auto t1 = std::chrono::steady_clock::now();
    while (!poll(host)) {
        if (std::chrono::duration<double>(std::chrono::steady_clock::now() - t1).count() > 60) break;
        SDL_Delay(50);
    }
    reap(host);
    reap(join);
    CHECK(jc == 0);
    CHECK(host.code == 0);
    return g_fail ? 1 : 0;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: vj_netplay_test unit ROM | pair EXE ROM [voicemodem] | control EXE ROM | discovery EXE ROM\n");
        return 2;
    }
    netInit();
    const std::string mode = argv[1];
    int rc = 0;
    if (mode == "unit" && argc >= 3) {
        testParsers();
        testCore(argv[2]);
        rc = g_fail ? 1 : 0;
    } else if (mode == "pair" && argc >= 4) {
        rc = runPair(argv[2], argv[3], argc >= 5 ? argv[4] : "jaglink");
    } else if (mode == "control" && argc >= 4) {
        rc = runControl(argv[2], argv[3]);
    } else if (mode == "discovery" && argc >= 4) {
        rc = runDiscovery(argv[2], argv[3]);
    } else {
        std::fprintf(stderr, "bad arguments\n");
        return 2;
    }
    std::printf("netplay_test %s: %d checks, %d failed\n", mode.c_str(), g_checks, g_fail);
    return rc;
}
