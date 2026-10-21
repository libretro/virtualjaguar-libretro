// The CPU debugger's logic, no ImGui (debug tools WP4): disassembly of the 68000 (vendored Musashi, through
// the core's side-effect-free peek) and of the GPU/DSP (the core's own disassembler), the decoded flag and
// control registers, the step-over decision, and the run controls (break / continue / step / step over /
// run to cursor) on top of DebugSession.  cpu_ui.cpp draws it; debug_cpu_test.cpp tests it against the mock.
//
// The logic that reads the core (disasmForward, backStart, stepOverTarget, ...) takes the iface and runs on the
// emulation thread, between retro_run() calls or inside the halt pump.  The window itself never calls it: it draws
// from the published Regs / Disasm view buffers (CpuFeed) and asks for the few synchronous walks as jobs (DisasmNav).
#pragma once

#include <array>
#include <string>
#include <vector>

#include "core_job.h"
#include "debug_session.h"
#include "debug_views.h"
#include "vj_debug_iface.h"

namespace vj {
namespace cpuview {

inline bool isRisc(int target) { return target == VJ_DBG_TARGET_GPU || target == VJ_DBG_TARGET_DSP; }
const char* targetName(int target);   // "68000" / "GPU" / "DSP"
const char* stopReasonName(int reason);

// ---- disassembly -------------------------------------------------------------------------------------------
struct Line {
    unsigned addr = 0;
    unsigned len = 2;          // bytes
    std::string bytes;         // "4E75" / "3C 00 12 34 56 78": the instruction's words in hex
    std::string text;          // "move.w  #$1234, D0"
    bool valid = false;        // false: the bus could not be read / the core refused (text says so)
};

// One instruction at `addr` (even; an odd address is rounded down).  68K: Musashi over peek(); GPU/DSP:
// iface->disasm_risc.  Never touches core state.
Line disasmOne(const vj_debug_iface* iface, int target, unsigned addr);
// The core's RISC disassembler prefixes its text with the address and the instruction words ("$F0319E: 9801 88FF 0000
// movei #...").  The window has columns for those, so Line::text carries only the instruction; this strips the
// prefix (exactly len/2 four-digit words -- a mnemonic such as "addc" is also four hex digits).  Text without the
// prefix is returned unchanged.
std::string stripRiscPrefix(const std::string& text, unsigned len);
// The core pads the mnemonic to 14 columns; collapse that to Musashi's 8 so the operands line up with the 68000 listing.
std::string tidyRiscSpacing(const std::string& text);
// `count` consecutive instructions starting at `start`.
std::vector<Line> disasmForward(const vj_debug_iface* iface, int target, unsigned start, int count);
// The address of the instruction after the one at `addr`.
unsigned nextAddr(const vj_debug_iface* iface, int target, unsigned addr);
// An address from which a linear decode lands exactly on `pc` after at most `lines` instructions (the
// most that fit).  Variable-length code cannot be walked backwards exactly, so this is the usual
// resynchronising guess: try start points back from pc and keep the earliest that lands on it.  Falls back to pc.
unsigned backStart(const vj_debug_iface* iface, int target, unsigned pc, int lines);
// The instruction before `addr` (backStart with one line); `addr` itself when nothing fits.
unsigned prevAddr(const vj_debug_iface* iface, int target, unsigned addr);
// disasmForward mapped to the DebugViews' DisasmLine (len 0 for an invalid line).  This is the DisasmFn the CPU tool
// installs with DebugViews::setDisasm, so the emulation thread builds the window's rows (Musashi for the 68000).
std::vector<DisasmLine> disasmRows(const vj_debug_iface* iface, int target, unsigned addr, unsigned rows);

// ---- what the window draws from ---------------------------------------------------------------------------------
// The CPU window's two view requests (registers and listing of the tab being looked at) and the newest answers it has
// adopted.  Published buffers only: nothing here reaches the core.  UI thread.
class CpuFeed {
public:
    // States the wanted registers and listing (views().set for each, only when changed; windows re-state every draw).
    void want(DebugSession& s, int target, unsigned top, unsigned rows);
    // Adopts the newest buffers; true when the registers or the rows changed.  A new core (the views' epoch) forgets
    // everything adopted first.
    bool update(DebugSession& s);
    void release(DebugSession& s);                       // the window closed: drop both requests and what was adopted
    bool answered() const { return answered_; }          // some Regs buffer has been adopted (ok or not)
    bool haveRegs() const { return haveRegs_; }          // ...and the core returned the registers
    int regsTarget() const { return regsTarget_; }       // the processor the adopted registers belong to (-1: none yet)
    const vj_debug_regs& regs() const { return regs_; }
    // The adopted rows answer exactly this request.
    bool rowsFor(int target, unsigned top, unsigned rows) const {
        return haveRows_ && rowsTarget_ == target && rowsTop_ == top && rowsN_ == rows;
    }
    const std::vector<DisasmLine>& rows() const { return rows_; }

private:
    void forget();
    DebugViews::Id regsId_ = DebugViews::kNoId, disId_ = DebugViews::kNoId;
    ViewRequest wantRegs_, wantDis_;
    bool stated_ = false;                                // wantRegs_ / wantDis_ hold what the views were last given
    uint64_t epoch_ = 0;                                 // the views' epoch the adopted data belongs to
    bool answered_ = false, haveRegs_ = false;
    int regsTarget_ = -1;
    uint64_t regsSerial_ = 0;
    vj_debug_regs regs_{};
    bool haveRows_ = false;
    int rowsTarget_ = -1;
    unsigned rowsTop_ = 0, rowsN_ = 0;
    uint64_t rowsSerial_ = 0;
    std::vector<DisasmLine> rows_;
};

// The synchronous walks (backStart / prevAddr) that used to run on the draw path, moved onto the emulation thread.
// One result at a time: take() gives the newest address; pending() says one is still on its way.
class DisasmNav {
public:
    void back(DebugSession& s, int target, unsigned pc, int lines) {   // result: backStart(iface, target, pc, lines)
        job_.submit(s, [=](const vj_debug_iface* i) { return backStart(i, target, pc, lines); });
    }
    void prev(DebugSession& s, int target, unsigned from, int count) {   // result: prevAddr applied `count` times
        job_.submit(s, [=](const vj_debug_iface* i) {
            unsigned a = from;
            for (int k = 0; k < count; k++) a = prevAddr(i, target, a);
            return a;
        });
    }
    bool take(unsigned& addr) { return job_.take(addr); }
    bool pending() const { return job_.pending(); }

private:
    CoreJob<unsigned> job_;
};

// ---- step over ----------------------------------------------------------------------------------------------------
// 68000: JSR, BSR and TRAP return to the next instruction, so "step over" runs to pc + length.  Anything else is a
// plain step.
//
// GPU/DSP: always a plain step.  The RISCs have no call instruction (a call is MOVE PC,Rn + JUMP), and JUMP/JR have
// ONE delay slot.  Measured on the real core (debug_cpu_e2e, section 7b): the delay slot of a TAKEN jump/jr is run
// inline with it, so it is never a stop of its own -- Step goes from the jump straight to the target, and a
// breakpoint or Run-to-cursor on a delay-slot address does not trigger while the jump is taken.  A conditional jr that
// is NOT taken just falls into the next instruction, which is then an ordinary, visible stop.
struct StepOver {
    bool overCall = false;     // true: run to `next` (temporary breakpoint)
    unsigned next = 0;         // the address after the instruction, when overCall
    std::string why;           // "jsr", "bsr", "trap", ""
};
StepOver stepOverTarget(const vj_debug_iface* iface, int target, unsigned pc);

// ---- decoded registers -------------------------------------------------------------------------------------------
struct Flag {
    const char* name;
    bool set;
    unsigned mask;             // the bit in the register (for toggling)
};
// 68000 SR: X N Z V C (bits 4..0), S (13), T (15), interrupt mask IPL (bits 10..8).
std::vector<Flag> decodeSr(unsigned sr);
inline unsigned srIpl(unsigned sr) { return (sr >> 8) & 7; }
inline bool srSupervisor(unsigned sr) { return (sr & 0x2000u) != 0; }
// "Z N C" style one-liner for the flags that are set, in the 68000's customary X N Z V C order.
std::string srFlagString(unsigned sr);

// G_FLAGS / D_FLAGS (docs/jtrm-gpu-dsp.md "FLAGS Register Layout", JTRM Rev 8 pp.58-59, 108-109).
struct RiscFlags {
    bool zero, carry, nega;
    bool imask;
    bool regpage;              // the FLAGS bit; the bank actually in use is vj_debug_regs.cur_bank (IMASK forces bank 0)
    bool dmaen;
    unsigned intEnable;        // bit n = INT_ENA n (DSP: 0-5; GPU: 0-4)
    unsigned intClear;         // bit n = INT_CLR n (reads 0 on hardware; kept for completeness)
};
RiscFlags decodeRiscFlags(unsigned flags, bool dsp);
// The register bank the processor is really using: REGPAGE, except that IMASK forces bank 0 (JTRM Rev 8 p.59,
// docs/jtrm-gpu-dsp.md).  vj_debug_regs.cur_bank is only the REGPAGE bit, so the window derives this itself.
inline unsigned riscActiveBank(unsigned flags) { return (flags & 8u) ? 0u : ((flags >> 14) & 1u); }

// G_CTRL / D_CTRL (JTRM Rev 8 pp.60-61, 110-111).
struct RiscCtrl {
    bool go;                   // GPUGO / DSPGO: the processor is running
    bool singleStep;           // bit 3
    bool singleGo;             // bit 4
    bool busHog;               // bit 11
    unsigned latch;            // INT_LAT n at bit n (GPU/DSP 0-4 from bits 6-10; DSP 5 from bit 16)
    unsigned version;          // bits 12-15
};
RiscCtrl decodeRiscCtrl(unsigned ctrl, bool dsp);
// "ena: 0 2 4" / "none".  Interrupt numbers only: the per-source names in docs/jtrm-gpu-dsp.md are tagged
// "derived from source", so they are not presented as hardware fact here.
std::string intList(unsigned bits, bool dsp);

// Register numbers for set_reg().
inline int regRiscBank(int bank, int r) { return (bank ? VJ_DBG_REG_RISC_BANK1 : VJ_DBG_REG_RISC_BANK0) + r; }
const char* reg68kName(int reg);   // "D0".."D7","A0".."A7","PC","SR","USP","SSP"

// "$F03000", "0xF03000", "F03000", "#$..", with an optional "+"/"-" offset-free form; false on garbage/empty.
// The value is masked to the 24-bit bus when `bus24`.
bool parseHex(const char* text, unsigned& out, bool bus24 = false);

// ---- run controls -----------------------------------------------------------------------------------------------------
// The temporary breakpoint behind "run to cursor" and "step over a call".  It is not in the session's list:
// it exists until the next halt (any cause), then it is cleared -- unless the user has an enabled breakpoint at
// that very address, which is theirs and stays.
class TempBreak {
public:
    bool arm(DebugSession& s, int target, unsigned addr);   // false: no core / table full
    void disarm(DebugSession& s);                            // clears from the core if we own it; idempotent
    void forget() { active_ = false; }                       // the core dropped everything (detach / unload)
    bool active() const { return active_; }
    int target() const { return target_; }
    unsigned addr() const { return addr_; }

private:
    bool active_ = false;
    bool owned_ = false;
    int target_ = 0;
    unsigned addr_ = 0;
};

class CpuControl {
public:
    explicit CpuControl(DebugSession& s) : s_(s) {}

    // Registered with the session's onHalt / onResume / onForcedClose.
    void onHalt(int target, unsigned pc, int reason);
    void forget();                                          // the tools were switched off

    // Break: stop `target` at its next instruction.  The halt may take a while (a stopped GPU/DSP only halts
    // when it is next started): pending(target) says it is armed.
    bool breakIn(int target);
    bool pending(int target) const { return target >= 0 && target < VJ_DBG_NUM_TARGETS && pending_[(size_t)target]; }
    // Continue every processor (the halted one resumes; a pending run-to is dropped).
    bool cont();
    // Step the processor of the tab being looked at.  If another processor is the one that is halted, this
    // resumes the machine and halts again at `tab`'s next instruction.
    bool stepIn(int tab);
    // Like stepIn, but a 68000 JSR/BSR/TRAP runs to the next instruction.  Returns true when it used a
    // temporary breakpoint; false for a plain step.
    bool stepOver(int tab);
    // Resume and halt at `addr` on `tab`'s processor.
    bool runTo(int tab, unsigned addr);

    const TempBreak& temp() const { return temp_; }
    // The last button's outcome, for the status line ("" when fine).
    const std::string& note() const { return note_; }

private:
    DebugSession& s_;
    TempBreak temp_;
    std::array<bool, VJ_DBG_NUM_TARGETS> pending_{};
    std::string note_;
};

}  // namespace cpuview
}  // namespace vj
