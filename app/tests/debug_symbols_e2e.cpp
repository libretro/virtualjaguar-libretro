// The Symbols window's promise end to end (#1195): the REAL core runs a real rln .cof, and a breakpoint set from a symbol's
// value halts there.  What a row's checkbox does, minus the pixels.
//
// usage: vj_debug_symbols_e2e <file.cof>
#include "core_host.h"
#include "debug/debug_session.h"
#include "debug/symbol_table.h"
#include "paths.h"

#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace vj;

static int g_fail = 0, g_checks = 0;
#define CHECK(cond)                                                                    \
    do {                                                                               \
        g_checks++;                                                                    \
        if (!(cond)) {                                                                 \
            g_fail++;                                                                  \
            std::fprintf(stderr, "FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);       \
        }                                                                              \
    } while (0)

static const Symbol* find(const std::vector<Symbol>& v, const char* name) {
    for (const Symbol& s : v)
        if (s.name == name) return &s;
    return nullptr;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: vj_debug_symbols_e2e <file.cof>\n");
        return 2;
    }
    std::vector<unsigned char> bytes;
    std::vector<Symbol> symbols;
    CHECK(readFile(argv[1], bytes));
    CHECK(parseSymbols(bytes.data(), bytes.size(), symbols) == SymbolParse::Ok);
    const Symbol* loop = find(symbols, "main_loop");
    const Symbol* count = find(symbols, "frame_count");
    CHECK(loop && count);
    if (!loop || !count) return 1;

    std::error_code ec;
    fs::path tmp = fs::temp_directory_path(ec);
    if (ec) tmp = ".";
    tmp /= "vj-debug-symbols-e2e-" + std::to_string((long long)std::time(nullptr)) + "-" + std::to_string((long long)clock());
    fs::create_directories(tmp / "system", ec);
    fs::create_directories(tmp / "saves", ec);
    const std::string t = tmp.string() + "/";

    CoreHost& host = CoreHost::instance();
    DebugSession dbg;
    host.onDebugIface = [&dbg](const vj_debug_iface* i) { dbg.setIface(i); };
    host.setDirectories(t + "system/", t + "saves/");
    host.announceDebug = true;
    std::string err;
    if (!host.loadContent(argv[1], err)) {
        std::fprintf(stderr, "FAIL load: %s\n", err.c_str());
        return 1;
    }
    dbg.setContentLoaded(true);
    CHECK(dbg.enabled());

    // The first hit, then the second one around the loop: the program really is running through this label.
    int hits = 0;
    unsigned firstPc = 0, secondPc = 0, firstCount = 0, secondCount = 0;
    dbg.core().runUiFrame = [&] {
        dbg.syncHalt();
        CHECK(dbg.halted() && dbg.haltReason() == VJ_DBG_STOP_BREAKPOINT);
        if (hits == 0) {
            firstPc = dbg.haltedPc();
            dbg.readU(count->value, 4, firstCount);
            dbg.resume();
        } else if (hits == 1) {
            secondPc = dbg.haltedPc();
            dbg.readU(count->value, 4, secondCount);
            dbg.detach();
        }
        hits++;
    };
    CHECK(symbolTarget(loop->value) == VJ_DBG_TARGET_M68K);
    CHECK(symbolCanBreak(loop->value));
    CHECK(dbg.addBreakpoint(symbolTarget(loop->value), loop->value));
    for (int i = 0; i < 300 && hits < 2; i++) {
        host.runFrame();
        dbg.core().frameEnded();
        dbg.syncHalt();
    }
    CHECK(hits == 2);
    CHECK(firstPc == loop->value);
    CHECK(secondPc == loop->value);
    CHECK(secondCount == firstCount + 1);   // the BSS symbol's value is where the loop counts
    CHECK(!dbg.halted() && !host.inFrame());

    host.unloadContent();
    fs::remove_all(tmp, ec);
    std::printf("debug_symbols_e2e: %d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
