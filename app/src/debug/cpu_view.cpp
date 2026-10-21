#include "cpu_view.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <utility>

#include "vj_m68kdasm.h"

namespace vj {
namespace cpuview {

const char* targetName(int t) {
    switch (t) {
    case VJ_DBG_TARGET_M68K: return "68000";
    case VJ_DBG_TARGET_GPU: return "GPU";
    case VJ_DBG_TARGET_DSP: return "DSP";
    default: return "?";
    }
}

const char* stopReasonName(int r) {
    switch (r) {
    case VJ_DBG_STOP_BREAKPOINT: return "breakpoint";
    case VJ_DBG_STOP_STEP: return "step";
    case VJ_DBG_STOP_USER: return "break";
    default: return "";
    }
}

// ---- disassembly ---------------------------------------------------------------------------------------------

namespace {

constexpr unsigned kBus = 0x00FFFFFFu;

std::string hexBytes(const unsigned char* b, unsigned n) {
    std::string s;
    char t[8];
    for (unsigned i = 0; i < n; i++) {
        if (i && (i & 1) == 0) s += ' ';
        std::snprintf(t, sizeof t, "%02X", b[i]);
        s += t;
    }
    return s;
}

// Reads up to n bytes at addr (clipped at the end of the bus); returns how many it got (0 on failure).
// The tail of `dst` past the count is zeroed.
unsigned peekClipped(const vj_debug_iface* iface, unsigned addr, unsigned char* dst, unsigned n) {
    std::memset(dst, 0, n);
    if (!iface || !iface->peek) return 0;
    unsigned avail = addr > kBus ? 0 : kBus + 1 - addr;
    n = std::min(n, avail);
    n &= ~1u;
    while (n >= 2) {
        if (iface->peek(addr, dst, n) == (int)n) return n;
        n -= 2;
    }
    return 0;
}

}  // namespace

std::string stripRiscPrefix(const std::string& text, unsigned len) {
    size_t i = 0;
    if (text.empty() || text[0] != '$') return text;
    size_t colon = text.find(':');
    if (colon == std::string::npos || colon < 2 || colon > 9) return text;
    for (size_t k = 1; k < colon; k++)
        if (!std::isxdigit((unsigned char)text[k])) return text;
    i = colon + 1;
    // Up to len/2 words, but the core prints only the first word of a 6-byte MOVEI: stop at the first token that
    // is not exactly four hex digits.  (A mnemonic such as "addc" is four hex digits, but it only ever follows
    // the single word of a 2-byte instruction, where len/2 is 1, so the cap keeps it.)
    unsigned words = 0;
    for (; words < len / 2; words++) {
        size_t j = i;
        while (j < text.size() && text[j] == ' ') j++;
        size_t k = j;
        for (; k < j + 4 && k < text.size() && std::isxdigit((unsigned char)text[k]); k++) {}
        if (k != j + 4 || (k < text.size() && text[k] != ' ')) break;
        i = k;
    }
    if (words == 0) return text;
    while (i < text.size() && text[i] == ' ') i++;
    return text.substr(i);
}

std::string tidyRiscSpacing(const std::string& text) {
    size_t sp = text.find(' ');
    if (sp == std::string::npos) return text;
    size_t rest = sp;
    while (rest < text.size() && text[rest] == ' ') rest++;
    if (rest >= text.size()) return text.substr(0, sp);
    std::string m = text.substr(0, sp);
    if (m.size() < 8) m.append(8 - m.size(), ' ');
    else m += ' ';
    return m + text.substr(rest);
}

Line disasmOne(const vj_debug_iface* iface, int target, unsigned addr) {
    Line l;
    addr &= kBus & ~1u;
    l.addr = addr;
    l.len = 2;
    l.text = "??";
    if (!iface) return l;
    if (target == VJ_DBG_TARGET_M68K) {
        unsigned char buf[16];
        unsigned got = peekClipped(iface, addr, buf, sizeof buf);
        if (got < 2) return l;
        char out[128];
        l.len = vj_m68k_disasm(buf, addr, out, sizeof out);
        if (l.len > got) l.len = got;   // an instruction cut off by the end of the bus
        l.text = out;
        l.bytes = hexBytes(buf, l.len);
        l.valid = true;
        return l;
    }
    if (isRisc(target) && iface->disasm_risc) {
        char out[128];
        unsigned len = 2;
        if (!iface->disasm_risc(target, addr, out, sizeof out, &len)) return l;
        if (len < 2 || len > 6) len = 2;
        l.len = len;
        l.text = tidyRiscSpacing(stripRiscPrefix(out, len));
        unsigned char buf[8];
        unsigned got = peekClipped(iface, addr, buf, 6);
        l.bytes = hexBytes(buf, std::min(len, got));
        l.valid = true;
    }
    return l;
}

std::vector<Line> disasmForward(const vj_debug_iface* iface, int target, unsigned start, int count) {
    std::vector<Line> v;
    unsigned a = start & kBus & ~1u;
    for (int i = 0; i < count; i++) {
        Line l = disasmOne(iface, target, a);
        v.push_back(l);
        a = (a + l.len) & kBus;
    }
    return v;
}

unsigned nextAddr(const vj_debug_iface* iface, int target, unsigned addr) {
    Line l = disasmOne(iface, target, addr);
    return (l.addr + l.len) & kBus;
}

namespace {

// Backwards sync over variable-length code.  predecessors(a) = every even s in [a - maxLen, a) whose
// instruction ends exactly at a.  depth(a, k) = how many instructions (<= k) can be chained back from a.
struct BackWalker {
    const vj_debug_iface* iface;
    int target;
    unsigned maxLen;
    std::map<unsigned, unsigned> lens;
    std::map<unsigned, bool> zeroOp;   // the instruction's first word is $0000 (ori.b #0,D0): zero-filled memory
    std::map<std::pair<unsigned, int>, int> memo;

    unsigned lenAt(unsigned a) {
        auto it = lens.find(a);
        if (it != lens.end()) return it->second;
        Line l = disasmOne(iface, target, a);
        lens[a] = l.len;
        zeroOp[a] = target == VJ_DBG_TARGET_M68K && l.bytes.compare(0, 4, "0000") == 0;
        return l.len;
    }
    // Nearest first, except that a decode starting with a $0000 word (what an operand of 0 or empty
    // memory reads as) is a last resort: "jsr $00001234.l" must not be mistaken for "ori.b #0,D0" + the rest.
    std::vector<unsigned> preds(unsigned a) {
        std::vector<unsigned> v, late;
        for (unsigned d = 2; d <= maxLen && d <= a; d += 2)
            if (lenAt(a - d) == d) (zeroOp[a - d] ? late : v).push_back(a - d);
        v.insert(v.end(), late.begin(), late.end());
        return v;
    }
    int depth(unsigned a, int k) {
        if (k <= 0) return 0;
        auto key = std::make_pair(a, k);
        auto it = memo.find(key);
        if (it != memo.end()) return it->second;
        int best = 0;
        for (unsigned s : preds(a)) {
            int d = 1 + depth(s, k - 1);
            if (d > best) best = d;
            if (best == k) break;
        }
        memo[key] = best;
        return best;
    }
};

}  // namespace

unsigned backStart(const vj_debug_iface* iface, int target, unsigned pc, int lines) {
    pc &= kBus & ~1u;
    if (lines <= 0 || pc == 0) return pc;
    BackWalker w;
    w.iface = iface;
    w.target = target;
    w.maxLen = target == VJ_DBG_TARGET_M68K ? 10u : 6u;
    int need = w.depth(pc, lines);
    unsigned a = pc;
    while (need > 0) {
        unsigned next = a;
        for (unsigned s : w.preds(a))
            if (w.depth(s, need - 1) >= need - 1) {
                next = s;
                break;
            }
        if (next == a) break;
        a = next;
        need--;
    }
    return a;
}

unsigned prevAddr(const vj_debug_iface* iface, int target, unsigned addr) {
    return backStart(iface, target, addr, 1);
}

std::vector<DisasmLine> disasmRows(const vj_debug_iface* iface, int target, unsigned addr, unsigned rows) {
    std::vector<DisasmLine> out;
    out.reserve(rows);
    for (Line& l : disasmForward(iface, target, addr, (int)rows)) {
        DisasmLine d;
        d.addr = l.addr;
        d.len = l.valid ? l.len : 0;
        d.text = std::move(l.text);
        d.bytes = std::move(l.bytes);
        out.push_back(std::move(d));
    }
    return out;
}

// ---- the window's feed -------------------------------------------------------------------------------------------

void CpuFeed::want(DebugSession& s, int target, unsigned top, unsigned rows) {
    ViewRequest r;
    r.kind = ViewRequest::Kind::Regs;
    r.target = target;
    ViewRequest d;
    d.kind = ViewRequest::Kind::Disasm;
    d.target = target;
    d.addr = top;
    d.rows = rows;
    const bool regsSame = stated_ && regsId_ != DebugViews::kNoId && wantRegs_.target == target;
    const bool disSame = stated_ && disId_ != DebugViews::kNoId && wantDis_.target == target && wantDis_.addr == top &&
                         wantDis_.rows == rows;
    if (!regsSame) regsId_ = s.views().set(regsId_, r);
    if (!disSame) disId_ = s.views().set(disId_, d);
    wantRegs_ = r;
    wantDis_ = d;
    stated_ = true;
}

void CpuFeed::forget() {
    answered_ = haveRegs_ = haveRows_ = false;
    regsTarget_ = rowsTarget_ = -1;
    regsSerial_ = rowsSerial_ = 0;
    rows_.clear();
    std::memset(&regs_, 0, sizeof regs_);
}

bool CpuFeed::update(DebugSession& s) {
    bool changed = false;
    const uint64_t ep = s.views().epoch();
    if (ep != epoch_) {   // a new core / content: nothing adopted before belongs to it
        changed = answered_ || haveRows_;
        forget();
        epoch_ = ep;
    }
    ViewBuffer b;
    if (regsId_ != DebugViews::kNoId && s.views().read(regsId_, b) && b.req.kind == ViewRequest::Kind::Regs) {
        if (!answered_ || b.serial != regsSerial_ || b.req.target != regsTarget_) {
            answered_ = true;
            regsTarget_ = b.req.target;
            regsSerial_ = b.serial;
            haveRegs_ = b.ok;
            if (b.ok) regs_ = b.regs;
            changed = true;
        }
    }
    if (disId_ != DebugViews::kNoId && s.views().read(disId_, b) && b.req.kind == ViewRequest::Kind::Disasm) {
        if (!haveRows_ || b.serial != rowsSerial_ || b.req.target != rowsTarget_ || b.req.addr != rowsTop_ ||
            b.req.rows != rowsN_) {
            haveRows_ = true;
            rowsTarget_ = b.req.target;
            rowsTop_ = b.req.addr;
            rowsN_ = b.req.rows;
            rowsSerial_ = b.serial;
            rows_ = b.ok ? std::move(b.lines) : std::vector<DisasmLine>();
            changed = true;
        }
    }
    return changed;
}

void CpuFeed::release(DebugSession& s) {
    if (regsId_ != DebugViews::kNoId) s.views().clear(regsId_);
    if (disId_ != DebugViews::kNoId) s.views().clear(disId_);
    regsId_ = disId_ = DebugViews::kNoId;
    stated_ = false;
    forget();
}

// ---- step over ------------------------------------------------------------------------------------------------------

StepOver stepOverTarget(const vj_debug_iface* iface, int target, unsigned pc) {
    StepOver r;
    if (target != VJ_DBG_TARGET_M68K || !iface) return r;
    pc &= kBus & ~1u;
    unsigned char b[2];
    if (peekClipped(iface, pc, b, 2) < 2) return r;
    unsigned op = ((unsigned)b[0] << 8) | b[1];
    const char* why = nullptr;
    if ((op & 0xFFC0u) == 0x4E80u) why = "jsr";
    else if ((op & 0xFF00u) == 0x6100u) why = "bsr";
    else if ((op & 0xFFF0u) == 0x4E40u) why = "trap";
    if (!why) return r;
    r.overCall = true;
    r.why = why;
    r.next = nextAddr(iface, target, pc);
    return r;
}

// ---- decoded registers ----------------------------------------------------------------------------------------------

std::vector<Flag> decodeSr(unsigned sr) {
    return {
        {"T", (sr & 0x8000u) != 0, 0x8000u}, {"S", (sr & 0x2000u) != 0, 0x2000u}, {"X", (sr & 0x10u) != 0, 0x10u},
        {"N", (sr & 0x08u) != 0, 0x08u},     {"Z", (sr & 0x04u) != 0, 0x04u},     {"V", (sr & 0x02u) != 0, 0x02u},
        {"C", (sr & 0x01u) != 0, 0x01u},
    };
}

std::string srFlagString(unsigned sr) {
    std::string s;
    for (const Flag& f : decodeSr(sr))
        if ((f.mask & 0x1Fu) && f.set) {
            if (!s.empty()) s += ' ';
            s += f.name;
        }
    return s.empty() ? "-" : s;
}

RiscFlags decodeRiscFlags(unsigned f, bool dsp) {
    RiscFlags r{};
    r.zero = (f & 1u) != 0;
    r.carry = (f & 2u) != 0;
    r.nega = (f & 4u) != 0;
    r.imask = (f & 8u) != 0;
    r.intEnable = (f >> 4) & 0x1Fu;
    r.intClear = (f >> 9) & 0x1Fu;
    if (dsp) {
        if (f & 0x10000u) r.intEnable |= 0x20u;
        if (f & 0x20000u) r.intClear |= 0x20u;
    }
    r.regpage = (f & 0x4000u) != 0;
    r.dmaen = (f & 0x8000u) != 0;
    return r;
}

RiscCtrl decodeRiscCtrl(unsigned c, bool dsp) {
    RiscCtrl r{};
    r.go = (c & 1u) != 0;
    r.singleStep = (c & 8u) != 0;
    r.singleGo = (c & 0x10u) != 0;
    r.latch = (c >> 6) & 0x1Fu;
    if (dsp && (c & 0x10000u)) r.latch |= 0x20u;
    r.busHog = (c & 0x800u) != 0;
    r.version = (c >> 12) & 0xFu;
    return r;
}

std::string intList(unsigned bits, bool dsp) {
    std::string s;
    const int n = dsp ? 6 : 5;
    for (int i = 0; i < n; i++)
        if (bits & (1u << i)) {
            if (!s.empty()) s += ' ';
            s += std::to_string(i);
        }
    return s.empty() ? "none" : s;
}

const char* reg68kName(int reg) {
    static const char* const names[] = {"D0", "D1", "D2", "D3", "D4", "D5", "D6", "D7", "A0", "A1",
                                        "A2", "A3", "A4", "A5", "A6", "A7", "PC", "SR", "USP", "SSP"};
    return reg >= 0 && reg < 20 ? names[reg] : "?";
}

bool parseHex(const char* text, unsigned& out, bool bus24) {
    if (!text) return false;
    while (std::isspace((unsigned char)*text)) text++;
    if (*text == '#') text++;
    if (*text == '$') text++;
    else if (text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) text += 2;
    const char* p = text;
    unsigned long long v = 0;
    int n = 0;
    for (; std::isxdigit((unsigned char)*p); p++, n++) {
        if (n >= 8) return false;
        v = (v << 4) | (unsigned)(std::isdigit((unsigned char)*p) ? *p - '0' : std::tolower((unsigned char)*p) - 'a' + 10);
    }
    if (n == 0) return false;
    while (std::isspace((unsigned char)*p)) p++;
    if (*p) return false;
    out = (unsigned)v;
    if (bus24) out &= kBus;
    return true;
}

// ---- run controls ----------------------------------------------------------------------------------------------------

bool TempBreak::arm(DebugSession& s, int target, unsigned addr) {
    disarm(s);
    if (!s.attach()) return false;
    bool userArmed = false;
    for (const DebugBreakpoint& b : s.breakpoints())
        if (b.target == target && b.addr == addr && b.enabled) userArmed = true;
    if (!userArmed) {
        bool set = false;
        s.callCore([&] { set = s.core().breakSet(target, addr); });
        if (!set) return false;
    }
    active_ = true;
    owned_ = !userArmed;
    target_ = target;
    addr_ = addr;
    return true;
}

void TempBreak::disarm(DebugSession& s) {
    if (active_ && owned_ && s.attached()) s.callCore([&] { s.core().breakClear(target_, addr_); });
    active_ = false;
    owned_ = false;
}

void CpuControl::onHalt(int target, unsigned, int) {
    temp_.disarm(s_);
    if (target >= 0 && target < VJ_DBG_NUM_TARGETS) pending_[(size_t)target] = false;
    note_.clear();
}

void CpuControl::forget() {
    temp_.forget();
    pending_.fill(false);
    note_.clear();
}

bool CpuControl::breakIn(int target) {
    note_.clear();
    if (target < 0 || target >= VJ_DBG_NUM_TARGETS) return false;
    if (!s_.enabled()) {
        note_ = s_.disabledReason();
        return false;
    }
    if (s_.halted()) {
        note_ = "Already halted";
        return false;
    }
    s_.breakNow(target);
    if (!s_.attached()) {
        note_ = "The core refused the debugger";
        return false;
    }
    pending_[(size_t)target] = true;
    return true;
}

bool CpuControl::cont() {
    note_.clear();
    if (!s_.halted()) return false;
    temp_.disarm(s_);
    s_.resume();
    return true;
}

bool CpuControl::stepIn(int tab) {
    note_.clear();
    if (!s_.halted()) {
        note_ = "Not halted";
        return false;
    }
    temp_.disarm(s_);
    if (tab == s_.haltedTarget() || tab < 0 || tab >= VJ_DBG_NUM_TARGETS) {
        s_.step();
        return true;
    }
    // Another processor is the one that is parked: let the machine run until `tab` executes its next instruction.
    bool ok = false;
    s_.callCore([&] {
        ok = s_.core().iface() != nullptr;
        if (ok) s_.core().haltNext(tab);
    });
    if (!ok) return false;
    pending_[(size_t)tab] = true;
    s_.resume();
    return true;
}

bool CpuControl::stepOver(int tab) {
    note_.clear();
    if (!s_.halted()) {
        note_ = "Not halted";
        return false;
    }
    if (tab != s_.haltedTarget()) return stepIn(tab), false;
    if (tab == VJ_DBG_TARGET_M68K) {
        StepOver so;
        bool got = false;
        s_.callCore([&] {   // one round trip: the registers and the decision read the same core state
            vj_debug_regs r;
            if (!s_.core().getRegs(tab, r)) return;
            so = stepOverTarget(s_.core().iface(), tab, r.pc);
            got = true;
        });
        if (got && so.overCall) {
            if (temp_.arm(s_, tab, so.next)) {
                s_.resume();
                return true;
            }
            note_ = "Could not set the temporary breakpoint; stepping instead";
        }
    }
    temp_.disarm(s_);
    s_.step();
    return false;
}

bool CpuControl::runTo(int tab, unsigned addr) {
    note_.clear();
    if (!s_.halted()) {
        note_ = "Not halted";
        return false;
    }
    if (tab < 0 || tab >= VJ_DBG_NUM_TARGETS) return false;
    addr &= kBus;
    if (!temp_.arm(s_, tab, addr)) {
        note_ = "Could not set the temporary breakpoint";
        return false;
    }
    s_.resume();
    return true;
}

}  // namespace cpuview
}  // namespace vj
