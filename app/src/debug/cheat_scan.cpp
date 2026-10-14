#include "cheat_scan.h"

#include <cctype>
#include <cstdio>
#include <cstring>

namespace vj {

namespace {

inline uint32_t maskOf(unsigned width) { return width >= 4 ? 0xFFFFFFFFu : ((1u << (8 * width)) - 1u); }

inline uint32_t rawAt(const uint8_t* p, unsigned width) {
    switch (width) {
    case 1: return p[0];
    case 2: return ((uint32_t)p[0] << 8) | p[1];
    default: return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
    }
}

inline int64_t conv(uint32_t raw, unsigned width, bool isSigned) {
    if (!isSigned) return (int64_t)raw;
    switch (width) {
    case 1: return (int64_t)(int8_t)(uint8_t)raw;
    case 2: return (int64_t)(int16_t)(uint16_t)raw;
    default: return (int64_t)(int32_t)raw;
    }
}

inline bool testBit(const std::vector<uint64_t>& b, size_t i) { return (b[i >> 6] >> (i & 63)) & 1u; }
inline void setBit(std::vector<uint64_t>& b, size_t i) { b[i >> 6] |= (uint64_t)1 << (i & 63); }

}  // namespace

// ---- helpers --------------------------------------------------------------------------------------

const char* CheatScan::modeName(ScanMode m) {
    switch (m) {
    case ScanMode::Unknown: return "Unknown initial value";
    case ScanMode::Equal: return "Equal to value";
    case ScanMode::NotEqual: return "Not equal to value";
    case ScanMode::Greater: return "Greater than value";
    case ScanMode::Less: return "Less than value";
    case ScanMode::Range: return "In range";
    case ScanMode::Changed: return "Changed";
    case ScanMode::Unchanged: return "Unchanged";
    case ScanMode::Increased: return "Increased";
    case ScanMode::Decreased: return "Decreased";
    case ScanMode::IncreasedBy: return "Increased by";
    case ScanMode::DecreasedBy: return "Decreased by";
    }
    return "?";
}

bool CheatScan::modeNeedsValue(ScanMode m) {
    switch (m) {
    case ScanMode::Equal:
    case ScanMode::NotEqual:
    case ScanMode::Greater:
    case ScanMode::Less:
    case ScanMode::Range:
    case ScanMode::IncreasedBy:
    case ScanMode::DecreasedBy: return true;
    default: return false;
    }
}

bool CheatScan::modeNeedsPrevious(ScanMode m) {
    switch (m) {
    case ScanMode::Changed:
    case ScanMode::Unchanged:
    case ScanMode::Increased:
    case ScanMode::Decreased:
    case ScanMode::IncreasedBy:
    case ScanMode::DecreasedBy: return true;
    default: return false;
    }
}

int64_t CheatScan::minValue(unsigned width, bool isSigned) {
    return isSigned ? -((int64_t)1 << (8 * width - 1)) : 0;
}
int64_t CheatScan::maxValue(unsigned width, bool isSigned) {
    return isSigned ? (((int64_t)1 << (8 * width - 1)) - 1) : (int64_t)maskOf(width);
}

int64_t CheatScan::decode(const uint8_t* p, unsigned width, bool isSigned) {
    return conv(rawAt(p, width), width, isSigned);
}

void CheatScan::encode(int64_t v, unsigned width, uint8_t* out) {
    uint32_t u = (uint32_t)(uint64_t)v;
    for (unsigned i = 0; i < width; i++) out[i] = (uint8_t)(u >> (8 * (width - 1 - i)));
}

bool CheatScan::parseNumber(const std::string& in, int64_t& out) {
    size_t i = 0, n = in.size();
    while (i < n && std::isspace((unsigned char)in[i])) i++;
    while (n > i && std::isspace((unsigned char)in[n - 1])) n--;
    if (i >= n) return false;
    bool neg = false;
    if (in[i] == '-' || in[i] == '+') {
        neg = in[i] == '-';
        i++;
    }
    unsigned base = 10;
    if (i < n && in[i] == '$') {
        base = 16;
        i++;
    } else if (i + 1 < n && in[i] == '0' && (in[i + 1] == 'x' || in[i + 1] == 'X')) {
        base = 16;
        i += 2;
    }
    if (i >= n) return false;
    uint64_t v = 0;
    for (; i < n; i++) {
        int d;
        char c = in[i];
        if (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'a' && c <= 'f') d = 10 + c - 'a';
        else if (c >= 'A' && c <= 'F') d = 10 + c - 'A';
        else return false;
        if ((unsigned)d >= base) return false;
        v = v * base + (unsigned)d;
        if (v > 0xFFFFFFFFFFull) return false;   // far beyond any 32-bit value
    }
    out = neg ? -(int64_t)v : (int64_t)v;
    return true;
}

std::string CheatScan::formatValue(int64_t v, unsigned width, bool hex) {
    char b[32];
    if (hex) std::snprintf(b, sizeof b, "%0*X", (int)(width * 2), (unsigned)((uint32_t)(uint64_t)v & maskOf(width)));
    else std::snprintf(b, sizeof b, "%lld", (long long)v);
    return b;
}

// ---- state ------------------------------------------------------------------------------------------

void CheatScan::reset() {
    hist_.clear();
    size_ = 0;
}

bool CheatScan::isCandidate(uint32_t addr) const {
    if (!active() || addr >= size_) return false;
    return testBit(hist_.back().bits, addr);
}

void CheatScan::results(size_t skip, size_t max, std::vector<uint32_t>& out) const {
    out.clear();
    if (!active() || max == 0) return;
    const std::vector<uint64_t>& b = hist_.back().bits;
    for (size_t wi = 0; wi < b.size() && out.size() < max; wi++) {
        uint64_t w = b[wi];
        if (!w) continue;
        for (unsigned k = 0; k < 64 && w; k++, w >>= 1) {
            if (!(w & 1)) continue;
            if (skip) {
                skip--;
                continue;
            }
            out.push_back((uint32_t)(wi * 64 + k));
            if (out.size() >= max) return;
        }
    }
}

int64_t CheatScan::previous(uint32_t addr) const {
    if (!active() || (size_t)addr + opt_.width > size_) return 0;
    return decode(hist_.back().snap.data() + addr, opt_.width, opt_.isSigned);
}

int64_t CheatScan::valueIn(const uint8_t* ram, size_t size, uint32_t addr) const {
    if (!ram || (size_t)addr + opt_.width > size) return 0;
    return decode(ram + addr, opt_.width, opt_.isSigned);
}

bool CheatScan::validSpec(const ScanSpec& s, bool first, std::string* err) const {
    auto fail = [&](const std::string& m) {
        if (err) *err = m;
        return false;
    };
    if (first && modeNeedsPrevious(s.mode))
        return fail(std::string("\"") + modeName(s.mode) + "\" compares with an earlier scan; start with a value or Unknown");
    if (!first && s.mode == ScanMode::Unknown) return fail("\"Unknown initial value\" only starts a search");
    if (!modeNeedsValue(s.mode)) return true;
    const bool delta = s.mode == ScanMode::IncreasedBy || s.mode == ScanMode::DecreasedBy;
    const int64_t lo = delta ? 0 : minValue(opt_.width, opt_.isSigned);
    const int64_t hi = delta ? (int64_t)maskOf(opt_.width) : maxValue(opt_.width, opt_.isSigned);
    auto inRange = [&](int64_t v) { return v >= lo && v <= hi; };
    char b[96];
    if (!inRange(s.value) || (s.mode == ScanMode::Range && !inRange(s.value2))) {
        std::snprintf(b, sizeof b, "value out of range for %u-bit %s (%lld to %lld)", opt_.width * 8,
                      delta ? "difference" : opt_.isSigned ? "signed" : "unsigned", (long long)lo, (long long)hi);
        return fail(b);
    }
    if (s.mode == ScanMode::Range && s.value > s.value2) return fail("the range starts above where it ends");
    return true;
}

bool CheatScan::begin(const uint8_t* ram, size_t size, const ScanOptions& o, const ScanSpec& s, std::string* err) {
    auto fail = [&](const char* m) {
        if (err) *err = m;
        return false;
    };
    if (!ram || size == 0) return fail("no RAM to search");
    if (o.width != 1 && o.width != 2 && o.width != 4) return fail("value width must be 1, 2 or 4 bytes");
    ScanOptions opt = o;
    if (opt.last == 0 || opt.last > size) opt.last = (uint32_t)size;
    if (opt.first >= opt.last || (size_t)(opt.last - opt.first) < opt.width) return fail("the address window is empty");
    CheatScan probe;
    probe.opt_ = opt;
    if (!probe.validSpec(s, true, err)) return false;

    // Every address a whole value fits at, in the window, honouring alignment.
    Pass p;
    p.bits.assign((size + 63) / 64, 0);
    p.snap.assign(ram, ram + size);
    for (uint32_t a = opt.first; (size_t)a + opt.width <= opt.last; a++) {
        if (opt.aligned && (a % opt.width)) continue;
        setBit(p.bits, a);
        p.count++;
    }
    hist_.clear();
    opt_ = opt;
    size_ = size;
    if (s.mode == ScanMode::Unknown) {
        hist_.push_back(std::move(p));
        return true;
    }
    Pass q;
    filter(p, ram, s, q);
    hist_.push_back(std::move(q));
    return true;
}

bool CheatScan::next(const uint8_t* ram, size_t size, const ScanSpec& s, std::string* err) {
    if (!active()) {
        if (err) *err = "start a new search first";
        return false;
    }
    if (!ram || size != size_) {
        if (err) *err = "the RAM changed size; start a new search";
        return false;
    }
    if (!validSpec(s, false, err)) return false;
    Pass q;
    filter(hist_.back(), ram, s, q);
    hist_.push_back(std::move(q));
    if (hist_.size() > kMaxUndo + 1) hist_.erase(hist_.begin());
    return true;
}

bool CheatScan::undo() {
    if (!canUndo()) return false;
    hist_.pop_back();
    return true;
}

// ---- the passes ------------------------------------------------------------------------------------------

void CheatScan::filter(const Pass& from, const uint8_t* ram, const ScanSpec& s, Pass& to) const {
    switch (s.mode) {
    case ScanMode::Unknown: to = from; to.snap.assign(ram, ram + size_); break;
    case ScanMode::Equal: filterMode<ScanMode::Equal>(from, ram, s, to); break;
    case ScanMode::NotEqual: filterMode<ScanMode::NotEqual>(from, ram, s, to); break;
    case ScanMode::Greater: filterMode<ScanMode::Greater>(from, ram, s, to); break;
    case ScanMode::Less: filterMode<ScanMode::Less>(from, ram, s, to); break;
    case ScanMode::Range: filterMode<ScanMode::Range>(from, ram, s, to); break;
    case ScanMode::Changed: filterMode<ScanMode::Changed>(from, ram, s, to); break;
    case ScanMode::Unchanged: filterMode<ScanMode::Unchanged>(from, ram, s, to); break;
    case ScanMode::Increased: filterMode<ScanMode::Increased>(from, ram, s, to); break;
    case ScanMode::Decreased: filterMode<ScanMode::Decreased>(from, ram, s, to); break;
    case ScanMode::IncreasedBy: filterMode<ScanMode::IncreasedBy>(from, ram, s, to); break;
    case ScanMode::DecreasedBy: filterMode<ScanMode::DecreasedBy>(from, ram, s, to); break;
    }
}

template <ScanMode M>
void CheatScan::filterMode(const Pass& from, const uint8_t* ram, const ScanSpec& s, Pass& to) const {
    const unsigned w = opt_.width;
    const bool sg = opt_.isSigned;
    const uint32_t mask = maskOf(w);
    const uint32_t delta = (uint32_t)(uint64_t)s.value & mask;
    to.bits.assign(from.bits.size(), 0);
    to.count = 0;
    for (size_t wi = 0; wi < from.bits.size(); wi++) {
        uint64_t word = from.bits[wi];
        if (!word) continue;
        for (unsigned k = 0; word; k++, word >>= 1) {
            if (!(word & 1)) continue;
            const size_t addr = wi * 64 + k;
            const uint32_t cr = rawAt(ram + addr, w);
            const int64_t cur = conv(cr, w, sg);
            bool keep;
            if (M == ScanMode::Equal) keep = cur == s.value;
            else if (M == ScanMode::NotEqual) keep = cur != s.value;
            else if (M == ScanMode::Greater) keep = cur > s.value;
            else if (M == ScanMode::Less) keep = cur < s.value;
            else if (M == ScanMode::Range) keep = cur >= s.value && cur <= s.value2;
            else {
                const uint32_t pr = rawAt(from.snap.data() + addr, w);
                const int64_t prv = conv(pr, w, sg);
                if (M == ScanMode::Changed) keep = cr != pr;
                else if (M == ScanMode::Unchanged) keep = cr == pr;
                else if (M == ScanMode::Increased) keep = cur > prv;
                else if (M == ScanMode::Decreased) keep = cur < prv;
                else if (M == ScanMode::IncreasedBy) keep = ((cr - pr) & mask) == delta;
                else keep = ((pr - cr) & mask) == delta;
            }
            if (keep) {
                setBit(to.bits, addr);
                to.count++;
            }
        }
    }
    to.snap.assign(ram, ram + size_);
}

}  // namespace vj
