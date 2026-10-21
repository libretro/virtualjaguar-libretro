// DebugViews: the request table and per-request double buffers behind the Tools windows (#967 phase 3a).
//
// A window states what it wants to see (a memory range, a disassembly, the OP list ...) with set(), and draws
// what read() returns.  The emulation thread (inline mode: the same thread) calls fill() after each frame, which
// serves every request from the core through the vj_debug_iface.  So no window ever calls the core from the UI
// thread.
//
// Lock discipline: one mutex guards the request table and the front buffers.  fill() copies (id, request, rev)
// out under it, runs the iface calls with NO lock held, builds each answer in a local ViewBuffer, and installs it
// under the lock only if the slot still exists and its rev is unchanged (a set() made meanwhile wins).  set() and
// clear() may be called from a lambda posted to the emulation thread.  A content switch calls bumpEpoch(): every
// buffer filled earlier stops being readable (a window never draws the previous game's bytes).
#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "vj_debug_iface.h"

namespace vj {

struct DisasmLine {
    unsigned addr = 0;
    unsigned len = 0;      // bytes (2, or 6 for MOVEI; the 68000's own lengths through Musashi); 0 = could not disassemble
    std::string text;
    std::string bytes;     // the hex bytes of the instruction (what cpu_view's disasmForward returns), "" when len == 0
};
// A5 injects this: runs on the emulation thread inside fill().  Without one, Disasm serves RISC targets through
// iface->disasm_risc and answers ok = false (no lines) for VJ_DBG_TARGET_M68K.
using DisasmFn = std::function<std::vector<DisasmLine>(const vj_debug_iface*, int target, unsigned addr, unsigned rows)>;

struct ViewRequest {
    enum class Kind : uint8_t { Memory, Dram, Regs, Disasm, OpSnapshot, BlitLog };
    Kind kind = Kind::Memory;
    unsigned addr = 0;                  // Memory: first bus address; Disasm: first instruction
    unsigned len = 0;                   // Memory: bytes (<= 64 KiB per request); Dram: ignored (DebugSession::kDramSize)
    int target = VJ_DBG_TARGET_M68K;    // Regs, Disasm
    unsigned rows = 0;                  // Disasm: instructions wanted (<= 512)
    unsigned since = 0;                 // BlitLog: first launch index wanted (blit::LogTracker's next_)
};

struct ViewBuffer {
    ViewRequest req;                    // the request this buffer answers (a window compares addr / since / target)
    uint64_t serial = 0;                // the refresh serial the fill ran under (0 = never filled)
    uint64_t epoch = 0;                 // DebugViews::epoch() at the fill; a content switch bumps it (read() then fails)
    unsigned frame = 0;                 // iface->frame_number() at the fill
    bool ok = false;                    // the iface answered every call
    std::vector<uint8_t> bytes;         // Memory, Dram (big-endian bus order)
    vj_debug_regs regs{};               // Regs
    std::vector<DisasmLine> lines;      // Disasm
    std::vector<vj_debug_op_obj> objs;  // OpSnapshot (opCount entries)
    int opCount = -1;                   // OpSnapshot: -1 = armed, not taken yet
    unsigned halfline = 0;              // OpSnapshot
    std::vector<vj_debug_blit> blits;   // BlitLog: entries since..head-1 that were still in the ring
    unsigned blitHead = 0;              // BlitLog: iface->blit_log_head() at the fill
    unsigned blitLost = 0;              // BlitLog: entries between `since` and the oldest kept
};

class DebugViews {
public:
    using Id = uint32_t;
    static constexpr Id kNoId = 0;
    static constexpr unsigned kMaxMemoryBytes = 65536;
    static constexpr unsigned kMaxDisasmRows = 512;

    // ---- UI thread (set / clear may also be called from a posted lambda on the emulation thread: one mutex) ----
    // kNoId allocates a new id; an existing id is replaced.  Returns the id.  A request equal to the stored one
    // changes nothing (windows re-state theirs every draw); a different one clears the front buffer (read() fails
    // until the next fill) and is pending at the same serial.
    Id set(Id id, const ViewRequest& r);
    void clear(Id id);                         // no-op for an unknown id
    void setDisasm(DisasmFn fn);               // A5: the CPU tool's disassembler; empty = the built-in RISC-only fill
    // Copies the front buffer.  False (and `out` untouched) when the id is unknown, never filled, or filled in an
    // older epoch.  Cheap: the mutex is held for the copy only.
    bool read(Id id, ViewBuffer& out) const;
    size_t count() const;                      // live requests (tests)

    // ---- emulation thread (inline: the same thread) -------------------------------------------
    // Fills every request whose front buffer carries a serial other than `serial` (or none).  Copies the request
    // table out first; no lock is held while the iface is called.  Returns how many were filled.  A null iface fills
    // nothing.  Fills run in id order.
    size_t fill(const vj_debug_iface* iface, uint64_t serial);
    bool pending(uint64_t serial) const;       // some request is not yet filled at `serial` (the pump asks this)
    void bumpEpoch();                          // a content switch: every buffer becomes unreadable until refilled
    uint64_t epoch() const;                    // atomic

private:
    struct Slot {
        ViewRequest req;
        uint64_t rev = 0;      // bumped by every set() that changes the request; a fill installs only against the rev it copied
        ViewBuffer front;
    };
    static bool equal(const ViewRequest& a, const ViewRequest& b);
    bool stale(const Slot& s, uint64_t serial, uint64_t ep) const { return s.front.serial != serial || s.front.epoch != ep; }
    static void fillOne(const vj_debug_iface* iface, const DisasmFn& dfn, ViewBuffer& b);

    mutable std::mutex mu_;
    std::map<Id, Slot> slots_;
    Id nextId_ = 1;
    DisasmFn disasm_;
    std::atomic<uint64_t> epoch_{1};
};

}  // namespace vj
