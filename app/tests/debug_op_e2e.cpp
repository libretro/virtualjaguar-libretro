// The Object list tool against the REAL core (no window): arm the one-shot display-list snapshot, run a
// frame, decode what the Object Processor walked, render a bitmap from DRAM, export it, paint a pixel back.
//
// usage: vj_debug_op_e2e <rom.j64>
#include "core_host.h"
#include "debug/debug_session.h"
#include "debug/op_view.h"
#include "paths.h"

#include <SDL3/SDL.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
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

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: vj_debug_op_e2e <rom.j64>\n");
        return 2;
    }
    const std::string rom = argv[1];
    std::error_code ec;
    fs::path tmp = fs::temp_directory_path(ec);
    if (ec) tmp = ".";
    tmp /= "vj-debug-op-e2e-" + std::to_string((long long)std::time(nullptr)) + "-" + std::to_string((long long)clock());
    fs::create_directories(tmp / "system", ec);
    fs::create_directories(tmp / "saves", ec);
    const std::string t = tmp.string() + "/";

    CoreHost& host = CoreHost::instance();
    host.setDirectories(t + "system/", t + "saves/");
    std::string err;
    DebugSession dbg;
    host.onDebugIface = [&](const vj_debug_iface* i) { dbg.setIface(i); };
    host.announceDebug = true;
    CHECK(host.loadContent(rom, err));
    dbg.setContentLoaded(true);
    CHECK(dbg.enabled());
    for (int i = 0; i < 60; i++) host.runFrame();

    OpViewModel vm;
    vm.update(dbg);                                   // first update arms the snapshot
    CHECK(vm.waiting() && !vm.haveSnapshot());
    host.runFrame();                                  // the walk happens during this frame
    vm.update(dbg);
    CHECK(!vm.waiting() && vm.haveSnapshot());
    const std::vector<OpObject>& objs = vm.objects();
    CHECK(objs.size() >= 1);
    std::printf("snapshot: %zu objects on half-line %u\n", objs.size(), vm.halfline());
    int bitmaps = 0, drawable = 0;
    int pick = -1;
    for (size_t i = 0; i < objs.size(); i++) {
        const OpObject& o = objs[i];
        std::printf("  %2zu %s\n", i, opSummary(o).c_str());
        CHECK(o.type != OpType::Unknown);
        if (o.isBitmap()) {
            bitmaps++;
            CHECK(o.havePhrase1);
            if (o.drawable()) {
                drawable++;
                if (pick < 0 && o.data < DebugSession::kDramSize) pick = (int)i;
            }
        }
    }
    bool hasStop = false;
    for (const OpObject& o : objs) hasStop |= o.type == OpType::Stop;
    CHECK(hasStop);                                   // a well-formed list ends in a stop object
    // The core captures the WHOLE list at the top of the field, so the bitmaps come from the snapshot itself:
    // none may be a fromRam (link-following fallback) object.
    for (const OpObject& o : objs) CHECK(!o.fromRam);
    CHECK(vm.snapshotCount() == objs.size());
    CHECK(bitmaps >= 1 && drawable >= 1);

    if (pick >= 0) {
        vm.select(pick);
        vm.update(dbg);
        const OpImage* img = vm.image();
        CHECK(img != nullptr);
        if (img) {
            std::printf("rendered object %d: %dx%d %s\n", pick, img->w, img->h, img->note.c_str());
            CHECK(img->w > 0 && img->h > 0);
            // The pixels are what DRAM holds: the first pixel's stored value is the first data byte(s).
            const OpObject& o = objs[(size_t)pick];
            std::vector<uint8_t> b;
            CHECK(dbg.read(o.data, b, 8));
            if (o.bpp == 8 && !img->mirrored && o.skipPixels == 0) CHECK(img->rawAt(0, 0) == b[0]);
            // Export it and read it back.
            fs::path png = tmp / "object.png";
            CHECK(opSavePng(*img, png.string(), err));
            SDL_Surface* s = SDL_LoadPNG(png.string().c_str());
            CHECK(s != nullptr && s->w == img->w && s->h == img->h);
            if (s) SDL_DestroySurface(s);
            if (const char* keep = std::getenv("VJ_OP_E2E_PNG")) {   // look at it: VJ_OP_E2E_PNG=/path/prefix
                for (size_t i = 0; i < objs.size(); i++) {
                    OpImage im;
                    std::string e2;
                    if (objs[i].drawable() && opRender(dbg.core().iface(), objs[i], OpRenderOptions(), im, e2))
                        opSavePng(im, std::string(keep) + std::to_string(i) + ".png", e2);
                }
            }
            // Paint one pixel back into main RAM and see it land.
            uint32_t cur = img->rawAt(0, 0);
            uint32_t want = (cur + 1) & opPixelMax(o);
            std::string st;
            CHECK(vm.paint(dbg, 0, 0, want, st));
            CHECK(vm.image() && vm.image()->rawAt(0, 0) == want);
            uint8_t* core = nullptr;
            size_t coreSize = 0;
            CHECK(host.memoryRegion(RETRO_MEMORY_SYSTEM_RAM, core, coreSize));
            OpPixelLoc L = opPixelLocation(o, *vm.image(), 0, 0);
            CHECK(L.ok && core && L.addr < coreSize);
            if (L.ok && core && L.addr < coreSize && L.bpp == 8) CHECK(core[L.addr] == want);
        }
        // The snapshot is the list BEFORE write-back: it is still the same objects after more frames.
        for (int i = 0; i < 3; i++) host.runFrame();
        vm.arm(dbg);
        host.runFrame();
        vm.update(dbg);
        CHECK(vm.haveSnapshot() && vm.objects().size() >= 1);
    }

    host.unloadContent();
    fs::remove_all(tmp, ec);
    std::printf("debug_op_e2e: %d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
