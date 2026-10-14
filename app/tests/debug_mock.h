// A header-only fake of the core's debug interface (src/debug/vj_debug_iface.h), so the
// debug-tool windows and their logic can be developed and unit-tested without a core.
//
//   vj::MockDebugIface mock;                 // installs itself as THE mock (the iface has no
//   session.setIface(mock.iface());          //  context pointer, so there is one at a time)
//   mock.dram()[0x1000] = 0x42;              // poke the image behind the interface
//
// Everything is deterministic.  The mock follows the contract of vj_debug_iface.h exactly:
// the same region table and the same poke policy as src/debug/vjdebug.c, so a test written
// against it holds against the real core.  What it fakes:
//
//   memory    2 MB DRAM, a 64 KB cartridge image (the rest of the cartridge region reads $FF),
//             the 128 KB BIOS window, BUTCH, and the TOM+JERRY window $F00000-$F1FFFF as one
//             vector (so CLUT $F00400, GPU RAM $F03000, DSP RAM $F1B000 are plain bytes).
//   regs      68K d0-d7 = $1000+n, a0-a7 = $2000+n, pc $4000, sr $2700 ...; GPU / DSP banks
//             $100*bank+n, pc $F03000 / $F1B000, current bank 0.  set_reg writes them back.
//   disasm    "fake <target> $<addr>" (2 bytes), or a 6-byte "fake movei" when addr % 8 == 6.
//   OP        a snapshot of 4 objects at $1000: an 8bpp bitmap (16x4 pixels at DRAM $10000, its
//             CLUT in the register window), a 16bpp scaled bitmap, a branch, a stop -- encoded
//             with the real bit layout of docs/jtrm-object-processor.md.
//   blitter   a log ring of 5 launches (the first two older than the ring window when the ring
//             is shrunk with setBlitRingForTest()).
//   halt      simulateHalt() plays the core's GDBHalt loop: it calls the attached host's
//             halt_pump until the verdict is not VJ_DBG_RESUME_NONE.
#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "vj_debug_iface.h"

namespace vj {

class MockDebugIface {
public:
    static constexpr unsigned kDramSize = 0x200000;
    static constexpr unsigned kCartBacked = 0x10000;
    static constexpr unsigned kBiosSize = 0x20000;
    static constexpr unsigned kRegWindowBase = 0xF00000;
    static constexpr unsigned kRegWindowSize = 0x20000;
    static constexpr unsigned kOpListBase = 0x1000;     // where the fake OP snapshot says its list lives
    static constexpr unsigned kBitmapData = 0x10000;    // the 8bpp bitmap's pixels (DRAM)
    static constexpr unsigned kBitmapW = 16, kBitmapH = 4;

    MockDebugIface() {
        dram_.assign(kDramSize, 0);
        cart_.assign(kCartBacked, 0);
        bios_.assign(kBiosSize, 0);
        butch_.assign(0x100, 0);
        regwin_.assign(kRegWindowSize, 0);
        // A recognisable image: DRAM byte i = i & 0xFF, cart = $C0 | (i & 0x3F).
        for (unsigned i = 0; i < kDramSize; i++) dram_[i] = (uint8_t)i;
        for (unsigned i = 0; i < kCartBacked; i++) cart_[i] = (uint8_t)(0xC0 | (i & 0x3F));
        // The 8bpp bitmap: pixel (x,y) = y*16+x, in DRAM.
        for (unsigned y = 0; y < kBitmapH; y++)
            for (unsigned x = 0; x < kBitmapW; x++) dram_[kBitmapData + y * kBitmapW + x] = (uint8_t)(y * kBitmapW + x);
        // Its CLUT (16-bit big-endian entries at $F00400): entry i = RGB565-ish gradient (i << 8 | i).
        for (unsigned i = 0; i < 256; i++) {
            regwin_[0x400 + i * 2] = (uint8_t)i;
            regwin_[0x400 + i * 2 + 1] = (uint8_t)(255 - i);
        }
        for (unsigned i = 0; i < 8; i++) { d_[i] = 0x1000 + i; a_[i] = 0x2000 + i; }
        for (unsigned b = 0; b < 2; b++)
            for (unsigned i = 0; i < 32; i++) { gpuBank_[b][i] = 0x100 * b + i; dspBank_[b][i] = 0x1000 + 0x100 * b + i; }
        buildOpSnapshot();
        buildBlitLog();
        std::memset(&iface_, 0, sizeof iface_);
        iface_.size = sizeof iface_;
        iface_.version = VJ_DEBUG_IFACE_VERSION;
        iface_.region = &MockDebugIface::sRegion;
        iface_.peek = &MockDebugIface::sPeek;
        iface_.poke = &MockDebugIface::sPoke;
        iface_.get_regs = &MockDebugIface::sGetRegs;
        iface_.set_reg = &MockDebugIface::sSetReg;
        iface_.disasm_risc = &MockDebugIface::sDisasm;
        iface_.attach = &MockDebugIface::sAttach;
        iface_.detach = &MockDebugIface::sDetach;
        iface_.break_set = &MockDebugIface::sBreakSet;
        iface_.break_clear = &MockDebugIface::sBreakClear;
        iface_.halt_next = &MockDebugIface::sHaltNext;
        iface_.halted_target = &MockDebugIface::sHaltedTarget;
        iface_.op_snapshot_arm = &MockDebugIface::sOpArm;
        iface_.op_snapshot_get = &MockDebugIface::sOpGet;
        iface_.cry_to_rgb = &MockDebugIface::sCry;
        iface_.blit_log_enable = &MockDebugIface::sBlitEnable;
        iface_.blit_log_head = &MockDebugIface::sBlitHead;
        iface_.blit_log_get = &MockDebugIface::sBlitGet;
        iface_.texreplace_reload = &MockDebugIface::sTexReload;
        iface_.frame_number = &MockDebugIface::sFrame;
        iface_.halt_reason = &MockDebugIface::sHaltReason;
        prev_ = current();
        current() = this;
    }
    ~MockDebugIface() {
        if (current() == this) current() = prev_;
    }
    MockDebugIface(const MockDebugIface&) = delete;
    MockDebugIface& operator=(const MockDebugIface&) = delete;

    const vj_debug_iface* iface() const { return &iface_; }
    static MockDebugIface* active() { return current(); }

    // ---- the image behind the interface ------------------------------------------------------
    std::vector<uint8_t>& dram() { return dram_; }
    std::vector<uint8_t>& regwin() { return regwin_; }
    std::vector<uint8_t>& cart() { return cart_; }

    // ---- observation ---------------------------------------------------------------------------
    bool attached() const { return host_ != nullptr; }
    int attachCalls = 0, detachCalls = 0;
    unsigned texReloads = 0;
    unsigned frame = 1234;
    unsigned pokesAccepted = 0, pokesRefused = 0;
    bool blitLogOn = false;
    unsigned opArms = 0;
    std::vector<std::pair<int, unsigned>> breakpoints;   // (target, addr), in set order
    std::vector<int> haltNexts;                          // targets halt_next() was called for
    bool refuseAttach = false;                           // attach() answers 0
    bool opReady = true;                                 // op_snapshot_get() returns -1 while false

    // Plays the core's halt loop: calls the attached host's halt_pump (as GDBHalt does every
    // iteration) until it returns something other than VJ_DBG_RESUME_NONE.  `script`, if given,
    // is consulted before each pump call so a test can poke the state between iterations.
    // Returns the verdict (or VJ_DBG_RESUME_CONTINUE if no host is attached, as GDBHalt would
    // keep waiting on a socket-less core: tests must not do that).
    int simulateHalt(int target, unsigned pc, int reason = VJ_DBG_STOP_BREAKPOINT, int maxPumps = 100000,
                     int* pumps = nullptr) {
        if (!host_ || !host_->halt_pump) return VJ_DBG_RESUME_CONTINUE;
        haltedTarget_ = target;
        haltReason_ = reason;
        int verdict = VJ_DBG_RESUME_NONE, n = 0;
        while (verdict == VJ_DBG_RESUME_NONE && n < maxPumps) {
            verdict = host_->halt_pump(host_->user, target, pc);
            n++;
        }
        haltedTarget_ = -1;
        haltReason_ = VJ_DBG_STOP_NONE;
        if (pumps) *pumps = n;
        if (verdict == VJ_DBG_RESUME_STEP) haltedStepTarget = target;
        if (verdict == VJ_DBG_RESUME_DETACH) doDetach();
        return verdict;
    }
    int haltedStepTarget = -1;   // the target a STEP verdict asked to single-step

    // Shrinks the blit ring so tests can exercise wrap (default VJ_DBG_BLIT_LOG_SIZE).
    void setBlitRingForTest(unsigned n) { blitRing_ = n; }
    // Adds one more blit launch to the log (as a game's B_CMD write would).
    void addBlit(unsigned cmd, unsigned count) {
        vj_debug_blit b{};
        b.seq = (unsigned)blits_.size();
        b.frame = frame;
        b.cmd = cmd;
        b.count = count;
        blits_.push_back(b);
    }

    // The fixed region table (matches src/debug/vjdebug.c).
    static const vj_debug_region* regionTable(int* n) {
        static const vj_debug_region t[] = {
            {"DRAM", 0x000000, 0x200000, VJ_DBG_REGION_RAM},
            {"Cartridge", 0x800000, 0x5FFF00, VJ_DBG_REGION_ROM},
            {"BUTCH", 0xDFFF00, 0x100, VJ_DBG_REGION_REG | VJ_DBG_REGION_BLOCKED},
            {"BIOS", 0xE00000, 0x20000, VJ_DBG_REGION_ROM},
            {"TOM regs", 0xF00000, 0x400, VJ_DBG_REGION_REG},
            {"CLUT", 0xF00400, 0x400, VJ_DBG_REGION_RAM},
            {"TOM LBUF", 0xF00800, 0x1800, VJ_DBG_REGION_RAM},
            {"GPU/blitter regs", 0xF02000, 0x1000, VJ_DBG_REGION_REG},
            {"GPU RAM", 0xF03000, 0x1000, VJ_DBG_REGION_RAM},
            {"JERRY regs", 0xF10000, 0xB000, VJ_DBG_REGION_REG},
            {"DSP RAM", 0xF1B000, 0x2000, VJ_DBG_REGION_RAM},
        };
        if (n) *n = (int)(sizeof t / sizeof t[0]);
        return t;
    }

private:
    static MockDebugIface*& current() {
        static MockDebugIface* c = nullptr;
        return c;
    }
    static MockDebugIface* M() { return current(); }

    // ---- memory ---------------------------------------------------------------------------------
    uint8_t* ref(unsigned addr) {
        if (addr < kDramSize) return &dram_[addr];
        if (addr >= 0x800000 && addr < 0x800000 + kCartBacked) return &cart_[addr - 0x800000];
        if (addr >= 0xDFFF00 && addr < 0xE00000) return &butch_[addr - 0xDFFF00];
        if (addr >= 0xE00000 && addr < 0xE00000 + kBiosSize) return &bios_[addr - 0xE00000];
        if (addr >= kRegWindowBase && addr < kRegWindowBase + kRegWindowSize) return &regwin_[addr - kRegWindowBase];
        return nullptr;
    }
    static const vj_debug_region* regionOf(unsigned addr) {
        int n = 0;
        const vj_debug_region* t = regionTable(&n);
        for (int i = 0; i < n; i++)
            if (addr >= t[i].start && addr - t[i].start < t[i].len) return &t[i];
        return nullptr;
    }
    // Registers whose write starts or stops something (4 bytes each).
    static bool dangerous(unsigned addr) {
        static const unsigned regs[] = {0xF02238 /*B_CMD*/, 0xF02114 /*G_CTRL*/, 0xF1A114 /*D_CTRL*/,
                                        0xF00020 /*OLP*/,   0xF1A148 /*LTXD*/,   0xF1A14C /*RTXD*/};
        for (unsigned r : regs)
            if (addr >= r && addr < r + 4) return true;
        return false;
    }

    static int sRegion(int idx, vj_debug_region* o) {
        int n = 0;
        const vj_debug_region* t = regionTable(&n);
        if (idx < 0 || idx >= n || !o) return 0;
        *o = t[idx];
        return 1;
    }
    static int sPeek(unsigned addr, unsigned char* dst, unsigned n) {
        MockDebugIface* m = M();
        if (!m || !dst || n == 0 || addr >= 0x1000000u || n > 0x1000000u - addr) return VJ_DBG_ERR_RANGE;
        for (unsigned i = 0; i < n; i++) {
            const uint8_t* p = m->ref(addr + i);
            dst[i] = p ? *p : (uint8_t)0xFF;
        }
        return (int)n;
    }
    static int sPoke(unsigned addr, const unsigned char* src, unsigned n, unsigned flags) {
        MockDebugIface* m = M();
        if (!m) return VJ_DBG_ERR_STATE;
        int rc = m->pokeCheck(addr, n, flags);
        if (!src && rc == (int)n) rc = VJ_DBG_ERR_RANGE;
        if (rc < 0) {
            m->pokesRefused++;
            return rc;
        }
        for (unsigned i = 0; i < n; i++) *m->ref(addr + i) = src[i];
        m->pokesAccepted++;
        return (int)n;
    }
    // The whole-range policy check shared by poke (all-or-nothing).  Returns n or VJ_DBG_ERR_*.
    int pokeCheck(unsigned addr, unsigned n, unsigned flags) {
        if (n == 0 || addr >= 0x1000000u || n > 0x1000000u - addr) return VJ_DBG_ERR_RANGE;
        for (unsigned i = 0; i < n; i++) {
            unsigned a = addr + i;
            const vj_debug_region* r = regionOf(a);
            if (!r) return VJ_DBG_ERR_RANGE;
            if (r->flags & VJ_DBG_REGION_BLOCKED) return VJ_DBG_ERR_POLICY;
            if (r->flags & VJ_DBG_REGION_ROM) return VJ_DBG_ERR_READONLY;
            if (r->flags & VJ_DBG_REGION_REG) {
                if (!(flags & VJ_DBG_POKE_REGS)) return VJ_DBG_ERR_POLICY;
                if (dangerous(a) && !(flags & VJ_DBG_POKE_DANGEROUS)) return VJ_DBG_ERR_POLICY;
            }
            if (!ref(a)) return VJ_DBG_ERR_RANGE;
        }
        return (int)n;
    }

    // ---- registers ---------------------------------------------------------------------------------
    static int sGetRegs(int target, vj_debug_regs* o) {
        MockDebugIface* m = M();
        if (!m || !o || target < 0 || target >= VJ_DBG_NUM_TARGETS) return 0;
        std::memset(o, 0, sizeof *o);
        if (target == VJ_DBG_TARGET_M68K) {
            for (int i = 0; i < 8; i++) { o->d[i] = m->d_[i]; o->a[i] = m->a_[i]; }
            o->pc = m->pc68_;
            o->sr = m->sr_;
            o->usp = m->usp_;
            o->ssp = m->ssp_;
        } else {
            unsigned(*b)[32] = target == VJ_DBG_TARGET_GPU ? m->gpuBank_ : m->dspBank_;
            for (int i = 0; i < 32; i++) { o->bank0[i] = b[0][i]; o->bank1[i] = b[1][i]; }
            o->risc_pc = target == VJ_DBG_TARGET_GPU ? m->gpuPc_ : m->dspPc_;
            o->flags = target == VJ_DBG_TARGET_GPU ? 0x4001 : 0x2003;
            o->ctrl = target == VJ_DBG_TARGET_GPU ? 0x0001 : 0x0001;
            o->cur_bank = 0;
        }
        return 1;
    }
    static int sSetReg(int target, int reg, unsigned v) {
        MockDebugIface* m = M();
        if (!m || reg < 0) return 0;
        if (target == VJ_DBG_TARGET_M68K) {
            if (reg < 8) m->d_[reg] = v;
            else if (reg < 16) m->a_[reg - 8] = v;
            else if (reg == VJ_DBG_REG_68K_PC) m->pc68_ = v;
            else if (reg == VJ_DBG_REG_68K_SR) m->sr_ = v;
            else if (reg == VJ_DBG_REG_68K_USP) m->usp_ = v;
            else if (reg == VJ_DBG_REG_68K_SSP) m->ssp_ = v;
            else return 0;
            return 1;
        }
        if (target == VJ_DBG_TARGET_GPU || target == VJ_DBG_TARGET_DSP) {
            unsigned(*b)[32] = target == VJ_DBG_TARGET_GPU ? m->gpuBank_ : m->dspBank_;
            unsigned& pc = target == VJ_DBG_TARGET_GPU ? m->gpuPc_ : m->dspPc_;
            if (reg < 32) b[0][reg] = v;
            else if (reg < 64) b[1][reg - 32] = v;
            else if (reg == VJ_DBG_REG_RISC_PC) pc = v;
            else if (reg == VJ_DBG_REG_RISC_FLAGS) { /* accepted, not modelled */ }
            else return 0;
            return 1;
        }
        return 0;
    }
    static int sDisasm(int target, unsigned addr, char* out, int max, unsigned* len) {
        if ((target != VJ_DBG_TARGET_GPU && target != VJ_DBG_TARGET_DSP) || !out || max <= 0) return 0;
        unsigned l = (addr % 8 == 6) ? 6 : 2;
        if (l == 6) std::snprintf(out, (size_t)max, "movei #$%08X,r%u", addr, (addr >> 1) & 31);
        else std::snprintf(out, (size_t)max, "fake %s $%06X", target == VJ_DBG_TARGET_GPU ? "gpu" : "dsp", addr);
        if (len) *len = l;
        return 1;
    }

    // ---- attach / breakpoints --------------------------------------------------------------------------
    static int sAttach(const vj_debug_host* h) {
        MockDebugIface* m = M();
        if (!m || !h) return 0;
        m->attachCalls++;
        if (m->refuseAttach || m->host_) return 0;
        m->hostCopy_ = *h;
        m->host_ = &m->hostCopy_;
        return 1;
    }
    static void sDetach() {
        MockDebugIface* m = M();
        if (m) m->doDetach();
    }
    void doDetach() {
        if (host_) detachCalls++;
        host_ = nullptr;
        breakpoints.clear();
    }
    static int sBreakSet(int target, unsigned addr) {
        MockDebugIface* m = M();
        if (!m || target < 0 || target >= VJ_DBG_NUM_TARGETS || !m->host_) return 0;
        for (auto& b : m->breakpoints)
            if (b.first == target && b.second == addr) return 1;
        if (m->breakpoints.size() >= 64) return 0;
        m->breakpoints.emplace_back(target, addr);
        return 1;
    }
    static int sBreakClear(int target, unsigned addr) {
        MockDebugIface* m = M();
        if (!m) return 0;
        m->breakpoints.erase(std::remove(m->breakpoints.begin(), m->breakpoints.end(), std::make_pair(target, addr)),
                             m->breakpoints.end());
        return 1;
    }
    static void sHaltNext(int target) {
        MockDebugIface* m = M();
        if (m) m->haltNexts.push_back(target);
    }
    static int sHaltedTarget() {
        MockDebugIface* m = M();
        return m ? m->haltedTarget_ : -1;
    }
    static int sHaltReason() {
        MockDebugIface* m = M();
        return m ? m->haltReason_ : VJ_DBG_STOP_NONE;
    }

    // ---- OP snapshot -----------------------------------------------------------------------------------------
    static uint64_t opP0(unsigned type, unsigned ypos, unsigned height, unsigned link, unsigned data) {
        return (uint64_t)type | ((uint64_t)ypos << 3) | ((uint64_t)height << 14) | ((uint64_t)(link >> 3) << 24) |
               ((uint64_t)(data >> 3) << 43);
    }
    static uint64_t opP1(int xpos, unsigned depth, unsigned pitch, unsigned dwidth, unsigned iwidth, unsigned index,
                         unsigned flags /*REFLECT=1 RMW=2 TRANS=4 RELEASE=8*/, unsigned firstpix) {
        return (uint64_t)((unsigned)xpos & 0xFFF) | ((uint64_t)depth << 12) | ((uint64_t)pitch << 15) |
               ((uint64_t)dwidth << 18) | ((uint64_t)iwidth << 28) | ((uint64_t)index << 38) |
               ((uint64_t)(flags & 1) << 45) | ((uint64_t)((flags >> 1) & 1) << 46) | ((uint64_t)((flags >> 2) & 1) << 47) |
               ((uint64_t)((flags >> 3) & 1) << 48) | ((uint64_t)firstpix << 49);
    }
    void putObj(unsigned address, unsigned type, unsigned phrases, uint64_t p0, uint64_t p1, uint64_t p2) {
        vj_debug_op_obj o{};
        o.address = address;
        o.type = type;
        o.phrases = phrases;
        o.p0_hi = (unsigned)(p0 >> 32); o.p0_lo = (unsigned)p0;
        o.p1_hi = (unsigned)(p1 >> 32); o.p1_lo = (unsigned)p1;
        o.p2_hi = (unsigned)(p2 >> 32); o.p2_lo = (unsigned)p2;
        op_.push_back(o);
    }
    void buildOpSnapshot() {
        const unsigned L = kOpListBase;
        // 0: 8bpp bitmap, 16x4 at (40, ypos 100), 2 data phrases per line (8 px per phrase), TRANS on
        putObj(L, 0, 2, opP0(0, 100, kBitmapH, L + 0x20, kBitmapData),
               opP1(40, 3, 1, 2, 2, 0, 4, 0), 0);
        // 1: scaled 16bpp bitmap (type 1, 3 phrases), 2x scale, at (-8, 120); data in DRAM $20000
        putObj(L + 0x20, 1, 3, opP0(1, 120, 8, L + 0x40, 0x20000),
               opP1(-8, 4, 1, 4, 4, 0, 0, 0), (uint64_t)0x40 | ((uint64_t)0x40 << 8) | ((uint64_t)0x20 << 16));
        // 2: branch object, type 3 (condition in bits 14-16, YPOS 3-13, link 24-42)
        putObj(L + 0x40, 3, 1, opP0(3, 400, 0, L + 0x48, 0), 0, 0);
        // 3: stop object
        putObj(L + 0x48, 4, 1, (uint64_t)4, 0, 0);
    }
    static void sOpArm() {
        MockDebugIface* m = M();
        if (m) m->opArms++;
    }
    static int sOpGet(vj_debug_op_obj* o, int max, unsigned* halfline) {
        MockDebugIface* m = M();
        if (!m || !m->opReady) return -1;
        if (halfline) *halfline = 41;
        int n = (int)m->op_.size();
        if (o) for (int i = 0; i < n && i < max; i++) o[i] = m->op_[(size_t)i];
        return n < max ? n : max;
    }
    static unsigned sCry(unsigned v16) {
        // Not the real CRY table: a fixed 565 expansion, enough to be deterministic and non-trivial.
        unsigned r = (v16 >> 11) & 31, g = (v16 >> 5) & 63, b = v16 & 31;
        return ((r << 3 | r >> 2) << 16) | ((g << 2 | g >> 4) << 8) | (b << 3 | b >> 2);
    }

    // ---- blitter log ----------------------------------------------------------------------------------------------
    void buildBlitLog() {
        for (unsigned i = 0; i < 5; i++) {
            vj_debug_blit b{};
            b.seq = i;
            b.frame = 1230 + i;
            b.cmd = 0x01800001u + i;
            b.a1_base = 0x10000 + 0x1000 * i; b.a1_flags = 0x00004000; b.a1_pixel = 0; b.a1_step = 0x00010000;
            b.a2_base = 0x20000; b.a2_flags = 0x00004000; b.a2_pixel = 0x00040004; b.a2_step = 0x00010000;
            b.count = 0x00040010;
            b.engine = i & 1;
            blits_.push_back(b);
        }
    }
    static void sBlitEnable(int on) {
        MockDebugIface* m = M();
        if (m) m->blitLogOn = on != 0;
    }
    static unsigned sBlitHead() {
        MockDebugIface* m = M();
        return m ? (unsigned)m->blits_.size() : 0;
    }
    static int sBlitGet(unsigned i, vj_debug_blit* o) {
        MockDebugIface* m = M();
        if (!m || !o) return 0;
        unsigned head = (unsigned)m->blits_.size();
        if (i >= head || head - i > m->blitRing_) return 0;
        *o = m->blits_[i];
        return 1;
    }

    static int sTexReload() {
        MockDebugIface* m = M();
        if (!m) return 0;
        m->texReloads++;
        return 1;
    }
    static unsigned sFrame() {
        MockDebugIface* m = M();
        return m ? m->frame : 0;
    }

    std::vector<uint8_t> dram_, cart_, bios_, butch_, regwin_;
    unsigned d_[8] = {}, a_[8] = {};
    unsigned pc68_ = 0x4000, sr_ = 0x2700, usp_ = 0x1F0000, ssp_ = 0x1FF000;
    unsigned gpuBank_[2][32] = {}, dspBank_[2][32] = {};
    unsigned gpuPc_ = 0xF03000, dspPc_ = 0xF1B000;
    std::vector<vj_debug_op_obj> op_;
    std::vector<vj_debug_blit> blits_;
    unsigned blitRing_ = VJ_DBG_BLIT_LOG_SIZE;
    vj_debug_host hostCopy_{};
    const vj_debug_host* host_ = nullptr;
    int haltedTarget_ = -1;
    int haltReason_ = VJ_DBG_STOP_NONE;
    vj_debug_iface iface_{};
    MockDebugIface* prev_ = nullptr;
};

}  // namespace vj
