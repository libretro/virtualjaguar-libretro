// Netplay (link-cable play over the network): a friendly front end on the core's
// EXISTING link networking (docs/netlink-design.md, docs/netlink-user-guide.md).
// This is NOT rollback netplay (epic #864): the core carries the Jaguar's serial
// link (JagLink / CatBox / Voice Modem) over a TCP connection between two
// emulators, and this class only configures that through the core options and
// reports what the core says about it.
//
// How it talks to the core -- everything through the libretro boundary:
//   settings   core options: virtualjaguar_netlink (mode), _netlink_host, _netlink_port,
//              _uart_device.  Mode, host and port are session overrides (never saved:
//              relaunching the app must not start listening on its own); the device is
//              an ordinary saved option.  An arbitrary address cannot be a core option
//              value (the option is an enumeration), so it goes through the core's own
//              <system dir>/vj_netlink.txt route ("From file" host).  A port outside the
//              four the option offers goes through VJ_NETLINK_PORT (tests, power users).
//   status     the core's "[NETLINK] ..." log lines, tapped in CoreHost::onLog: link
//              UP/DOWN/waiting, and the "link stats" line every 5 s while traffic flows.
//   discovery  the core finds LAN hosts itself (UDP beacon, jlink_discover.c) and splices
//              them into the host option's value list with a second
//              SET_CORE_OPTIONS_V2; CoreHost re-captures it, we read it back.
// Nothing from the core's JLink* internals is called.
//
// Single-threaded: every method runs on the thread that drives CoreHost.
#pragma once

#include <chrono>
#include <string>
#include <vector>

#include "core_host.h"

namespace vj {

enum class NetRole { Off, Host, Join, Loopback };
enum class NetDevice { JagLink, VoiceModem };
// Off = nothing configured.  Waiting = configured, no partner (yet).  Up = a partner
// is connected.  Lost = it was up and the partner went away.
enum class LinkState { Off, Waiting, Up, Lost };

struct NetSettings {
    NetRole role = NetRole::Off;
    NetDevice device = NetDevice::JagLink;
    int port = 42171;
    // Join only.  Empty = "just look for hosts on the LAN, do not dial anything yet".
    std::string address;
};

// The "[NETLINK] link stats" line, parsed.  Fields mirror jlink.c's JLinkStatsPoll.
struct LinkStats {
    bool valid = false;
    unsigned windowSec = 0;
    unsigned hopsPerSec = 0;     // link exchanges per second
    double hopMs = 0;            // mean send -> reply time (network round trip + partner turnaround)
    double waitPerHopMs = 0;     // mean time this side spent blocked waiting for the reply
    unsigned slippedPct = 0;     // replies that missed the wait and cost a frame edge
    unsigned slipped = 0, hops = 0;
    unsigned timeouts = 0;       // waits that gave up (partner silent / spike)
    unsigned budgetCut = 0;
    int budgetUs = 0;
    unsigned idleHits = 0, idleWaits = 0;
    std::string raw;             // the whole line, for the "details" view
};

struct DiscoveredHost {
    std::string address;         // dotted quad, what Join dials
    std::string name;            // the host machine's name, "" if it sent none
    std::string device;          // "" = same device as ours, else "JagLink" / "Voice Modem"
    std::string label;           // the core's picker label
};

struct LanAddress {
    std::string iface;           // "en0", "Ethernet"
    std::string ip;              // dotted quad
};

// Pre-load/CLI description of what to do (the headless twin of the window).
struct NetplayCli {
    std::string role;            // "" = unused; host | join | join-discovered | browse | loopback
    std::string address;         // join
    std::string device;          // jaglink | voicemodem
    int port = 0;                // 0 = default
    double expectLinkSec = 0;    // headless: run until the link is UP (FAIL after this long)
    double expectDiscoverySec = 0;   // headless: join-discovered / browse: SKIP (77) if no host shows up
};

class Netplay {
public:
    explicit Netplay(CoreHost& host);

    // Where the "From file" address is written: the same system directory the core is told about.
    void setSystemDir(const std::string& dir) { systemDir_ = dir; }
    // A second copy of the app on the same data folder (instance.h) shares this system directory
    // with the first, and the core's "From file" address (<system>/vj_netlink.txt) is ONE file: two
    // copies joining different hosts would overwrite each other's.  Such a copy hands the core the
    // address through VJ_NETLINK_HOST instead -- an environment variable is per process.
    void setAddressViaEnv(bool on) { addressViaEnv_ = on; }

    // ---- control -----------------------------------------------------------
    // Applies `s` (host/join/loopback), or Off to disconnect.  Returns false with `err` when
    // the settings are unusable.  `restartKeys` (optional) receives the core options this
    // change touched that the core says only take effect when content is (re)loaded -- the
    // window offers a game restart for those.  None of the link options are like that today
    // (the core re-reads them every frame); the check keeps the window correct if one ever is.
    bool start(const NetSettings& s, std::string& err, std::vector<std::string>* restartKeys = nullptr);
    void stop();
    // Call once per emulated frame (after CoreHost::runFrame).
    void afterFrame();

    // ---- state -------------------------------------------------------------
    const NetSettings& settings() const { return want_; }
    bool configured() const { return want_.role != NetRole::Off; }
    LinkState link() const { return state_; }
    // The device the core reported on its last "link UP" ("jaglink" / "voicemodem" / "").
    const std::string& linkDevice() const { return upDevice_; }
    const LinkStats& stats() const { return stats_; }
    // Seconds since the last stats line; negative when there has not been one.
    double statsAgeSec() const;
    // One plain-language line for the status banner.
    std::string statusText() const;
    // Extra facts for the window: a failed open, a discovery socket that could not bind, a
    // dropped host, an environment override.  Empty when nothing needs saying.
    std::vector<std::string> notices() const;
    unsigned wireSpeedup() const { return speedup_; }
    // Hosts the core's LAN discovery currently sees (only populated while content is running
    // in Join or Host mode).
    std::vector<DiscoveredHost> discovered() const;

    // ---- log tap (CoreHost::onLog) ------------------------------------------
    void onCoreLog(int level, const char* text);

    // ---- pure helpers (unit tested) ------------------------------------------
    // `line` is the text after "[NETLINK] ", or the whole logged line.
    static bool parseStatsLine(const std::string& line, LinkStats& out);
    static std::vector<DiscoveredHost> hostsFromOptions(const std::vector<OptionDef>& defs);
    static bool optionNeedsRestart(const OptionDef& d);
    static const char* modeValue(NetRole r);              // the virtualjaguar_netlink value
    static const char* deviceValue(NetDevice d);          // the virtualjaguar_uart_device value
    static bool portIsOptionValue(int port);              // 42171..42174
    // IPv4 addresses other machines can reach this one on (no loopback, no link-local),
    // private ranges first.  Empty when the machine has no network.
    static std::vector<LanAddress> lanAddresses();
    static std::string machineName();
    // True and the variable name when the environment would silently override the window.
    static std::string envOverride();

private:
    bool applyNow(const NetSettings& s, std::string& err, std::vector<std::string>* restartKeys);
    void setOverride(const char* key, const std::string& value, std::vector<std::string>* restartKeys);
    void clearAll();
    bool writeAddressFile(const std::string& addr, std::string& err);
    void setPortEnv(int port, bool on);
    void setHostEnv(const std::string& addr);   // "" = remove (only if we set it)

    CoreHost& host_;
    std::string systemDir_;
    NetSettings want_;
    NetSettings applied_;          // what the core was last told (Off when nothing)
    bool appliedValid_ = false;
    // Changing host/port without changing the mode does not re-open the link in the core
    // (UARTSetLinkMode only acts on a mode change), so such a change bounces through
    // "disabled" for a couple of frames first.
    bool bouncing_ = false;
    int bounceFrames_ = 0;
    NetSettings bounceTarget_;

    LinkState state_ = LinkState::Off;
    std::string upDevice_;
    std::string peerText_;         // "192.168.1.5:42171" from the core's mode= line
    LinkStats stats_;
    std::chrono::steady_clock::time_point statsAt_{};
    bool haveStats_ = false;
    unsigned speedup_ = 1;
    std::string openError_, discoveryError_, droppedHost_;
    bool portEnvSet_ = false;
    bool hostEnvSet_ = false;
    bool addressViaEnv_ = false;
};

}  // namespace vj
