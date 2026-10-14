// The CPU debugger end to end: the REAL core through CoreHost, DebugSession and cpuview::CpuControl (no window).
// What the CPU window's buttons do, minus the pixels:
//
//   1. break at the 68000's first instruction (halt_next before the first frame): the halt is deterministic
//      (the same PC on a second load), get_regs agrees with the halt, the disassembler decodes it;
//   2. step 3 instructions: after each, PC advanced by exactly the disassembled length (the wrapper agrees
//      with the real core on instruction lengths), or, for a branch, moved elsewhere;
//   3. a long step trace -> a breakpoint at a PC the trace reaches halts there on a fresh load (determinism);
//   4. run to cursor halts at the cursor and leaves no temporary breakpoint behind;
//   5. step over a JSR/BSR halts at the instruction after it, in one resume;
//   6. a user breakpoint hit inside a stepped-over call wins over the temporary one (which is cleared);
//   7. the GPU: yarc starts it; Break on the GPU tab halts it, its registers/disassembly are real, a breakpoint
//      on its PC halts again on the next visit;
//   8. detach resumes and the game runs on.
//
// usage: vj_debug_cpu_e2e <rom.j64>      (VJ_E2E_VERBOSE=1 prints the trace)
#include "core_host.h"
#include "debug/cpu_view.h"
#include "debug/debug_session.h"
#include "paths.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace vj;
using namespace vj::cpuview;

static int g_fail = 0, g_checks = 0;
static bool g_verbose = false;
#define CHECK(cond)                                                                    \
    do {                                                                               \
        g_checks++;                                                                    \
        if (!(cond)) {                                                                 \
            g_fail++;                                                                  \
            std::fprintf(stderr, "FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);       \
        }                                                                              \
    } while (0)
#define VLOG(...)                          \
    do {                                   \
        if (g_verbose) std::printf(__VA_ARGS__); \
    } while (0)

namespace {

struct Step {
    unsigned pc;
    std::string text;
    unsigned len;
    int reason;
};

class Rig {
public:
    CoreHost& host = CoreHost::instance();
    DebugSession dbg;
    CpuControl ctl{dbg};
    std::string rom;
    std::function<void()> onUi;

    Rig() {
        dbg.onHalt.push_back([this](int t, unsigned pc, int r) { ctl.onHalt(t, pc, r); });
        dbg.onForcedClose.push_back([this](const char*) { ctl.forget(); });
        dbg.runUiFrame = [this] {
            CHECK(dbg.halted() && host.inFrame());
            if (onUi) onUi();
        };
        host.onDebugIface = [this](const vj_debug_iface* i) { dbg.setIface(i); };
    }

    bool load() {
        std::string err;
        if (host.isLoaded()) host.unloadContent();
        host.announceDebug = true;
        if (!host.loadContent(rom, err)) {
            std::fprintf(stderr, "load failed: %s\n", err.c_str());
            return false;
        }
        dbg.setContentLoaded(true);
        ctl.forget();
        return dbg.enabled();
    }

    vj_debug_regs regs(int target) {
        vj_debug_regs r;
        std::memset(&r, 0, sizeof r);
        dbg.iface()->get_regs(target, &r);
        return r;
    }
    // Runs frames until `done()` or the budget is spent.
    bool runUntil(const std::function<bool()>& done, int frames) {
        for (int i = 0; i < frames && !done(); i++) host.runFrame();
        return done();
    }
};

std::string lower(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

bool isControlFlow(const std::string& text) {
    std::string t = lower(text);
    static const char* const m[] = {"bra", "bsr", "jsr", "jmp", "rts", "rte", "rtr", "trap", "db", "bcc", "bcs", "beq", "bne",
                                    "bge", "bgt", "ble", "blt", "bhi", "bls", "bmi", "bpl", "bvc", "bvs", "stop", "reset", "illegal"};
    for (const char* p : m)
        if (t.compare(0, std::strlen(p), p) == 0) return true;
    return false;
}

// Boots `rig` fresh, halts at the 68000's first instruction and steps `n` instructions, recording each halt.
std::vector<Step> traceFromBoot(Rig& rig, int n) {
    std::vector<Step> tr;
    CHECK(rig.load());
    rig.onUi = [&] {
        vj_debug_regs r = rig.regs(VJ_DBG_TARGET_M68K);
        Line l = disasmOne(rig.dbg.iface(), VJ_DBG_TARGET_M68K, r.pc);
        tr.push_back({r.pc, l.text, l.len, rig.dbg.haltReason()});
        if ((int)tr.size() < n) rig.ctl.stepIn(VJ_DBG_TARGET_M68K);
        else rig.dbg.detachAndResume();
    };
    CHECK(rig.ctl.breakIn(VJ_DBG_TARGET_M68K));
    rig.host.runFrame();
    CHECK(!rig.dbg.halted() && !rig.host.inFrame());
    rig.onUi = nullptr;
    return tr;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: vj_debug_cpu_e2e <rom.j64>\n");
        return 2;
    }
    g_verbose = std::getenv("VJ_E2E_VERBOSE") != nullptr;
    std::error_code ec;
    fs::path tmp = fs::temp_directory_path(ec);
    if (ec) tmp = ".";
    tmp /= "vj-debug-cpu-e2e-" + std::to_string((long long)std::time(nullptr)) + "-" + std::to_string((long long)clock());
    fs::create_directories(tmp / "system", ec);
    fs::create_directories(tmp / "saves", ec);
    const std::string t = tmp.string() + "/";

    Rig rig;
    rig.rom = argv[1];
    rig.host.setDirectories(t + "system/", t + "saves/");

    // ---- 1 + 2: break at the first instruction, step ------------------------------------------------------------
    std::vector<Step> tr = traceFromBoot(rig, 60);
    CHECK(tr.size() == 60);
    for (size_t k = 0; k < tr.size() && g_verbose; k++) VLOG("%2zu  $%06X  %-34s len %u  reason %d\n", k, tr[k].pc, tr[k].text.c_str(), tr[k].len, tr[k].reason);
    if (tr.size() == 60) {
        CHECK(tr[0].reason == VJ_DBG_STOP_USER);
        for (size_t k = 1; k < 60; k++) CHECK(tr[k].reason == VJ_DBG_STOP_STEP);
        // the wrapper agrees with the core about instruction lengths
        int checked = 0;
        for (size_t k = 0; k + 1 < 60; k++) {
            if (isControlFlow(tr[k].text)) continue;
            CHECK(tr[k + 1].pc == tr[k].pc + tr[k].len);
            checked++;
        }
        CHECK(checked >= 10);
        for (size_t k = 0; k < 60; k++) CHECK((tr[k].pc & 1) == 0);
    }

    // The entry is deterministic: a second boot halts at the same PC with the same first 3 steps.
    std::vector<Step> tr2 = traceFromBoot(rig, 3);
    CHECK(tr2.size() == 3 && tr.size() >= 3);
    if (tr2.size() == 3 && tr.size() >= 3)
        for (int k = 0; k < 3; k++) CHECK(tr2[(size_t)k].pc == tr[(size_t)k].pc);

    // The first call (JSR/BSR) in the trace: used for step over below.
    size_t callIdx = tr.size();
    for (size_t k = 0; k < tr.size(); k++) {
        std::string x = lower(tr[k].text);
        if (x.compare(0, 3, "bsr") == 0 || x.compare(0, 3, "jsr") == 0) {
            callIdx = k;
            break;
        }
    }
    CHECK(callIdx + 1 < tr.size());
    if (tr.size() < 60 || callIdx + 1 >= tr.size()) {
        std::printf("debug_cpu_e2e: %d checks, %d failed (trace too short, stopping)\n", g_checks, g_fail + 1);
        return 1;
    }

    // ---- 3: a breakpoint at a PC the trace reaches halts there on a fresh load ---------------------------------------
    {
        const Step& want = tr[30];
        CHECK(rig.load());
        std::vector<Step> halts;
        rig.onUi = [&] {
            vj_debug_regs r = rig.regs(VJ_DBG_TARGET_M68K);
            halts.push_back({r.pc, "", 0, rig.dbg.haltReason()});
            rig.ctl.cont();
        };
        CHECK(rig.dbg.addBreakpoint(VJ_DBG_TARGET_M68K, want.pc));
        CHECK(!rig.dbg.addBreakpoint(VJ_DBG_TARGET_M68K, want.pc));
        for (int f = 0; f < 3; f++) rig.host.runFrame();
        CHECK(!halts.empty());
        if (!halts.empty()) {
            CHECK(halts[0].pc == want.pc && halts[0].reason == VJ_DBG_STOP_BREAKPOINT);
            CHECK(rig.dbg.haltedTarget() == -1);
        }
        // Disabled: the core no longer stops there.
        rig.dbg.setBreakpointEnabled(VJ_DBG_TARGET_M68K, want.pc, false);
        rig.load();
        halts.clear();
        for (int f = 0; f < 3; f++) rig.host.runFrame();
        CHECK(halts.empty());
        rig.dbg.clearBreakpoints();
        rig.onUi = nullptr;
    }

    // ---- 4: run to cursor -------------------------------------------------------------------------------------------
    {
        const Step& cursor = tr[40];
        CHECK(rig.load());
        std::vector<Step> halts;
        rig.onUi = [&] {
            vj_debug_regs r = rig.regs(VJ_DBG_TARGET_M68K);
            halts.push_back({r.pc, "", 0, rig.dbg.haltReason()});
            if (halts.size() == 1) CHECK(rig.ctl.runTo(VJ_DBG_TARGET_M68K, cursor.pc));
            else rig.ctl.cont();
        };
        CHECK(rig.ctl.breakIn(VJ_DBG_TARGET_M68K));
        for (int f = 0; f < 3; f++) rig.host.runFrame();
        CHECK(halts.size() == 2);
        if (halts.size() == 2) {
            CHECK(halts[0].pc == tr[0].pc && halts[0].reason == VJ_DBG_STOP_USER);
            CHECK(halts[1].pc == cursor.pc && halts[1].reason == VJ_DBG_STOP_BREAKPOINT);
        }
        CHECK(!rig.ctl.temp().active());
        // Continue ran on: the temporary breakpoint is gone, so nothing halts again at the cursor.
        CHECK(halts.size() == 2);
        rig.onUi = nullptr;
    }

    // ---- 5 + 6: step over a call ----------------------------------------------------------------------------------------
    {
        const Step& call = tr[callIdx];
        const unsigned after = call.pc + call.len;
        CHECK(rig.load());
        CHECK(rig.dbg.addBreakpoint(VJ_DBG_TARGET_M68K, call.pc));
        std::vector<Step> halts;
        bool usedTemp = false;
        rig.onUi = [&] {
            vj_debug_regs r = rig.regs(VJ_DBG_TARGET_M68K);
            halts.push_back({r.pc, "", 0, rig.dbg.haltReason()});
            if (halts.size() == 1) {
                CHECK(stepOverTarget(rig.dbg.iface(), VJ_DBG_TARGET_M68K, r.pc).next == after);
                usedTemp = rig.ctl.stepOver(VJ_DBG_TARGET_M68K);
            } else {
                rig.ctl.cont();
            }
        };
        for (int f = 0; f < 3; f++) rig.host.runFrame();
        CHECK(usedTemp);
        CHECK(halts.size() == 2);   // the callee's instructions did not halt one by one
        if (halts.size() == 2) {
            CHECK(halts[0].pc == call.pc);
            CHECK(halts[1].pc == after && halts[1].reason == VJ_DBG_STOP_BREAKPOINT);
        }
        CHECK(!rig.ctl.temp().active());

        // A user breakpoint inside the call wins over the temporary one, which is cleared by that halt.
        const Step& inside = tr[callIdx + 1];
        CHECK(inside.pc != after);
        CHECK(rig.load());
        CHECK(rig.dbg.addBreakpoint(VJ_DBG_TARGET_M68K, call.pc));
        CHECK(rig.dbg.addBreakpoint(VJ_DBG_TARGET_M68K, inside.pc));
        halts.clear();
        rig.onUi = [&] {
            vj_debug_regs r = rig.regs(VJ_DBG_TARGET_M68K);
            halts.push_back({r.pc, "", 0, rig.dbg.haltReason()});
            if (halts.size() == 1) rig.ctl.stepOver(VJ_DBG_TARGET_M68K);
            else rig.ctl.cont();
        };
        for (int f = 0; f < 3; f++) rig.host.runFrame();
        CHECK(halts.size() >= 2);                         // (the callee is entered again later: more halts follow)
        if (halts.size() >= 2) CHECK(halts[1].pc == inside.pc);
        CHECK(!rig.ctl.temp().active());
        for (const Step& h : halts) CHECK(h.pc != after);   // and `after` was never a stop (the temporary breakpoint went)
        rig.dbg.clearBreakpoints();
        rig.onUi = nullptr;
    }

    // ---- 7: the GPU -------------------------------------------------------------------------------------------------
    {
        CHECK(rig.load());
        for (int f = 0; f < 30; f++) rig.host.runFrame();
        vj_debug_regs gr = rig.regs(VJ_DBG_TARGET_GPU);
        CHECK((gr.ctrl & 1u) != 0);                       // yarc starts the GPU...
        CHECK(decodeRiscCtrl(gr.ctrl, false).go);
        CHECK(gr.risc_pc >= 0xF03000 && gr.risc_pc < 0xF04000);
        vj_debug_regs dr = rig.regs(VJ_DBG_TARGET_DSP);
        CHECK(dr.cur_bank <= 1);

        // Its OP interrupt vector ($F03030) runs once per frame, deterministically: a breakpoint there halts the
        // GPU (target GPU, reason breakpoint) every frame; the registers show the interrupt state; step walks the ISR.
        const unsigned vec = 0xF03030;
        CHECK(rig.dbg.addBreakpoint(VJ_DBG_TARGET_GPU, vec));
        std::vector<Step> gh;
        std::vector<unsigned> isrPcs;
        rig.onUi = [&] {
            vj_debug_regs r = rig.regs(VJ_DBG_TARGET_GPU);
            Line l = disasmOne(rig.dbg.iface(), VJ_DBG_TARGET_GPU, r.risc_pc);
            CHECK(rig.dbg.haltedTarget() == VJ_DBG_TARGET_GPU && r.risc_pc == rig.dbg.haltedPc());
            if (gh.empty()) {
                CHECK(rig.dbg.haltReason() == VJ_DBG_STOP_BREAKPOINT);
                CHECK(r.risc_pc == vec);
                RiscFlags f = decodeRiscFlags(r.flags, false);
                CHECK(f.imask && riscActiveBank(r.flags) == 0);   // interrupt entry: IMASK set, bank 0 forced
                CHECK(l.valid && l.len == 2 && l.text.compare(0, 4, "load") == 0);   // the address/word prefix is stripped
            }
            gh.push_back({r.risc_pc, l.text, l.len, rig.dbg.haltReason()});
            isrPcs.push_back(r.risc_pc);
            if (gh.size() == 1) {
                CHECK(rig.ctl.stepIn(VJ_DBG_TARGET_GPU));
            } else if (gh.size() < 14) {
                CHECK(rig.ctl.stepIn(VJ_DBG_TARGET_GPU));
            } else {
                rig.ctl.cont();
            }
        };
        unsigned f0 = (unsigned)rig.host.frameCount();
        for (int f = 0; f < 3 && gh.size() < 14; f++) rig.host.runFrame();
        CHECK(gh.size() >= 14);
        for (size_t k = 0; k < gh.size() && g_verbose; k++) VLOG("GPU %2zu $%06X %-44s len %u reason %d\n", k, gh[k].pc, gh[k].text.c_str(), gh[k].len, gh[k].reason);
        for (const Step& h : gh) CHECK(!h.text.empty() && h.text[0] != '$');   // prefix stripped, incl. the 6-byte movei
        bool sawMovei = false;
        for (const Step& h : gh) sawMovei |= (h.text.compare(0, 5, "movei") == 0 && h.len == 6);
        CHECK(sawMovei);
        // step: PC advances by the disassembled length, except across a jump/jr (the delay slot runs as part of it).
        int adv = 0;
        for (size_t k = 0; k + 1 < gh.size(); k++) {
            std::string x = gh[k].text;
            bool branch = x.find(" jr ") != std::string::npos || x.find(" jump ") != std::string::npos ||
                          x.find("jr  ") != std::string::npos || x.find("jump") != std::string::npos;
            if (!branch) {
                CHECK(gh[k + 1].pc == gh[k].pc + gh[k].len);
                adv++;
            }
        }
        CHECK(adv >= 3);
        if (gh.size() > 1) CHECK(gh[1].reason == VJ_DBG_STOP_STEP);
        (void)f0;
        // A GPU Break (halt_next) on the next frames: the session shows it pending until it lands.
        rig.dbg.clearBreakpoints();
        gh.clear();
        rig.onUi = [&] {
            vj_debug_regs r = rig.regs(VJ_DBG_TARGET_GPU);
            gh.push_back({r.risc_pc, "", 0, rig.dbg.haltReason()});
            CHECK(rig.dbg.haltedTarget() == VJ_DBG_TARGET_GPU && !rig.ctl.pending(VJ_DBG_TARGET_GPU));
            rig.ctl.cont();
        };
        CHECK(rig.ctl.breakIn(VJ_DBG_TARGET_GPU));
        CHECK(rig.ctl.pending(VJ_DBG_TARGET_GPU));
        rig.host.runFrame();
        CHECK(gh.size() == 1 && gh[0].reason == VJ_DBG_STOP_USER);

        // "Step the GPU" while the 68000 is the halted one: resumes, and halts on the GPU's next instruction.
        gh.clear();
        std::vector<int> who;
        rig.onUi = [&] {
            who.push_back(rig.dbg.haltedTarget());
            if (who.size() == 1) CHECK(rig.ctl.stepIn(VJ_DBG_TARGET_GPU));   // halted on the 68000 -> asks for the GPU
            else rig.ctl.cont();
        };
        CHECK(rig.ctl.breakIn(VJ_DBG_TARGET_M68K));
        rig.host.runFrame();
        CHECK(who.size() == 2 && who[0] == VJ_DBG_TARGET_M68K && who[1] == VJ_DBG_TARGET_GPU);
        rig.onUi = nullptr;
    }

    // ---- 8: detach resumes and the game runs on ------------------------------------------------------------------------
    {
        CHECK(rig.load());
        int ui = 0;
        rig.onUi = [&] {
            ui++;
            rig.dbg.detachAndResume();
        };
        CHECK(rig.ctl.breakIn(VJ_DBG_TARGET_M68K));
        rig.host.runFrame();
        CHECK(ui == 1 && !rig.dbg.halted() && !rig.dbg.attached());
        for (int f = 0; f < 20; f++) rig.host.runFrame();
        CHECK(ui == 1);
        rig.onUi = nullptr;
        // and a fresh attach works afterwards
        CHECK(rig.dbg.attach());
    }

    std::filesystem::remove_all(tmp, ec);
    std::printf("debug_cpu_e2e: %d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
