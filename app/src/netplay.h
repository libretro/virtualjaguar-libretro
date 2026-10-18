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
//              an ordinary saved option.  The core's host and port options take any
//              hostname / IPv4 address and any port 1-65535 (typed_options.h), so a typed
//              address and port are set straight on them.  The core's own
//              <system dir>/vj_netlink.txt route ("From file" host) is kept as a fallback
//              (setAddressViaOption(false)).
//   status     the core's private event channel (src/core/vj_frontend_ext.h): link UP / DOWN,
//              wire speed, the link stats every 5 s while traffic flows.  Without that
//              channel, the "[NETLINK] ..." log lines tapped in CoreHost::onLog say the same.
//   discovery  the core finds LAN hosts itself (UDP beacon, jlink_discover.c) and splices
//              them into the host option's value list with a second
//              SET_CORE_OPTIONS_V2; CoreHost re-captures it, we read it back.
// Nothing from the core's JLink* internals is called.
//
// Every method runs on the UI thread.
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

    // ---- control -----------------------------------------------------------
    // Applies `s` (host/join/loopback), or Off to disconnect.  Returns false with `err` when
    // the settings are unusable.  `restartKeys` (optional) receives the core options this
    // change touched that the core says only take effect when content is (re)loaded -- the
    // window offers a game restart for those.  None of the link options are like that today
    // (the core re-reads them every frame); the check keeps the window correct if one ever is.
    bool start(const NetSettings& s, std::string& err, std::vector<std::string>* restartKeys = nullptr);
    void stop();
    // Call on the UI thread, once per emulated frame or once per UI iteration.  It completes an endpoint change once
    // the core has read "disabled" for long enough, counted in emulated frames.
    void afterFrame();

    // ---- state -------------------------------------------------------------
    const NetSettings& settings() const { return want_; }
    bool configured() const { return want_.role != NetRole::Off; }
    LinkState link() const { return state_; }
    // The device the core reported on its last "link UP" ("jaglink" / "voicemodem" / "").
    const std::string& linkDevice() const { return upDevice_; }
    const LinkStats& stats() const { return stats_; }
    // LINK_UP events from the core's event channel that moved the link up (0 on a core without the channel).
    unsigned linkUpEvents() const { return upEvents_; }
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

    // ---- what the core tells us (CoreHost::onLog / CoreHost::onEvent) --------------
    // The core's private event channel is authoritative when the core said HELLO (the app's CoreHost
    // answers it); the log tap then only handles the lines that have no event (open errors, discovery
    // trouble).  A core that never said HELLO is read the old way, entirely from its log text.
    void onCoreLog(int level, const char* text);
    void onCoreEvent(const vj_event& e);
    // The app's tap (CoreHost::onUiEvent, on the UI thread): drops what the core said before the last start() /
    // stop() / bounce (its epoch), then reads it like onCoreLog / onCoreEvent.
    void onUiEvent(const CoreEvent& e);
    // How a typed address reaches the core.  true (default): the host option takes it.  false: the core's
    // own "From file" route, <system dir>/vj_netlink.txt (the fallback for a core whose host option is a
    // closed list).  That file is ONE file per system directory, shared by every copy of the app on a data
    // folder (instance.h), so two copies joining different hosts through it would overwrite each other:
    // the option route, which is per process, is the default for all instances.
    void setAddressViaOption(bool on) { viaOption_ = on; }

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

    CoreHost& host_;
    std::string systemDir_;
    NetSettings want_;
    NetSettings applied_;          // what the core was last told (Off when nothing)
    bool appliedValid_ = false;
    // Changing host/port without changing the mode does not re-open the link in the core
    // (UARTSetLinkMode only acts on a mode change), so such a change bounces through
    // "disabled" for a couple of frames first.
    bool bouncing_ = false;
    uint64_t bounceFrom_ = 0;   // CoreHost::frameCount() when the bounce began
    unsigned bounceNeed_ = 2;   // emulated frames "disabled" must be read for
    uint32_t minEpoch_ = 0;     // core lines and events from before this epoch are stale
    NetSettings bounceTarget_;

    LinkState state_ = LinkState::Off;
    std::string upDevice_;
    std::string peerText_;         // "192.168.1.5:42171" from the core's mode= line
    LinkStats stats_;
    std::chrono::steady_clock::time_point statsAt_{};
    bool haveStats_ = false;
    unsigned speedup_ = 1;
    std::string openError_, discoveryError_, droppedHost_;
    unsigned upEvents_ = 0;
    bool viaOption_ = true;        // typed host address goes in the host option; false = vj_netlink.txt route
};

}  // namespace vj
