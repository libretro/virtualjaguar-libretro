#include "dev_boot.h"

#include <cctype>
#include <cstdlib>
#include <vector>

#include "vj_debug_iface.h"

namespace vj {

namespace {

std::string lower(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

std::vector<std::string> split(const std::string& s, char sep) {
    std::vector<std::string> out;
    size_t a = 0;
    for (;;) {
        const size_t b = s.find(sep, a);
        out.push_back(s.substr(a, b == std::string::npos ? std::string::npos : b - a));
        if (b == std::string::npos) break;
        a = b + 1;
    }
    return out;
}

}  // namespace

bool parseBreakSpec(const std::string& text, BreakSpec& out, std::string& err) {
    out = BreakSpec();
    std::string rest = text;
    const size_t colon = text.find(':');
    if (colon != std::string::npos) {
        const std::string cpu = lower(text.substr(0, colon));
        if (cpu == "68k" || cpu == "m68k" || cpu == "68000") out.target = VJ_DBG_TARGET_M68K;
        else if (cpu == "gpu") out.target = VJ_DBG_TARGET_GPU;
        else if (cpu == "dsp") out.target = VJ_DBG_TARGET_DSP;
        else {
            err = "'" + text.substr(0, colon) + "' is not a processor (68k, gpu or dsp)";
            return false;
        }
        rest = text.substr(colon + 1);
    }
    if (rest.empty()) {
        err = "no address or symbol";
        return false;
    }
    out.where = rest;
    return true;
}

bool parseWatchSpec(const std::string& text, WatchSpec& out, std::string& err) {
    out = WatchSpec();
    const std::vector<std::string> parts = split(text, ':');
    if (parts.empty() || parts[0].empty()) {
        err = "no address or symbol";
        return false;
    }
    if (parts.size() > 3) {
        err = "too many ':' fields (ADDRESS[:LENGTH][:r|w|rw])";
        return false;
    }
    out.where = parts[0];
    bool haveLen = false, haveKind = false;
    for (size_t i = 1; i < parts.size(); i++) {
        const std::string p = lower(parts[i]);
        if (p == "r" || p == "w" || p == "rw" || p == "wr") {
            if (haveKind) {
                err = "the access kind is given twice";
                return false;
            }
            haveKind = true;
            if (p.find('r') != std::string::npos) out.kind |= VJ_DBG_WATCH_READ;
            if (p.find('w') != std::string::npos) out.kind |= VJ_DBG_WATCH_WRITE;
            continue;
        }
        char* end = nullptr;
        const unsigned long n = std::strtoul(p.c_str(), &end, 10);
        if (p.empty() || *end || n == 0 || n > 0x1000000ul || haveLen) {
            err = "'" + parts[i] + "' is neither a length in bytes nor r, w or rw";
            return false;
        }
        haveLen = true;
        out.len = (unsigned)n;
    }
    if (!haveKind) out.kind = VJ_DBG_WATCH_WRITE;
    return true;
}

}  // namespace vj
