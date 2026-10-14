// BIOS choice end to end: the REAL core, through CoreHost, with the app's side of the private BIOS channel
// (VJ_ENV_GET_BIOS_PATH / VJ_ENV_GET_BOOT_TARGET, src/core/vj_frontend_ext.h).  The real BIOS images cannot be
// committed, so the files handed to the core are synthetic: a 256 KiB image with a plausible run address at $404
// is accepted as a CD BIOS, any 128 KiB image as a boot ROM.  What is checked is WHICH image the core ran, read
// from the core's own report (VJ_EVT_BIOS_STAGED): source (built in / system folder / the file named), the
// checksum of the image, and that a file the core cannot accept is refused with the built-in image taking over.
//
// usage: vj_bios_e2e <rom.j64>
#include "core_host.h"
#include "paths.h"

#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
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

static uint32_t crc32Of(const std::vector<unsigned char>& v) {
    uint32_t c = 0xFFFFFFFFu;
    for (unsigned char b : v) {
        c ^= b;
        for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
    }
    return ~c;
}

static std::vector<unsigned char> image(size_t n, bool cd, unsigned seed) {
    std::vector<unsigned char> v(n);
    for (size_t i = 0; i < n; i++) v[i] = (unsigned char)((i * (seed * 2 + 1) + 13) ^ (i >> 8));
    if (cd) {
        v[0x404] = 0x00; v[0x405] = 0x80; v[0x406] = 0x20; v[0x407] = 0x00;   // run address $802000
    }
    return v;
}

static std::string write(const std::string& path, const std::vector<unsigned char>& v) {
    std::ofstream f(path, std::ios::binary);
    f.write((const char*)v.data(), (std::streamsize)v.size());
    return path;
}

static bool frameVaries(const Frame& f) {
    for (size_t i = 1; i < f.pixels.size(); i++)
        if ((f.pixels[i] & 0xFFFFFF) != (f.pixels[0] & 0xFFFFFF)) return true;
    return false;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: vj_bios_e2e <rom.j64>\n");
        return 2;
    }
    const std::string rom = argv[1];
    std::error_code ec;
    fs::path tmp = fs::temp_directory_path(ec);
    if (ec) tmp = ".";
    tmp /= "vj-bios-e2e-" + std::to_string((long long)std::time(nullptr)) + "-" + std::to_string((long long)clock());
    fs::create_directories(tmp / "system", ec);
    fs::create_directories(tmp / "saves", ec);
    const std::string t = tmp.string() + "/";

    CoreHost& host = CoreHost::instance();
    host.setDirectories(t + "system/", t + "saves/");   // empty: no real BIOS can be picked up by the core's own search

    const std::vector<unsigned char> cdImg = image(0x40000, true, 1), bootImg = image(0x20000, false, 2);
    const std::string cdFile = write(t + "my cd bios.bin", cdImg), bootFile = write(t + "my boot rom.rom", bootImg);
    const std::string shortFile = write(t + "short.bin", image(1000, false, 3));
    const uint32_t cdCrc = crc32Of(cdImg), bootCrc = crc32Of(bootImg);
    std::string err;
    const unsigned CD = VJ_BIOS_CD, CART = VJ_BIOS_CART_BOOT;

    // 1. nothing chosen (Auto): the built-in images; the CD BIOS is what a no-content boot enters.
    host.setBiosPath(CD, "");
    host.setBiosPath(CART, "");
    host.setBootTarget(VJ_BOOT_CD_BIOS);
    CHECK(host.loadNoContent(err));
    CHECK(host.stagedBios(CD).seen && host.stagedBios(CD).source == VJ_BIOS_SRC_BUILTIN);
    CHECK(host.stagedBios(CD).crc == 0x687068D5u);   // the built-in retail CD BIOS
    CHECK(!host.stagedBios(CD).rejected);
    for (int i = 0; i < 90; i++) host.runFrame();
    CHECK(frameVaries(host.frame()));                // the CD BIOS draws something

    // 2. a CD BIOS file chosen: exactly that image runs, under any file name.
    host.setBiosPath(CD, cdFile);
    CHECK(host.loadNoContent(err));
    CHECK(host.stagedBios(CD).source == VJ_BIOS_SRC_FRONTEND && host.stagedBios(CD).crc == cdCrc);
    CHECK(host.stagedBios(CD).where == cdFile);
    CHECK(!host.stagedBios(CD).rejected);

    // 3. a file the core cannot accept (wrong size) is refused, and the built-in image runs instead.
    host.setBiosPath(CD, shortFile);
    CHECK(host.loadNoContent(err));
    CHECK(host.stagedBios(CD).rejected && host.stagedBios(CD).rejectedPath == shortFile);
    CHECK(host.stagedBios(CD).source == VJ_BIOS_SRC_BUILTIN);
    //    ... and so is a boot ROM handed over as the CD BIOS slot's choice of the wrong kind of size.
    host.setBiosPath(CD, bootFile);
    CHECK(host.loadNoContent(err));
    CHECK(host.stagedBios(CD).rejected && host.stagedBios(CD).source == VJ_BIOS_SRC_BUILTIN);

    // 4. a chosen file that has since vanished is "no preference", not an error.
    host.setBiosPath(CD, t + "gone.bin");
    CHECK(host.loadNoContent(err));
    CHECK(!host.stagedBios(CD).rejected && host.stagedBios(CD).source == VJ_BIOS_SRC_BUILTIN);

    // 5. a frontend that leaves VJ_CAP_BIOS out (RetroArch's behaviour) is never asked: the file is ignored.
    host.setBiosPath(CD, cdFile);
    host.announceBios = false;
    CHECK(host.loadNoContent(err));
    CHECK(host.stagedBios(CD).seen && host.stagedBios(CD).source == VJ_BIOS_SRC_BUILTIN && !host.stagedBios(CD).rejected);
    host.announceBios = true;

    // 6. no-content boot into the cartridge boot ROM with an empty slot: no CD BIOS is staged at all.
    host.setBiosPath(CD, "");
    host.setBootTarget(VJ_BOOT_CART_BOOT_ROM);
    CHECK(host.loadNoContent(err));
    CHECK(host.stagedBios(CART).seen && host.stagedBios(CART).source == VJ_BIOS_SRC_BUILTIN);
    CHECK(host.stagedBios(CART).crc == 0xFB731AAAu);   // the built-in Series K
    CHECK(!host.stagedBios(CD).seen);
    for (int i = 0; i < 120; i++) host.runFrame();
    CHECK(frameVaries(host.frame()));                  // the boot ROM's logo on an empty slot

    // 7. the same, through a boot ROM file chosen under any name.
    host.setBiosPath(CART, bootFile);
    CHECK(host.loadNoContent(err));
    CHECK(host.stagedBios(CART).source == VJ_BIOS_SRC_FRONTEND && host.stagedBios(CART).crc == bootCrc);
    host.setBiosPath(CART, shortFile);
    CHECK(host.loadNoContent(err));
    CHECK(host.stagedBios(CART).rejected && host.stagedBios(CART).source == VJ_BIOS_SRC_BUILTIN);

    // 8. content never asks for a boot target; a chosen boot ROM is staged for it even while it boots through HLE.
    host.setBootTarget(VJ_BOOT_CD_BIOS);
    host.setBiosPath(CART, bootFile);
    CHECK(host.loadContent(rom, err));
    CHECK(host.stagedBios(CART).source == VJ_BIOS_SRC_FRONTEND && host.stagedBios(CART).crc == bootCrc);
    CHECK(!host.stagedBios(CD).seen);                  // a cartridge never stages a CD BIOS

    host.unloadContent();
    fs::remove_all(tmp, ec);   // our own temp dir only
    std::printf("bios e2e: %d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
