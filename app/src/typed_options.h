// Free-form ("typed") values for the core options that take a number or an address.
//
// The core publishes every option as an enumeration, because that is all RetroArch can show.  But
// the core's own parsers already take any string for these keys (libretro.c: check_variables(),
// netlink_apply(), the GDB stub setup), so this app lets the user type a value and offers the
// core's list as presets.  This header is the single table of which keys are typed, what range
// the core really honours, and the validators -- header-only and free of ImGui, so CoreHost
// (validFor), the Settings widget, the Netplay window and the tests all agree.
//
// Rules:
//   * Only the keys in the table are typed; every other option stays a strict list.
//   * A typed value is accepted only if it validates -- the UI never calls setOptionValue with a
//     value that fails, and CoreHost ignores a saved / --option value that fails (as it always did
//     for a value outside a list).
//   * A value that is one of the core's own list entries is always valid (the table cannot make a
//     preset the core publishes unusable).
#pragma once

#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <string>

namespace vj {

enum class OptKind { Int, Host, Percent };

struct TypedOption {
    const char* key;
    OptKind kind;
    long min, max;       // Int / Percent
    const char* unit;    // shown after the box: "", "%", "s", "units"
    const char* hint;    // what to type, e.g. "1-65535"
};

namespace typed_detail {

// All the per-axis tuning options (#439 / #437) share three shapes.
#define VJ_TYPED_AXIS(prefix)                                                                             \
    {prefix "_deadzone_x", OptKind::Int, 0, 64, "units", "0-64"},                                         \
    {prefix "_deadzone_y", OptKind::Int, 0, 64, "units", "0-64"},                                         \
    {prefix "_offset_x", OptKind::Int, -64, 64, "units", "-64 to 64"},                                    \
    {prefix "_offset_y", OptKind::Int, -64, 64, "units", "-64 to 64"},                                    \
    {prefix "_exponent_x", OptKind::Int, 1, 800, "% of linear", "1-800 (100 = linear)"},                  \
    {prefix "_exponent_y", OptKind::Int, 1, 800, "% of linear", "1-800 (100 = linear)"}

inline const TypedOption* table(size_t& n) {
    // Ranges are what the core honours, not what the preset lists happen to offer:
    //   ports        libretro.c netlink_apply() atoi > 0 and JLinkSetTCPEndpoint() 1..65535;
    //                the GDB stub setup accepts 1..65535
    //   halt timeout atoi > 0 (clamped to 3600 in the core)
    //   sensitivity  atoi clamped 1..1600
    //   tuning       AxisTuneSet(): dead zone 0..AXISTUNE_REF(64), offset +-64; read_tune_exponent() 1..800
    //   clock scale  percentage form accepted by check_variables(), clamped 25..400
    static const TypedOption t[] = {
        {"virtualjaguar_netlink_port", OptKind::Int, 1, 65535, "", "1-65535"},
        {"virtualjaguar_netlink_host", OptKind::Host, 0, 0, "", "name or IPv4 address"},
        {"virtualjaguar_gdb_port", OptKind::Int, 1, 65535, "", "1-65535"},
        {"virtualjaguar_gdb_halt_timeout", OptKind::Int, 1, 3600, "seconds", "1-3600"},
        {"virtualjaguar_mouse_sensitivity", OptKind::Int, 1, 1600, "%", "1-1600"},
        {"virtualjaguar_rotary_sensitivity", OptKind::Int, 1, 1600, "%", "1-1600"},
        {"virtualjaguar_m68k_clock_scale", OptKind::Percent, 25, 400, "%", "25-400"},
        {"virtualjaguar_risc_clock_scale", OptKind::Percent, 25, 400, "%", "25-400"},
        VJ_TYPED_AXIS("virtualjaguar_mouse"),
        {"virtualjaguar_rotary_deadzone", OptKind::Int, 0, 64, "units", "0-64"},
        {"virtualjaguar_rotary_offset", OptKind::Int, -64, 64, "units", "-64 to 64"},
        {"virtualjaguar_rotary_exponent", OptKind::Int, 1, 800, "% of linear", "1-800 (100 = linear)"},
        VJ_TYPED_AXIS("virtualjaguar_analog"),
    };
    n = sizeof t / sizeof t[0];
    return t;
}
#undef VJ_TYPED_AXIS

}  // namespace typed_detail

// The table row for `key`, or nullptr when the key is a strict list.
inline const TypedOption* typedOptionFor(const std::string& key) {
    size_t n = 0;
    const TypedOption* t = typed_detail::table(n);
    for (size_t i = 0; i < n; i++)
        if (key == t[i].key) return &t[i];
    return nullptr;
}

// ---- parsers / validators (all return false and leave `out` alone on bad input) -----------------

// Optional '-', then 1-9 digits.  No '+', spaces, hex or exponent.
inline bool parseTypedInt(const std::string& s, long& out) {
    size_t i = 0;
    if (s.empty()) return false;
    if (s[0] == '-') i = 1;
    if (i >= s.size() || s.size() - i > 9) return false;
    for (size_t k = i; k < s.size(); k++)
        if (s[k] < '0' || s[k] > '9') return false;
    out = std::strtol(s.c_str(), nullptr, 10);
    return true;
}

// 1-4 digits and an optional trailing '%' -- exactly the form the core's clock-scale parser takes.
inline bool parseTypedPercent(const std::string& s, long& out) {
    std::string d = s;
    if (!d.empty() && d.back() == '%') d.pop_back();
    if (d.empty() || d.size() > 4) return false;
    for (char c : d)
        if (c < '0' || c > '9') return false;
    out = std::strtol(d.c_str(), nullptr, 10);
    return true;
}

// A host the core can dial: 1-127 characters (its buffers are 128 bytes), either a dotted-quad IPv4
// address or a DNS-style name.  IPv6 is not accepted: the core's link sockets are AF_INET
// (jlink_tcp.c).  `why` receives the reason when it returns false.
inline bool validTypedHost(const std::string& h, std::string* why = nullptr) {
    auto fail = [&](const char* m) {
        if (why) *why = m;
        return false;
    };
    if (h.empty()) return fail("type an address or a name");
    if (h.size() > 127) return fail("too long (127 characters at most)");
    bool numeric = true;
    for (char c : h) {
        const bool alnum = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
        if (!(alnum || c == '.' || c == '-' || c == '_'))
            return fail("use letters, digits, '.', '-' and '_' only (no port, no spaces; IPv6 is not supported)");
        if (!((c >= '0' && c <= '9') || c == '.')) numeric = false;
    }
    if (h.front() == '.' || h.back() == '.' || h.find("..") != std::string::npos)
        return fail("empty part between dots");
    // Each label 1-63 characters, not starting or ending with '-'.
    size_t start = 0;
    int labels = 0;
    for (;;) {
        size_t dot = h.find('.', start);
        std::string lab = h.substr(start, dot == std::string::npos ? std::string::npos : dot - start);
        if (lab.empty() || lab.size() > 63) return fail("each part must be 1-63 characters");
        if (lab.front() == '-' || lab.back() == '-') return fail("a part may not start or end with '-'");
        labels++;
        if (numeric) {
            if (lab.size() > 3 || std::atoi(lab.c_str()) > 255) return fail("not a valid IPv4 address");
        }
        if (dot == std::string::npos) break;
        start = dot + 1;
    }
    if (numeric && labels != 4) return fail("an IPv4 address has four numbers");
    return true;
}

// Is `value` acceptable for the typed option `t`?  `why` receives a short reason when not.
inline bool validTypedValue(const TypedOption& t, const std::string& value, std::string* why = nullptr) {
    auto fail = [&](const std::string& m) {
        if (why) *why = m;
        return false;
    };
    long v = 0;
    switch (t.kind) {
    case OptKind::Host: return validTypedHost(value, why);
    case OptKind::Int:
        if (!parseTypedInt(value, v)) return fail(std::string("type a whole number, ") + t.hint);
        break;
    case OptKind::Percent:
        if (!parseTypedPercent(value, v)) return fail(std::string("type a percentage, ") + t.hint);
        break;
    }
    if (v < t.min || v > t.max) return fail(std::string("out of range, ") + t.hint);
    return true;
}

// The form stored for a typed value: a percentage always carries its '%'.
inline std::string canonicalTypedValue(const TypedOption& t, const std::string& value) {
    if (t.kind == OptKind::Percent && !value.empty() && value.back() != '%') return value + "%";
    return value;
}

}  // namespace vj
