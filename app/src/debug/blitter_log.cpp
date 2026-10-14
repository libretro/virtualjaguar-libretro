#include "blitter_log.h"

#include <cstdio>

namespace vj {
namespace blit {

namespace {
const CmdBit kBits[] = {
    {1u << 0, "SRCEN", "Read source data (from A2, or A1 when DSTA2)"},
    {1u << 1, "SRCENZ", "Read source Z data"},
    {1u << 2, "SRCENX", "Source extra data read"},
    {1u << 3, "DSTEN", "Read destination data (from A1, or A2 when DSTA2)"},
    {1u << 4, "DSTENZ", "Read destination Z data"},
    {1u << 5, "DSTWRZ", "Write Z data to the destination"},
    {1u << 6, "CLIP_A1", "A1 window clipping on"},
    {1u << 8, "UPDA1F", "Update the A1 fractional pointer each outer loop"},
    {1u << 9, "UPDA1", "Update the A1 pointer each outer loop"},
    {1u << 10, "UPDA2", "Update the A2 pointer each outer loop"},
    {1u << 11, "DSTA2", "A2 is the destination (normally A1 is)"},
    {1u << 12, "GOURD", "Gouraud shading"},
    {1u << 13, "GOURZ", "Computed (polygon) Z updates in the inner loop"},
    {1u << 14, "TOPBEN", "Carry into the top byte of the intensity integers"},
    {1u << 15, "TOPNEN", "Carry into the top nibble of the intensity integers"},
    {1u << 16, "PATDSEL", "Use the pattern data (B_PATD) instead of the LFU output"},
    {1u << 17, "ADDDSEL", "Add source and destination pixel values"},
    {1u << 25, "CMPDST", "Compare with the destination (else the source) for transparency"},
    {1u << 26, "BCOMPEN", "Bit comparator enable"},
    {1u << 27, "DCOMPEN", "Data comparator enable (pixel transparency)"},
    {1u << 28, "BKGWREN", "Write background (transparent) pixels"},
    {1u << 29, "BUSHI", "High bus priority (the manual says never to set it)"},
    {1u << 30, "SRCSHADE", "Gouraud shading applied to the source data"},
};

const Reg kRegs[] = {
    {"A1_BASE", 0xF02200, 4},  {"A1_FLAGS", 0xF02204, 4}, {"A1_CLIP", 0xF02208, 4},  {"A1_PIXEL", 0xF0220C, 4},
    {"A1_STEP", 0xF02210, 4},  {"A1_FSTEP", 0xF02214, 4}, {"A1_FPIXEL", 0xF02218, 4}, {"A1_INC", 0xF0221C, 4},
    {"A1_FINC", 0xF02220, 4},  {"A2_BASE", 0xF02224, 4},  {"A2_FLAGS", 0xF02228, 4}, {"A2_MASK", 0xF0222C, 4},
    {"A2_PIXEL", 0xF02230, 4}, {"A2_STEP", 0xF02234, 4},  {"B_CMD", 0xF02238, 4},    {"B_COUNT", 0xF0223C, 4},
    {"B_SRCD", 0xF02240, 8},   {"B_DSTD", 0xF02248, 8},   {"B_DSTZ", 0xF02250, 8},   {"B_SRCZ1", 0xF02258, 8},
    {"B_SRCZ2", 0xF02260, 8},  {"B_PATD", 0xF02268, 8},   {"B_IINC", 0xF02270, 4},   {"B_ZINC", 0xF02274, 4},
    {"B_STOP", 0xF02278, 4},   {"B_I3", 0xF0227C, 4},     {"B_I2", 0xF02280, 4},     {"B_I1", 0xF02284, 4},
    {"B_I0", 0xF02288, 4},     {"B_Z3", 0xF0228C, 4},     {"B_Z2", 0xF02290, 4},     {"B_Z1", 0xF02294, 4},
    {"B_Z0", 0xF02298, 4},
};

unsigned be32(const uint8_t* p) {
    return ((unsigned)p[0] << 24) | ((unsigned)p[1] << 16) | ((unsigned)p[2] << 8) | p[3];
}
}  // namespace

const CmdBit* cmdBits(size_t* count) {
    if (count) *count = sizeof kBits / sizeof kBits[0];
    return kBits;
}

std::vector<std::string> cmdFlagNames(unsigned cmd) {
    std::vector<std::string> out;
    for (const CmdBit& b : kBits)
        if (cmd & b.mask) out.push_back(b.name);
    return out;
}

unsigned cmdLfu(unsigned cmd) { return (cmd >> 21) & 0xF; }
unsigned cmdZmode(unsigned cmd) { return (cmd >> 18) & 0x7; }
bool cmdDstIsA2(unsigned cmd) { return (cmd & (1u << 11)) != 0; }

const char* lfuName(unsigned lfu) {
    // docs/jtrm-blitter.md, the LFU[3:0] table (JTRM Rev 8 p.74).
    static const char* const n[16] = {"0",     "~S&~D", "~S&D", "~S",    "S&~D", "~D", "S^D",   "~(S&D)",
                                      "S&D",   "~(S^D)", "D",    "~S|D", "S",    "S|~D", "S|D", "1"};
    return n[lfu & 15];
}

std::string zmodeName(unsigned z) {
    if ((z & 7) == 0) return "off";
    std::string s;
    if (z & 1) s += "S<D";
    if (z & 2) s += std::string(s.empty() ? "" : "|") + "S=D";
    if (z & 4) s += std::string(s.empty() ? "" : "|") + "S>D";
    return s;
}

std::string cmdSummary(unsigned cmd) {
    std::string s;
    for (const std::string& n : cmdFlagNames(cmd)) {
        if (!s.empty()) s += ' ';
        s += n;
    }
    if (!s.empty()) s += ' ';
    s += std::string("LFU=") + lfuName(cmdLfu(cmd));
    if (cmdZmode(cmd)) s += " ZMODE=" + zmodeName(cmdZmode(cmd));
    return s;
}

unsigned windowWidth(unsigned widthField) {
    unsigned e = (widthField >> 2) & 0xF, m = widthField & 3;
    return ((4u | m) << e) >> 2;
}

Channel decodeFlags(unsigned f) {
    static const unsigned pitchOf[4] = {1, 2, 4, 3};   // 0 contiguous, 1: 1 phrase gap, 2: 3 gaps, 3: 2 gaps
    Channel c;
    c.pitchField = f & 3;
    c.pitch = pitchOf[c.pitchField];
    c.pixelField = (f >> 3) & 7;
    c.bpp = c.pixelField <= 5 ? 1u << c.pixelField : 0;
    c.zoffs = (f >> 6) & 7;
    c.widthField = (f >> 9) & 0x3F;
    c.width = windowWidth(c.widthField);
    c.xAddCtl = (f >> 16) & 3;
    c.yAddCtl = (f >> 18) & 1;
    c.xSub = (f & (1u << 19)) != 0;
    c.ySub = (f & (1u << 20)) != 0;
    return c;
}

const char* xAddName(unsigned ctl) {
    static const char* const n[4] = {"phrase", "+pixel", "+0", "+inc"};
    return n[ctl & 3];
}

std::string describeChannel(const Channel& c) {
    char b[160];
    if (c.bpp)
        std::snprintf(b, sizeof b, "%ubpp  w=%u  pitch %u  x:%s%s  y:%s%u", c.bpp, c.width, c.pitch,
                      c.xSub ? "-" : "", xAddName(c.xAddCtl), c.ySub ? "-" : "+", c.yAddCtl);
    else
        std::snprintf(b, sizeof b, "bpp?(%u)  w=%u  pitch %u  x:%s%s  y:%s%u", c.pixelField, c.width, c.pitch,
                      c.xSub ? "-" : "", xAddName(c.xAddCtl), c.ySub ? "-" : "+", c.yAddCtl);
    return b;
}

int pixelX(unsigned v) { return (int)(int16_t)(v & 0xFFFF); }
int pixelY(unsigned v) { return (int)(int16_t)(v >> 16); }

Count decodeCount(unsigned bc) {
    Count c;
    c.inner = bc & 0xFFFF ? bc & 0xFFFF : 65536;
    c.outer = (bc >> 16) ? (bc >> 16) : 65536;
    return c;
}

const char* engineName(unsigned e) { return e ? "accurate" : "fast"; }

Row decodeRow(const vj_debug_blit& b) {
    Row r;
    r.raw = b;
    r.a1 = decodeFlags(b.a1_flags);
    r.a2 = decodeFlags(b.a2_flags);
    r.count = decodeCount(b.count);
    r.dstA2 = cmdDstIsA2(b.cmd);
    r.lfu = cmdLfu(b.cmd);
    r.zmode = cmdZmode(b.cmd);
    return r;
}

const Reg* registers(size_t* count) {
    if (count) *count = sizeof kRegs / sizeof kRegs[0];
    return kRegs;
}

std::vector<RegValue> decodeRegisters(const uint8_t* image) {
    std::vector<RegValue> out;
    for (const Reg& r : kRegs) {
        const uint8_t* p = image + (r.addr - kRegBase);
        RegValue v;
        v.reg = &r;
        if (r.bytes == 8) {
            v.hi = be32(p);
            v.lo = be32(p + 4);
        } else {
            v.lo = be32(p);
        }
        out.push_back(v);
    }
    return out;
}

std::string statusText(unsigned v) {
    std::string s = (v & 1) ? "IDLE" : "BUSY";
    if (v & 2) s += " STOPPED";
    return s;
}

// ---- LogTracker ----------------------------------------------------------------------------------------------

void LogTracker::reset() {
    next_ = 0;
    lost_ = 0;
    rows_.clear();
}

size_t LogTracker::sync(const vj_debug_iface* iface) {
    if (!iface || !iface->blit_log_head || !iface->blit_log_get) return 0;
    const unsigned head = iface->blit_log_head();
    if (head < next_) reset();   // the log was switched off and on again: its numbering restarted
    const unsigned oldest = head > VJ_DBG_BLIT_LOG_SIZE ? head - VJ_DBG_BLIT_LOG_SIZE : 0;
    if (next_ < oldest) {        // launches that fell out of the ring before we looked
        lost_ += oldest - next_;
        next_ = oldest;
    }
    size_t added = 0;
    for (unsigned i = next_; i < head; i++) {
        vj_debug_blit b;
        if (iface->blit_log_get(i, &b)) {
            rows_.push_back(b);
            added++;
        } else {
            lost_++;
        }
    }
    next_ = head;
    while (rows_.size() > max_) rows_.pop_front();
    return added;
}

}  // namespace blit
}  // namespace vj
