#include "netplay.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "paths.h"
#include "typed_options.h"

namespace vj {

namespace {

const char* const kModeKey = "virtualjaguar_netlink";
const char* const kHostKey = "virtualjaguar_netlink_host";
const char* const kPortKey = "virtualjaguar_netlink_port";
const char* const kDeviceKey = "virtualjaguar_uart_device";
const char* const kAddressFile = "vj_netlink.txt";   // the core's own "From file" route

// The values the core's host picker always carries; everything else in it is a discovered peer.
bool isPresetHost(const std::string& v) {
    return v == "127.0.0.1" || v == "jaghub.local" || v == kAddressFile;
}

// The number after `key` in `line`, or false.
bool numAfter(const std::string& line, const char* key, double& v) {
    size_t p = line.find(key);
    if (p == std::string::npos) return false;
    p += std::strlen(key);
    char* end = nullptr;
    v = std::strtod(line.c_str() + p, &end);
    return end != line.c_str() + p;
}

// Is this value-list row a discovered peer?  See Netplay::hostsFromOptions.
bool peerLabel(const OptionValue& v) {
    std::string l = v.label;
    const char* tags[] = {" (Voice Modem)", " (JagLink)"};
    for (const char* t : tags) {
        size_t n = std::strlen(t);
        if (l.size() > n && l.compare(l.size() - n, n, t) == 0) {
            l.resize(l.size() - n);
            break;
        }
    }
    std::string suffix = " - " + v.value;
    return l.size() >= suffix.size() && l.compare(l.size() - suffix.size(), suffix.size(), suffix) == 0;
}

bool contains(const std::string& s, const char* sub) { return s.find(sub) != std::string::npos; }

}  // namespace

Netplay::Netplay(CoreHost& host) : host_(host) {}

// ---------------------------------------------------------------------------------
// pure helpers
// ---------------------------------------------------------------------------------
const char* Netplay::modeValue(NetRole r) {
    switch (r) {
    case NetRole::Host: return "tcp_server";
    case NetRole::Join: return "tcp_client";
    case NetRole::Loopback: return "loopback";
    case NetRole::Off: break;
    }
    return "disabled";
}

const char* Netplay::deviceValue(NetDevice d) { return d == NetDevice::VoiceModem ? "voicemodem" : "jaglink"; }

bool Netplay::portIsOptionValue(int port) { return port >= 42171 && port <= 42174; }

bool Netplay::optionNeedsRestart(const OptionDef& d) {
    return d.info.rfind("Restart required", 0) == 0 || d.infoCategorized.rfind("Restart required", 0) == 0 ||
           contains(d.desc, "Restart");
}

std::string Netplay::envOverride() {
    const char* h = std::getenv("VJ_NETLINK_HOST");
    if (h && *h) return "VJ_NETLINK_HOST";
    const char* p = std::getenv("VJ_NETLINK_PORT");
    if (p && *p) return "VJ_NETLINK_PORT";
    return "";
}

bool Netplay::parseStatsLine(const std::string& line, LinkStats& out) {
    size_t at = line.find("link stats ");
    if (at == std::string::npos) return false;
    LinkStats s;
    s.raw = line.substr(at);
    double v = 0;
    if (numAfter(s.raw, "link stats ", v)) s.windowSec = (unsigned)v;
    // Every figure is required except the optional tail; a line missing the core ones is not ours.
    if (!numAfter(s.raw, "hops/s=", v)) return false;
    s.hopsPerSec = (unsigned)v;
    if (!numAfter(s.raw, " hop=", v)) return false;
    s.hopMs = v;
    if (numAfter(s.raw, "wait/hop=", v)) s.waitPerHopMs = v;
    if (numAfter(s.raw, "slipped=", v)) s.slippedPct = (unsigned)v;
    size_t p = s.raw.find("slipped=");
    if (p != std::string::npos) {
        unsigned a = 0, b = 0;
        size_t q = s.raw.find('(', p);
        if (q != std::string::npos && std::sscanf(s.raw.c_str() + q, "(%u/%u)", &a, &b) == 2) {
            s.slipped = a;
            s.hops = b;
        }
    }
    if (numAfter(s.raw, "timeouts=", v)) s.timeouts = (unsigned)v;
    if (numAfter(s.raw, "budget_cut=", v)) s.budgetCut = (unsigned)v;
    if (numAfter(s.raw, "budget_us=", v)) s.budgetUs = (int)v;
    p = s.raw.find("idle_wait=");
    if (p != std::string::npos) {
        unsigned a = 0, b = 0;
        if (std::sscanf(s.raw.c_str() + p, "idle_wait=%u/%u", &a, &b) == 2) {
            s.idleHits = a;
            s.idleWaits = b;
        }
    }
    s.valid = true;
    out = s;
    return true;
}

std::vector<DiscoveredHost> Netplay::hostsFromOptions(const std::vector<OptionDef>& defs) {
    std::vector<DiscoveredHost> out;
    for (const OptionDef& d : defs) {
        if (d.key != kHostKey) continue;
        for (const OptionValue& v : d.values) {
            // A discovered peer's label is "<name> - <addr>" (+ a device tag); the three presets' labels
            // never end that way, which also keeps a peer on THIS machine (127.0.0.1) from being
            // mistaken for the preset of the same value.
            if (isPresetHost(v.value) && !peerLabel(v)) continue;
            DiscoveredHost h;
            h.address = v.value;
            h.label = v.label;
            // The core labels a peer "<name> - <addr>" and appends " (Voice Modem)" / " (JagLink)"
            // when the peer's device differs from ours.
            std::string l = v.label;
            const char* tags[] = {" (Voice Modem)", " (JagLink)"};
            for (const char* t : tags) {
                size_t n = std::strlen(t);
                if (l.size() > n && l.compare(l.size() - n, n, t) == 0) {
                    h.device = std::string(t + 2, n - 3);
                    l.resize(l.size() - n);
                    break;
                }
            }
            std::string suffix = " - " + v.value;
            if (l.size() >= suffix.size() && l.compare(l.size() - suffix.size(), suffix.size(), suffix) == 0)
                h.name = l.substr(0, l.size() - suffix.size());
            if (h.name == h.address) h.name.clear();
            out.push_back(std::move(h));
        }
    }
    return out;
}

// ---------------------------------------------------------------------------------
// applying settings
// ---------------------------------------------------------------------------------
void Netplay::setOverride(const char* key, const std::string& value, std::vector<std::string>* restartKeys) {
    host_.overrideOption(key, value);
    if (!restartKeys) return;
    for (const OptionDef& d : host_.optionDefs())
        if (d.key == key && optionNeedsRestart(d)) restartKeys->push_back(key);
}

bool Netplay::writeAddressFile(const std::string& addr, std::string& err) {
    if (systemDir_.empty()) {
        err = "no system directory to keep the address in";
        return false;
    }
    std::string path = systemDir_ + kAddressFile;
    std::string body = addr + "\n";
    if (!writeFileAtomic(path, body.data(), body.size())) {
        err = "could not write " + path;
        return false;
    }
    return true;
}

bool Netplay::applyNow(const NetSettings& s, std::string& err, std::vector<std::string>* restartKeys) {
    // The core's host option takes the typed address itself.  The vj_netlink.txt route is the fallback
    // (viaOption_ = false) for a build whose host option is still a closed list.
    if (s.role == NetRole::Join && !s.address.empty() && !viaOption_) {
        if (!writeAddressFile(s.address, err)) return false;
    }
    setOverride(kModeKey, modeValue(s.role), restartKeys);

    // Saved like any option: it is a preference, not a standing "listen on the network".
    host_.setOptionValue(kDeviceKey, deviceValue(s.device));
    if (restartKeys)
        for (const OptionDef& d : host_.optionDefs())
            if (d.key == kDeviceKey && optionNeedsRestart(d)) restartKeys->push_back(kDeviceKey);

    if (s.role == NetRole::Join) {
        // Looking around (no address) must not dial anything meaningful: stay on the preset.
        setOverride(kHostKey, s.address.empty() ? "127.0.0.1" : (viaOption_ ? s.address : std::string(kAddressFile)),
                    restartKeys);
    } else {
        host_.clearOverride(kHostKey);
    }
    // Any port from 1 to 65535: the core's port option takes the typed value (the VJ_NETLINK_PORT
    // environment route this used for ports outside the four presets is gone).
    if (s.role == NetRole::Host || s.role == NetRole::Join)
        setOverride(kPortKey, std::to_string(s.port), restartKeys);
    else
        host_.clearOverride(kPortKey);

    want_ = s;
    applied_ = s;
    appliedValid_ = true;
    state_ = LinkState::Waiting;
    upDevice_.clear();
    peerText_.clear();
    stats_ = LinkStats();
    haveStats_ = false;
    openError_.clear();
    discoveryError_.clear();
    return true;
}

bool Netplay::start(const NetSettings& s, std::string& err, std::vector<std::string>* restartKeys) {
    if (s.role == NetRole::Off) {
        stop();
        return true;
    }
    if (s.port < 1 || s.port > 65535) {
        err = "the port must be between 1 and 65535";
        return false;
    }
    if (s.role == NetRole::Join && !s.address.empty()) {
        std::string why;
        if (!validTypedHost(s.address, &why)) {
            err = "that does not look like an address (use a name or an IPv4 address, no port): " + why;
            return false;
        }
    }
    const bool endpoint = s.role == NetRole::Host || s.role == NetRole::Join;
    const bool changed = appliedValid_ && (applied_.role != s.role ||
                                           (endpoint && (applied_.port != s.port || applied_.address != s.address)));
    if (changed && host_.isLoaded()) {
        // Go through "disabled" for a couple of frames first.  UARTSetLinkMode() only re-opens on a mode
        // change and the TCP client copies its endpoint when it opens, so a new host or port needs the
        // bounce; and the core only logs link UP/DOWN on a change, so a switch that goes straight from one
        // connected link to another would never report the new one.
        setOverride(kModeKey, modeValue(NetRole::Off), nullptr);
        bouncing_ = true;
        bounceFrames_ = 2;
        bounceTarget_ = s;
        want_ = s;
        state_ = LinkState::Waiting;
        upDevice_.clear();
        peerText_.clear();
        stats_ = LinkStats();
        haveStats_ = false;
        return true;
    }
    bouncing_ = false;
    return applyNow(s, err, restartKeys);
}

void Netplay::clearAll() {
    host_.clearOverride(kModeKey);
    host_.clearOverride(kHostKey);
    host_.clearOverride(kPortKey);
    bouncing_ = false;
    want_ = NetSettings();
    applied_ = NetSettings();
    appliedValid_ = false;
    state_ = LinkState::Off;
    upDevice_.clear();
    peerText_.clear();
    stats_ = LinkStats();
    haveStats_ = false;
    speedup_ = 1;
    openError_.clear();
    discoveryError_.clear();
}

void Netplay::stop() { clearAll(); }

void Netplay::afterFrame() {
    if (!bouncing_) return;
    if (--bounceFrames_ > 0) return;
    bouncing_ = false;
    std::string err;
    NetSettings t = bounceTarget_;
    if (!applyNow(t, err, nullptr)) {
        openError_ = err;
        state_ = LinkState::Off;
    }
}

// ---------------------------------------------------------------------------------
// what the core says
// ---------------------------------------------------------------------------------
void Netplay::onCoreLog(int /*level*/, const char* text) {
    if (!text) return;
    const char* tag = std::strstr(text, "[NETLINK] ");
    if (!tag) return;
    std::string l = tag + std::strlen("[NETLINK] ");
    if (want_.role == NetRole::Off || bouncing_) return;

    // With the core's event channel answered (CoreHost::eventsActive) link UP / DOWN, wire speed and the
    // stats arrive as events (onCoreEvent) and these lines are skipped; without it (a core that never
    // said HELLO, or announceExtensions off) the log text is all there is.
    const bool viaEvents = host_.eventsActive();
    if (l.rfind("link UP", 0) == 0) {
        if (viaEvents) return;
        state_ = LinkState::Up;
        size_t p = l.find("device=");
        if (p != std::string::npos) {
            size_t e = l.find_first_of(",) ", p + 7);
            upDevice_ = l.substr(p + 7, e == std::string::npos ? std::string::npos : e - (p + 7));
        }
        openError_.clear();
    } else if (l.rfind("link DOWN", 0) == 0) {
        if (viaEvents) return;
        state_ = state_ == LinkState::Up ? LinkState::Lost : LinkState::Waiting;
        stats_ = LinkStats();
        haveStats_ = false;
        speedup_ = 1;
    } else if (contains(l, "open, waiting for peer")) {
        state_ = LinkState::Waiting;
    } else if (l.rfind("mode=", 0) == 0) {
        size_t p = l.find("peer=");
        if (p != std::string::npos) {
            size_t e = l.find(' ', p);
            peerText_ = l.substr(p + 5, e == std::string::npos ? std::string::npos : e - (p + 5));
        }
    } else if (l.rfind("failed to open", 0) == 0) {
        openError_ = "The link could not be opened (is the port already in use by another program?)";
    } else if (l.rfind("LAN discovery failed", 0) == 0) {
        discoveryError_ = "Finding games on the network is not working (the network permission may have been "
                          "refused). You can still type the host's address.";
    } else if (l.rfind("wire speed auto CONFIRMED", 0) == 0) {
        if (viaEvents) return;
        size_t p = l.find("now ");
        speedup_ = p == std::string::npos ? 2u : (unsigned)std::strtoul(l.c_str() + p + 4, nullptr, 10);
        if (speedup_ < 1) speedup_ = 1;
    } else if (l.rfind("wire speed back to stock", 0) == 0) {
        if (viaEvents) return;
        speedup_ = 1;
    } else if (l.rfind("link stats", 0) == 0) {
        if (viaEvents) return;
        LinkStats s;
        if (parseStatsLine(l, s)) {
            stats_ = s;
            haveStats_ = true;
            statsAt_ = std::chrono::steady_clock::now();
        }
    }
}

// The core's private event channel (src/core/vj_frontend_ext.h): the same facts the "[NETLINK] ..."
// log lines carry, as numbers.  Runs inside CoreHost::runFrame on the emulation thread.
void Netplay::onCoreEvent(const vj_event& e) {
    if (want_.role == NetRole::Off || bouncing_) return;
    switch (e.type) {
    case VJ_EVT_LINK_UP:
        upEvents_++;
        state_ = LinkState::Up;
        upDevice_ = e.a ? "voicemodem" : "jaglink";
        if (e.s) {
            const char* sp = std::strchr(e.s, ' ');   // "tcp_client <host>"
            if (sp) peerText_ = sp + 1;
        }
        openError_.clear();
        break;
    case VJ_EVT_LINK_DOWN:
        state_ = state_ == LinkState::Up ? LinkState::Lost : LinkState::Waiting;
        stats_ = LinkStats();
        haveStats_ = false;
        speedup_ = 1;
        break;
    case VJ_EVT_LINK_STATS: {
        LinkStats s;
        s.windowSec = 5;   // the core reports about every 5 seconds
        s.hopsPerSec = e.a;
        s.hopMs = e.b / 100.0;
        s.slipped = e.c;
        s.timeouts = e.d;
        const unsigned hops = e.a * s.windowSec;
        s.hops = hops;
        s.slippedPct = hops ? std::min(100u, e.c * 100u / hops) : 0;
        char raw[160];
        std::snprintf(raw, sizeof raw, "link stats ~%us: hops/s=%u hop=%.2fms slipped=%u timeouts=%u", s.windowSec,
                      s.hopsPerSec, s.hopMs, s.slipped, s.timeouts);
        s.raw = raw;
        s.valid = true;
        stats_ = s;
        haveStats_ = true;
        statsAt_ = std::chrono::steady_clock::now();
        break;
    }
    case VJ_EVT_WIRE_SPEED:
        speedup_ = e.b && e.a > 1 ? e.a : 1u;
        break;
    default:   // CRASH_WATCHDOG is the host's business (an on-screen message); future types are ignored
        break;
    }
}

double Netplay::statsAgeSec() const {
    if (!haveStats_) return -1.0;
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - statsAt_).count();
}

std::vector<DiscoveredHost> Netplay::discovered() const {
    if (want_.role != NetRole::Join && want_.role != NetRole::Host) return {};
    return hostsFromOptions(host_.optionDefs());
}

std::string Netplay::statusText() const {
    const std::string port = std::to_string(want_.port);
    switch (want_.role) {
    case NetRole::Off: return "Not connected.";
    case NetRole::Loopback:
        return state_ == LinkState::Up ? "Echo test on: the console hears its own link traffic."
                                       : "Echo test starting...";
    case NetRole::Host:
        if (state_ == LinkState::Up) return "Connected: a partner has joined your game.";
        if (state_ == LinkState::Lost) return "Your partner disconnected. Still hosting; they can join again.";
        return "Hosting on port " + port + ". Waiting for your partner to join...";
    case NetRole::Join:
        if (want_.address.empty()) return "Looking for games on your network...";
        if (state_ == LinkState::Up) return "Connected to " + want_.address + ".";
        if (state_ == LinkState::Lost) return "Lost the connection to " + want_.address + ". Trying again...";
        return "Trying to reach " + want_.address + " (port " + port + ")...";
    }
    return "";
}

std::vector<std::string> Netplay::notices() const {
    std::vector<std::string> n;
    if (!openError_.empty()) n.push_back(openError_);
    if (!discoveryError_.empty() && (want_.role == NetRole::Host || want_.role == NetRole::Join))
        n.push_back(discoveryError_);
    std::string env = envOverride();
    if (!env.empty() && configured())
        n.push_back("The environment variable " + env + " is set and overrides what is chosen here.");
    return n;
}

}  // namespace vj
