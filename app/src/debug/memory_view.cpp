#include "memory_view.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <map>

namespace vj {
namespace memview {

// ---- parsing / formatting ---------------------------------------------------------------------------------

static std::string trimBlanks(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace((unsigned char)s[a])) a++;
    while (b > a && std::isspace((unsigned char)s[b - 1])) b--;
    return s.substr(a, b - a);
}

static int hexDigit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static std::string stripHexPrefix(const std::string& t) {
    if (!t.empty() && t[0] == '$') return t.substr(1);
    if (t.size() >= 2 && t[0] == '0' && (t[1] == 'x' || t[1] == 'X')) return t.substr(2);
    return t;
}

bool parseHex(const std::string& s, unsigned& v) {
    std::string t = stripHexPrefix(trimBlanks(s));
    if (t.empty() || t.size() > 8) return false;
    unsigned r = 0;
    for (char c : t) {
        int d = hexDigit(c);
        if (d < 0) return false;
        r = (r << 4) | (unsigned)d;
    }
    v = r;
    return true;
}

bool parseAddress(const std::string& s, unsigned& addr) {
    unsigned v;
    if (!parseHex(s, v) || v >= kBusSize) return false;
    addr = v;
    return true;
}

bool parseHexBytes(const std::string& s, std::vector<uint8_t>& out) {
    out.clear();
    std::vector<int> digits;
    size_t i = 0;
    while (i < s.size()) {
        char c = s[i];
        if (std::isspace((unsigned char)c) || c == ',') {
            i++;
            continue;
        }
        if (c == '$') {
            i++;
            continue;
        }
        if (c == '0' && i + 1 < s.size() && (s[i + 1] == 'x' || s[i + 1] == 'X')) {
            i += 2;
            continue;
        }
        int d = hexDigit(c);
        if (d < 0) {
            out.clear();
            return false;
        }
        digits.push_back(d);
        i++;
    }
    if (digits.empty() || (digits.size() & 1)) return false;
    for (size_t k = 0; k < digits.size(); k += 2) out.push_back((uint8_t)((digits[k] << 4) | digits[k + 1]));
    return true;
}

bool parseCell(const std::string& s, unsigned bytes, unsigned& v) {
    std::string t = trimBlanks(s);
    if (t.empty() || bytes == 0 || bytes > 4) return false;
    // leading zeros do not count against the width
    size_t nz = 0;
    while (nz + 1 < t.size() && t[nz] == '0') nz++;
    if (t.size() - nz > 2u * bytes) return false;
    unsigned r = 0;
    for (char c : t) {
        int d = hexDigit(c);
        if (d < 0) return false;
        r = (r << 4) | (unsigned)d;
    }
    v = r;
    return true;
}

unsigned decodeBE(const uint8_t* p, unsigned bytes) {
    unsigned v = 0;
    for (unsigned i = 0; i < bytes; i++) v = (v << 8) | p[i];
    return v;
}

void encodeBE(uint8_t* p, unsigned bytes, unsigned v) {
    for (unsigned i = 0; i < bytes; i++) p[i] = (uint8_t)(v >> (8 * (bytes - 1 - i)));
}

std::string hexCell(unsigned v, unsigned bytes) {
    char buf[16];
    std::snprintf(buf, sizeof buf, "%0*X", (int)(bytes * 2), bytes >= 4 ? v : (v & ((1u << (bytes * 8)) - 1)));
    return buf;
}

char asciiChar(uint8_t b) { return (b >= 0x20 && b < 0x7F) ? (char)b : '.'; }

std::string formatDumpLine(unsigned addr, const uint8_t* data, unsigned n, unsigned bytes, bool ascii) {
    char buf[16];
    std::snprintf(buf, sizeof buf, "%06X ", addr & 0xFFFFFFu);
    std::string out = buf;
    unsigned i = 0;
    while (i < n) {
        out += ' ';
        unsigned w = (n - i >= bytes) ? bytes : 1;
        out += hexCell(decodeBE(data + i, w), w);
        i += w;
    }
    if (ascii) {
        out += "  |";
        for (unsigned k = 0; k < n; k++) out += asciiChar(data[k]);
        out += '|';
    }
    return out;
}

// ---- regions ----------------------------------------------------------------------------------------------------

int regionIndexAt(const std::vector<vj_debug_region>& regions, unsigned addr) {
    for (size_t i = 0; i < regions.size(); i++)
        if (addr >= regions[i].start && addr - regions[i].start < regions[i].len) return (int)i;
    return -1;
}

unsigned clampToRegion(const vj_debug_region& r, unsigned addr, unsigned want) {
    if (addr < r.start || addr - r.start >= r.len) return 0;
    unsigned left = r.len - (addr - r.start);
    return want < left ? want : left;
}

unsigned rowCount(const vj_debug_region& r, unsigned perRow) {
    if (perRow == 0) return 0;
    return (r.len + perRow - 1) / perRow;
}

unsigned bytesPerRowFor(unsigned bytes, float availablePx, float cellPx) {
    if (bytes == 0) bytes = 1;
    unsigned maxCells = 16 / bytes;
    if (maxCells == 0) maxCells = 1;
    unsigned fit = (cellPx > 0 && availablePx > cellPx) ? (unsigned)(availablePx / cellPx) : 1;
    if (fit < 1) fit = 1;
    unsigned cells = 1;
    while (cells * 2 <= fit && cells * 2 <= maxCells) cells *= 2;
    return cells * bytes;
}

bool resolveGoto(const std::vector<vj_debug_region>& regions, const std::string& text, unsigned& addr, int& idx,
                 std::string& err) {
    unsigned a;
    if (!parseAddress(text, a)) {
        unsigned v;
        err = parseHex(text, v) ? "the Jaguar bus is 24 bit: use an address up to $FFFFFF"
                                : "type a hex address, e.g. 1F0000 or $F00400";
        return false;
    }
    int r = regionIndexAt(regions, a);
    if (r < 0) {
        char buf[64];
        std::snprintf(buf, sizeof buf, "$%06X is not mapped to anything", a);
        err = buf;
        return false;
    }
    addr = a;
    idx = r;
    err.clear();
    return true;
}

const char* regionKindText(const vj_debug_region& r) {
    if (r.flags & VJ_DBG_REGION_BLOCKED) return "blocked (never written)";
    if (r.flags & VJ_DBG_REGION_ROM) return "read-only";
    if (r.flags & VJ_DBG_REGION_REG) return "registers";
    if (r.flags & VJ_DBG_REGION_RAM) return "RAM";
    return "";
}

// ---- change marks -----------------------------------------------------------------------------------------------------

void ChangeTracker::reset() {
    cur_.clear();
    lastChange_.clear();
    base_ = 0;
}

void ChangeTracker::update(unsigned addr, const uint8_t* data, unsigned n, bool advance) {
    if (advance) tick_++;
    std::vector<uint8_t> nc(data, data + n);
    std::vector<unsigned> nl(n, 0);
    for (unsigned i = 0; i < n; i++) {
        unsigned a = addr + i;
        if (a < base_ || a - base_ >= cur_.size()) continue;   // no baseline for this byte
        unsigned o = a - base_;
        nl[i] = lastChange_[o];
        if (advance && cur_[o] != data[i]) nl[i] = tick_;
        if (!advance) nc[i] = cur_[o];   // not a refresh: keep what the baseline saw
    }
    base_ = addr;
    cur_.swap(nc);
    lastChange_.swap(nl);
}

bool ChangeTracker::changed(unsigned addr) const {
    if (addr < base_ || addr - base_ >= cur_.size()) return false;
    unsigned lc = lastChange_[addr - base_];
    return lc != 0 && tick_ - lc < hold_;
}

// ---- search --------------------------------------------------------------------------------------------------------------

std::vector<uint8_t> textNeedle(const std::string& s) { return std::vector<uint8_t>(s.begin(), s.end()); }

SearchJob::SearchJob(const DebugSession& s, const vj_debug_region& region, unsigned from,
                     std::vector<uint8_t> needle, bool ignoreCase)
    : s_(s), needle_(std::move(needle)), icase_(ignoreCase) {
    const unsigned S = region.start, E = region.start + region.len;
    const unsigned len = (unsigned)needle_.size();
    if (len == 0 || len > region.len) {
        done_ = true;
        return;
    }
    if (icase_)
        for (uint8_t& b : needle_) b = (uint8_t)std::tolower(b);
    unsigned s0 = (from >= S && from < E - 1) ? from + 1 : S;
    // match starts in [s0, E-len], then [S, s0) (the wrap); `end` is the exclusive end of the START positions
    segs_.push_back({s0, E - len + 1});
    if (s0 > S) segs_.push_back({S, std::min(s0, E - len + 1)});
    for (const Seg& g : segs_)
        if (g.end > g.start) total_ += g.end - g.start;
    seg_ = 0;
    pos_ = segs_[0].start;
}

bool SearchJob::matchAt(const uint8_t* p) const {
    const size_t n = needle_.size();
    if (!icase_) return std::memcmp(p, needle_.data(), n) == 0;
    for (size_t i = 0; i < n; i++)
        if ((uint8_t)std::tolower(p[i]) != needle_[i]) return false;
    return true;
}

bool SearchJob::step(unsigned budget) {
    if (done_) return true;
    s_.callCore([this, budget] { stepOnCore(budget); });
    return done_;
}

void SearchJob::stepOnCore(unsigned budget) {
    const unsigned len = (unsigned)needle_.size();
    std::vector<uint8_t> buf;
    while (!done_ && budget > 0) {
        if (seg_ >= segs_.size()) {
            done_ = true;
            break;
        }
        const Seg& g = segs_[seg_];
        if (pos_ >= g.end) {
            seg_++;
            if (seg_ < segs_.size()) pos_ = segs_[seg_].start;
            continue;
        }
        unsigned c = std::min(std::min(g.end - pos_, budget), 65536u);
        if (!s_.read(pos_, buf, c + len - 1)) {
            readFailed_ = true;
            done_ = true;
            break;
        }
        for (unsigned i = 0; i < c; i++) {
            if (matchAt(buf.data() + i)) {
                found_ = true;
                foundAddr_ = pos_ + i;
                done_ = true;
                break;
            }
        }
        if (done_) break;
        pos_ += c;
        scanned_ += c;
        budget -= c;
    }
}

float SearchJob::progress() const {
    if (done_) return 1.0f;
    return total_ ? (float)scanned_ / (float)total_ : 1.0f;
}

// ---- the write policy -------------------------------------------------------------------------------------------------------

static const BlockedRegister kBlocked[] = {
    {0xF02238, 4, "B_CMD", "writing it starts a blit"},
    {0xF02114, 4, "G_CTRL", "writing it starts or stops the GPU"},
    {0xF1A114, 4, "D_CTRL", "writing it starts or stops the DSP"},
    {0xF00020, 4, "OLP", "it is the Object Processor's list pointer"},
    {0xF1A148, 4, "LTXD", "writing it transmits on the serial port"},
    {0xF1A14C, 4, "RTXD", "writing it transmits on the serial port"},
};

const BlockedRegister* blockedRegisters(int* n) {
    if (n) *n = (int)(sizeof kBlocked / sizeof kBlocked[0]);
    return kBlocked;
}

const BlockedRegister* blockedRegisterAt(unsigned addr, unsigned n) {
    for (const BlockedRegister& b : kBlocked)
        if (addr < b.addr + b.len && b.addr < addr + n) return &b;
    return nullptr;
}

static WriteDecision refuse(int err, const std::string& why) {
    WriteDecision d;
    d.ok = false;
    d.err = err;
    d.reason = why;
    return d;
}

WriteDecision decideWrite(const std::vector<vj_debug_region>& regions, unsigned addr, unsigned n,
                          bool allowRegisterWrites) {
    if (n == 0 || addr >= kBusSize || n > kBusSize - addr)
        return refuse(VJ_DBG_ERR_RANGE, "the write leaves the Jaguar's 24-bit address space");
    char buf[160];
    unsigned a = addr;
    const unsigned end = addr + n;
    while (a < end) {
        int ri = regionIndexAt(regions, a);
        if (ri < 0) {
            std::snprintf(buf, sizeof buf, "$%06X is not mapped to anything", a);
            return refuse(VJ_DBG_ERR_RANGE, buf);
        }
        const vj_debug_region& r = regions[(size_t)ri];
        const unsigned segEnd = std::min(end, r.start + r.len);
        const unsigned segLen = segEnd - a;
        const char* name = r.name ? r.name : "region";
        if (r.flags & VJ_DBG_REGION_BLOCKED) {
            std::snprintf(buf, sizeof buf, "%s ($%06X-$%06X) is never writable: the CD-ROM registers have side effects",
                          name, r.start, r.start + r.len - 1);
            return refuse(VJ_DBG_ERR_POLICY, buf);
        }
        if (r.flags & VJ_DBG_REGION_ROM) {
            std::snprintf(buf, sizeof buf, "%s is read-only (ROM)", name);
            return refuse(VJ_DBG_ERR_READONLY, buf);
        }
        if (const BlockedRegister* b = blockedRegisterAt(a, segLen)) {
            std::snprintf(buf, sizeof buf, "%s ($%06X) is never written from here: %s", b->name, b->addr, b->why);
            return refuse(VJ_DBG_ERR_POLICY, buf);
        }
        const bool clut = a < kClutEnd && segEnd > kClutA;
        if (((r.flags & VJ_DBG_REGION_REG) || clut) && !allowRegisterWrites) {
            if (clut && !(r.flags & VJ_DBG_REGION_REG))
                std::snprintf(buf, sizeof buf, "editing the palette (CLUT) needs \"Allow register writes\"");
            else
                std::snprintf(buf, sizeof buf, "%s are hardware registers: tick \"Allow register writes\" to edit them", name);
            return refuse(VJ_DBG_ERR_POLICY, buf);
        }
        a = segEnd;
    }
    WriteDecision d;
    d.ok = true;
    return d;
}

std::vector<WriteOp> planWrite(unsigned addr, const uint8_t* data, unsigned n) {
    std::map<unsigned, uint8_t> bytes;
    for (unsigned i = 0; i < n; i++) {
        unsigned a = addr + i;
        if (a >= kClutA && a < kClutEnd) {
            unsigned off = (a - kClutA) & 0x1FF;
            bytes[kClutA + off] = data[i];
            bytes[kClutB + off] = data[i];
        } else {
            bytes[a] = data[i];
        }
    }
    std::vector<WriteOp> ops;
    for (const auto& kv : bytes) {
        if (!ops.empty() && ops.back().addr + ops.back().data.size() == kv.first) {
            ops.back().data.push_back(kv.second);
        } else {
            WriteOp o;
            o.addr = kv.first;
            o.data.push_back(kv.second);
            ops.push_back(std::move(o));
        }
    }
    return ops;
}

int writeBytes(DebugSession& s, unsigned addr, const uint8_t* data, unsigned n, std::string* why) {
    if (why) why->clear();
    if (!s.enabled()) {
        if (why) *why = s.disabledReason();
        return VJ_DBG_ERR_STATE;
    }
    WriteDecision d = decideWrite(s.regions(), addr, n, s.allowRegisterWrites);
    if (!d.ok) {
        if (why) *why = d.reason;
        return d.err;
    }
    std::vector<WriteOp> ops = planWrite(addr, data, n);
    for (const WriteOp& o : ops) {
        int rc = s.poke(o.addr, o.data.data(), (unsigned)o.data.size());
        if (rc < 0) {
            if (why) *why = DebugSession::errorText(rc);
            return rc;
        }
    }
    return (int)n;
}

int writeCell(DebugSession& s, unsigned addr, unsigned bytes, unsigned v, std::string* why) {
    if (bytes != 1 && bytes != 2 && bytes != 4) return VJ_DBG_ERR_RANGE;
    uint8_t b[4];
    encodeBE(b, bytes, v);
    return writeBytes(s, addr, b, bytes, why);
}

// ---- the palette ------------------------------------------------------------------------------------------------------------------

uint32_t rgb16ToRgb888(unsigned v) {
    // The renderer's RGB16ToRGB32[]: R = bits 15-11, B = bits 10-6, G = bits 5-0, each placed at the top of its byte.
    return ((v & 0xF800u) << 8) | ((v & 0x003Fu) << 10) | ((v & 0x07C0u) >> 3);
}

unsigned rgb888ToRgb16(uint32_t rgb) {
    unsigned r = (rgb >> 16) & 0xFF, g = (rgb >> 8) & 0xFF, b = rgb & 0xFF;
    return ((r >> 3) << 11) | ((b >> 3) << 6) | (g >> 2);
}

bool CryCodec::build(const vj_debug_iface* iface) {
    if (!iface || !iface->cry_to_rgb) {
        table_.clear();
        built_ = nullptr;
        return false;
    }
    if (iface == built_ && !table_.empty()) return true;
    table_.resize(0x10000);
    for (unsigned v = 0; v < 0x10000; v++) table_[v] = iface->cry_to_rgb(v) & 0xFFFFFFu;
    built_ = iface;
    return true;
}

bool CryCodec::buildFrom(DebugSession& s) {
    const uint64_t ep = s.views().epoch();
    if (ep == epoch_) return !refused_ && !table_.empty();
    std::vector<uint32_t> t;
    const bool ok = s.buildCryTable(t) && t.size() == 0x10000;
    epoch_ = ep;
    refused_ = !ok;
    built_ = nullptr;   // build()'s identity check no longer describes this table
    if (ok) table_.swap(t);
    else table_.clear();
    return ok;
}

unsigned CryCodec::encode(uint32_t rgb) const {
    if (table_.empty()) return 0;
    const int r = (int)((rgb >> 16) & 0xFF), g = (int)((rgb >> 8) & 0xFF), b = (int)(rgb & 0xFF);
    unsigned best = 0;
    long bestD = -1;
    for (unsigned v = 0; v < 0x10000; v++) {
        uint32_t c = table_[v];
        int dr = r - (int)((c >> 16) & 0xFF), dg = g - (int)((c >> 8) & 0xFF), db = b - (int)(c & 0xFF);
        long d = (long)dr * dr + (long)dg * dg + (long)db * db;
        if (bestD < 0 || d < bestD) {
            bestD = d;
            best = v;
            if (d == 0) break;
        }
    }
    return best;
}

uint32_t swatchColor(ColorMode m, unsigned raw16, const CryCodec& cry) {
    return m == ColorMode::Rgb16 ? rgb16ToRgb888(raw16) : cry.decode(raw16);
}

unsigned encodeSwatch(ColorMode m, uint32_t rgb, const CryCodec& cry) {
    return m == ColorMode::Rgb16 ? rgb888ToRgb16(rgb) : cry.encode(rgb);
}

}  // namespace memview
}  // namespace vj
