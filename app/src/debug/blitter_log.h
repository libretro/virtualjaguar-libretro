// Blitter inspector logic (pure): decoding of B_CMD / A1,A2_FLAGS / pixel pointers / B_COUNT exactly as
// docs/jtrm-blitter.md documents them (JTRM Rev 8 pp.66-75), the live register table, and the tracker
// that turns the core's 256-entry launch ring into a stable local list without losing or duplicating
// launches across wraps.  No ImGui, no core.
#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

#include "vj_debug_iface.h"

namespace vj {
namespace blit {

// ---- B_CMD ($F02238) ---------------------------------------------------------------------------------
struct CmdBit {
    unsigned mask;
    const char* name;
    const char* tip;
};
// The single-bit controls, in bit order (the LFU nibble and ZMODE field are decoded separately).
const CmdBit* cmdBits(size_t* count);
// Names of the set single-bit controls, bit order ("SRCEN", "DSTEN", "UPDA1", ...).
std::vector<std::string> cmdFlagNames(unsigned cmd);
unsigned cmdLfu(unsigned cmd);          // bits 21-24 (a 4 bit minterm mask)
const char* lfuName(unsigned lfu);      // "S", "D", "S|D", "~S&~D" ... per the JTRM table
unsigned cmdZmode(unsigned cmd);        // bits 18-20
std::string zmodeName(unsigned zmode);  // "off" or "S<D|S=D|S>D" inhibit conditions
// One line: "SRCEN UPDA1 UPDA2 LFU=S" (flags, then LFU, then ZMODE if set).
std::string cmdSummary(unsigned cmd);
bool cmdDstIsA2(unsigned cmd);          // DSTA2 (bit 11)

// ---- A1_FLAGS ($F02204) / A2_FLAGS ($F02228) -----------------------------------------------------------
struct Channel {
    unsigned pitchField = 0;   // bits 0-1
    unsigned pitch = 1;        // phrases between successive phrases of pixel data: {1,2,4,3}[pitchField]
    unsigned pixelField = 0;   // bits 3-5
    unsigned bpp = 0;          // 1,2,4,8,16,32; 0 = reserved encoding
    unsigned zoffs = 0;        // bits 6-8
    unsigned widthField = 0;   // bits 9-14 (6 bit float)
    unsigned width = 0;        // window width in pixels
    unsigned xAddCtl = 0;      // bits 16-17: 0 phrase, 1 +pixel size, 2 +0, 3 +increment
    unsigned yAddCtl = 0;      // bit 18
    bool xSub = false;         // bit 19
    bool ySub = false;         // bit 20
};
Channel decodeFlags(unsigned flags);
unsigned windowWidth(unsigned widthField);        // ((4|M) << E) >> 2 for the 6-bit float
std::string describeChannel(const Channel& c);    // "16bpp  w=320  pitch 1  x:+pixel  y:+1"
const char* xAddName(unsigned ctl);

// ---- pointers / counts -----------------------------------------------------------------------------------
// A1_PIXEL / A2_PIXEL / A1_STEP / A2_STEP: Y in the high 16 bits, X in the low 16 (both signed).
int pixelX(unsigned v);
int pixelY(unsigned v);
struct Count {
    unsigned inner = 0, outer = 0;   // B_COUNT, a field of 0 meaning 65536
};
Count decodeCount(unsigned bcount);
const char* engineName(unsigned engine);   // "fast" / "accurate"

// A launch with its derived facts, for the table and the detail pane.
struct Row {
    vj_debug_blit raw{};
    Channel a1, a2;
    Count count;
    bool dstA2 = false;
    unsigned lfu = 0;
    unsigned zmode = 0;
};
Row decodeRow(const vj_debug_blit& b);

// ---- live registers ------------------------------------------------------------------------------------------
struct Reg {
    const char* name;
    unsigned addr;
    unsigned bytes;   // 4, or 8 for the 64-bit data registers
};
constexpr unsigned kRegBase = 0xF02200, kRegBytes = 0x9C;   // A1_BASE .. B_Z0 (the last register ends at $F0229C)
const Reg* registers(size_t* count);
// A register's value(s) from an image of the window; a 64-bit register is hi:lo.
struct RegValue {
    const Reg* reg = nullptr;
    unsigned hi = 0, lo = 0;   // 32-bit registers use lo only
};
std::vector<RegValue> decodeRegisters(const uint8_t* image /* kRegBytes at kRegBase */);
// B_CMD READS as the status register: bit 0 IDLE, bit 1 STOPPED (JTRM Rev 8 p.75).
std::string statusText(unsigned bcmdRead);

// ---- the launch ring ------------------------------------------------------------------------------------------------
// Follows blit_log_head()/blit_log_get(): each sync() fetches the launches since the previous one.  A
// launch that scrolled out of the core's ring before it was read is counted in lost(), never replayed.
class LogTracker {
public:
    explicit LogTracker(size_t maxRows = VJ_DBG_BLIT_LOG_SIZE) : max_(maxRows ? maxRows : 1) {}
    size_t sync(const vj_debug_iface* iface);   // returns the number of new rows
    void reset();
    const std::deque<vj_debug_blit>& rows() const { return rows_; }   // oldest first
    unsigned total() const { return next_; }    // launches seen by the core since the log was enabled
    unsigned lost() const { return lost_; }
private:
    size_t max_;
    unsigned next_ = 0;
    unsigned lost_ = 0;
    std::deque<vj_debug_blit> rows_;
};

}  // namespace blit
}  // namespace vj
