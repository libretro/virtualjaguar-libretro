// Tests for the shader layer (issue #834).  Usage:
//   vj_shader_tests unit      pure logic + the library-missing fallbacks (no GL, no library)
//   vj_shader_tests library   real librashader parses the bundled presets   (exit 77 = skipped)
//   vj_shader_tests gl        renders pixels through the bundled presets      (exit 77 = skipped)
// Exit code 77 is CTest's SKIP_RETURN_CODE: the machine lacks the library or a
// display, which is not a failure and must not be reported as a pass.
#ifdef _WIN32
#define NOMINMAX
#endif
#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "imgui_impl_opengl3_loader.h"
#include "config.h"
#include "paths.h"
#include "present_util.h"
#include "shaders.h"

#ifndef VJ_SHADER_DIR
#error VJ_SHADER_DIR must point at app/shaders
#endif

namespace fs = std::filesystem;
using namespace vj;

static int g_fail = 0;
// Progress markers on stderr (unbuffered): a hung CI job then shows where it stopped.
#define STEP(name) std::fprintf(stderr, "shader_tests: step %s\n", name)
#define CHECK(cond, ...)                                              \
    do {                                                              \
        if (!(cond)) {                                                \
            g_fail++;                                                 \
            std::fprintf(stderr, "shader_tests: FAIL %s:%d: %s -- ", __FILE__, __LINE__, #cond); \
            std::fprintf(stderr, __VA_ARGS__);                        \
            std::fprintf(stderr, "\n");                               \
        }                                                             \
    } while (0)

static void setEnv(const char* k, const char* v) {
#ifdef _WIN32
    _putenv_s(k, v ? v : "");
#else
    if (v) setenv(k, v, 1);
    else unsetenv(k);
#endif
}

static fs::path makeTemp(const char* tag) {
    fs::path p = fs::temp_directory_path() / (std::string("vj-shader-test-") + tag + "-" +
                                              std::to_string((unsigned long long)SDL_GetPerformanceCounter()));
    fs::create_directories(p);
    return p;
}

static void write(const fs::path& p, const std::string& text) {
    fs::create_directories(p.parent_path());
    std::ofstream(p, std::ios::binary) << text;
}

static std::string str(const fs::path& p) { return p.generic_string(); }

// ---------------------------------------------------------------------------------------
// unit
// ---------------------------------------------------------------------------------------

static void testRect() {
    STEP("rect");
    PresentOptions o;
    // 640x480 source in a 1000x800 area, 4:3 core aspect: width-limited, centred.
    PresentRect r = computePresentRect(0, 0, 1000, 800, 640, 480, 4.0f / 3.0f, o);
    CHECK(std::fabs(r.w - 1000) < 0.5f && std::fabs(r.h - 750) < 0.5f, "fit %g x %g", r.w, r.h);
    CHECK(r.x == 0 && r.y == 25, "centred at %g,%g", r.x, r.y);

    // Integer scaling: whole multiples of the source height only.
    o.integerScale = true;
    r = computePresentRect(0, 0, 1000, 1000, 320, 240, 4.0f / 3.0f, o);
    CHECK(r.h == 720 && std::fabs(r.w - 960) < 0.5f, "integer 3x -> %g x %g", r.w, r.h);
    // A window narrower than 4:3 of the integer height steps down a multiple.
    r = computePresentRect(0, 0, 700, 1000, 320, 240, 4.0f / 3.0f, o);
    CHECK(r.h == 480 && std::fabs(r.w - 640) < 0.5f, "integer width-limited -> %g x %g", r.w, r.h);
    // Never below 1x when the window is smaller than the source: falls back to fit.
    r = computePresentRect(0, 0, 200, 150, 320, 240, 4.0f / 3.0f, o);
    CHECK(r.w <= 200.5f && r.h <= 150.5f && r.w > 0, "tiny window -> %g x %g", r.w, r.h);

    // Stretch ignores aspect; the override beats the core's aspect.
    o = PresentOptions();
    o.stretch = true;
    r = computePresentRect(10, 20, 300, 100, 320, 240, 4.0f / 3.0f, o);
    CHECK(r.w == 300 && r.h == 100 && r.x == 10 && r.y == 20, "stretch");
    o = PresentOptions();
    o.aspectOverride = 16.0f / 9.0f;
    r = computePresentRect(0, 0, 1600, 1600, 320, 240, 4.0f / 3.0f, o);
    CHECK(std::fabs(r.w / r.h - 16.0f / 9.0f) < 0.01f, "override aspect %g", r.w / r.h);
    // No frame: an empty rectangle, never a division by zero.
    r = computePresentRect(5, 6, 100, 100, 0, 0, 0, o);
    CHECK(r.w == 0 && r.h == 0, "no source");
}

static void testPresetParsing() {
    STEP("parse");
    fs::path d = makeTemp("parse");
    write(d / "a.slang", "x");
    write(d / "sub/b.slang", "x");
    write(d / "lut.png", "x");

    CHECK(hasPresetExtension("x/y.slangp") && hasPresetExtension("Y.SLANGP"), "extension, case-insensitive");
    CHECK(!hasPresetExtension("y.glslp") && !hasPresetExtension("slangp") && !hasPresetExtension("y.slang"), "extension");

    write(d / "good.slangp",
          "shaders = 2\n"
          "# a comment\n"
          "shader0 = \"a.slang\"\n"
          "filter_linear0 = true\n"
          "shader1 = sub/b.slang   # trailing comment\n"
          "textures = \"lut\"\n"
          "lut = lut.png\n");
    PresetInfo p = inspectPreset(str(d / "good.slangp"));
    CHECK(p.ok, "good preset rejected: %s", p.error.c_str());
    CHECK(p.shaders.size() == 2, "2 passes, got %zu", p.shaders.size());
    CHECK(p.shaders.size() == 2 && p.shaders[0] == str(d / "a.slang") && p.shaders[1] == str(d / "sub/b.slang"),
          "pass order / resolution: %s", p.shaders.empty() ? "(none)" : p.shaders.back().c_str());
    CHECK(p.textures.size() == 1, "1 texture");

    // #reference: the referencing file's keys win; paths resolve against the file that defines them.
    write(d / "ref/over.slangp", "#reference \"../good.slangp\"\nshaders = 1\n");
    p = inspectPreset(str(d / "ref/over.slangp"));
    CHECK(p.ok && p.shaders.size() == 1 && p.references.size() == 1, "reference: %s", p.error.c_str());
    write(d / "ref/inherit.slangp", "#reference \"../good.slangp\"\n");
    p = inspectPreset(str(d / "ref/inherit.slangp"));
    CHECK(p.ok && p.shaders.size() == 2, "inherits both passes: %s", p.error.c_str());

    // Things that must be refused, each with a message that names the problem.
    p = inspectPreset(str(d / "nope.slangp"));
    CHECK(!p.ok && p.error.find("not found") != std::string::npos, "missing file: %s", p.error.c_str());
    p = inspectPreset(str(d));
    CHECK(!p.ok, "a directory is not a preset");
    p = inspectPreset("");
    CHECK(!p.ok, "empty path");
    write(d / "old.glslp", "shaders = 1\nshader0 = a.slang\n");
    p = inspectPreset(str(d / "old.glslp"));
    CHECK(!p.ok && p.error.find("glslp") != std::string::npos, "glslp: %s", p.error.c_str());
    write(d / "nocount.slangp", "shader0 = a.slang\n");
    p = inspectPreset(str(d / "nocount.slangp"));
    CHECK(!p.ok && p.error.find("shaders") != std::string::npos, "no count: %s", p.error.c_str());
    write(d / "zero.slangp", "shaders = 0\n");
    CHECK(!inspectPreset(str(d / "zero.slangp")).ok, "zero passes");
    write(d / "gap.slangp", "shaders = 2\nshader0 = a.slang\n");
    p = inspectPreset(str(d / "gap.slangp"));
    CHECK(!p.ok && p.error.find("pass 1") != std::string::npos, "missing shader1: %s", p.error.c_str());
    write(d / "missing.slangp", "shaders = 1\nshader0 = gone.slang\n");
    p = inspectPreset(str(d / "missing.slangp"));
    CHECK(!p.ok && p.error.find("gone.slang") != std::string::npos, "missing shader file: %s", p.error.c_str());
    write(d / "lutless.slangp", "shaders = 1\nshader0 = a.slang\ntextures = \"lut\"\nlut = nothere.png\n");
    p = inspectPreset(str(d / "lutless.slangp"));
    CHECK(!p.ok && p.error.find("nothere.png") != std::string::npos, "missing texture: %s", p.error.c_str());
    write(d / "loopA.slangp", "#reference \"loopB.slangp\"\nshaders = 1\n");
    write(d / "loopB.slangp", "#reference \"loopA.slangp\"\n");
    p = inspectPreset(str(d / "loopA.slangp"));
    CHECK(!p.ok && p.error.find("loop") != std::string::npos, "reference loop: %s", p.error.c_str());
    write(d / "badref.slangp", "#reference \"absent.slangp\"\n");
    CHECK(!inspectPreset(str(d / "badref.slangp")).ok, "reference to a missing file");

    STEP("scan");
    // Folder scan: finds presets at depth, skips .git, sorted, relative with '/'.
    write(d / ".git/hooks/x.slangp", "x");
    write(d / "deep/er/crt.slangp", "shaders = 1\n");
    bool trunc = true;
    std::vector<std::string> l = scanPresets(str(d), 5000, 10, &trunc);
    bool hasDeep = false, hasGit = false, sorted = std::is_sorted(l.begin(), l.end());
    for (const std::string& s : l) {
        if (s == "deep/er/crt.slangp") hasDeep = true;
        if (s.find(".git") != std::string::npos) hasGit = true;
    }
    CHECK(hasDeep && !hasGit && sorted && !trunc, "scan: %zu found", l.size());
    l = scanPresets(str(d), 2, 10, &trunc);
    CHECK(l.size() == 2 && trunc, "result cap");
    l = scanPresets(str(d / "nonexistent"), 10, 10, &trunc);
    CHECK(l.empty(), "missing root");
    l = scanPresets("", 10, 10, nullptr);
    CHECK(l.empty(), "empty root");

    std::error_code ec;
    fs::remove_all(d, ec);
}

static void testFallback() {
    STEP("fallback");
    fs::path d = makeTemp("fallback");
    write(d / "p.slang", "x");
    write(d / "p.slangp", "shaders = 1\nshader0 = p.slang\n");

    // 1. A library path that does not exist forces "missing", whatever is installed.
    std::string bogus = str(d / "no-such-dir" / ShaderChain::libraryFileName());
    setEnv("VJ_LIBRASHADER", bogus.c_str());
    {
        ShaderChain sc;
        sc.enabled = true;
        CHECK(!sc.probeLibrary(), "bogus VJ_LIBRASHADER must read as missing");
        CHECK(!sc.libraryLoaded() && !sc.active(), "not loaded, not active");
        CHECK(!sc.libraryError().empty(), "an error text is set");
        CHECK(!ShaderChain::builtWithHeaders() || sc.libraryStatus() == ShaderChain::LibraryStatus::NotFound, "a path with no file reads as not found, not as a failed load");
        std::string err;
        CHECK(sc.usePreset(str(d / "p.slangp"), err), "a valid preset is accepted even without the library: %s", err.c_str());
        CHECK(!sc.usePreset(str(d / "absent.slangp"), err) && !err.empty(), "an invalid preset is still refused");
        CHECK(sc.preset == str(d / "p.slangp"), "refused preset leaves the previous one");
        ShaderFrame f;
        f.srcTex = 1;
        f.srcW = f.srcH = 8;
        f.outW = f.outH = 16;
        // No GL is touched on this path: render() just says "draw the plain picture".
        CHECK(sc.render(f) == 0 && sc.render(f) == 0 && sc.render(f) == 0, "render falls back to 0");
        std::string note;
        CHECK(sc.takeNotice(note), "a notice is queued");
        CHECK(note.find("plain picture") != std::string::npos, "notice text: %s", note.c_str());
        CHECK(!sc.takeNotice(note), "and only one, not one per frame");
        sc.enabled = false;
        CHECK(sc.render(f) == 0 && !sc.takeNotice(note), "disabled: silent");
        sc.destroyGL();   // safe with nothing created
    }

    STEP("fallback-garbage-lib");
    // 2. A file that is not a library at all.
    write(d / "fake" / ShaderChain::libraryFileName(), "this is not a shared library\n");
    setEnv("VJ_LIBRASHADER", str(d / "fake").c_str());
    {
        ShaderChain sc;
        CHECK(!sc.probeLibrary(), "a text file must not load as a library");
        CHECK(!sc.libraryError().empty(), "and says why: %s", sc.libraryError().c_str());
        CHECK(!ShaderChain::builtWithHeaders() || sc.libraryStatus() == ShaderChain::LibraryStatus::LoadFailed, "a file that exists but cannot be used reads as a failed load");
        CHECK(!ShaderChain::builtWithHeaders() || sc.libraryError().find(ShaderChain::libraryFileName()) != std::string::npos, "and names the file");
    }

    STEP("fallback-settings");
    // 3. settings round trip
    {
        Config cfg;
        cfg.setPath(str(d / "c.ini"));
        ShaderChain a;
        a.enabled = true;
        a.preset = "/some/preset.slangp";
        a.folder = "/some/folder/";
        a.saveSettings(cfg);
        ShaderChain b;
        b.loadSettings(cfg, str(d) + "/");
        CHECK(b.enabled && b.preset == a.preset && b.folder == a.folder, "settings round trip");
        ShaderChain c;
        Config empty;
        c.loadSettings(empty, "");
        CHECK(!c.enabled && c.preset.empty(), "defaults: off");
    }
    setEnv("VJ_LIBRASHADER", nullptr);
    std::error_code ec;
    fs::remove_all(d, ec);
}

// Optional (VJ_TEST_SLANG_DIR=<slang-shaders checkout>): the parser against ~1500
// real presets.  Not part of the default run; prints what it finds.
static void testRealCheckout() {
    const char* dir = std::getenv("VJ_TEST_SLANG_DIR");
    if (!dir || !*dir) return;
    Uint64 t0 = SDL_GetTicks();
    bool trunc = false;
    std::vector<std::string> l = scanPresets(dir, 100000, 12, &trunc);
    Uint64 t1 = SDL_GetTicks();
    std::string root = dir;
    if (root.back() != '/') root += '/';
    size_t bad = 0;
    for (const std::string& rel : l) {
        PresetInfo p = inspectPreset(root + rel);
        if (!p.ok) {
            if (bad++ < 12) std::printf("  not valid: %s: %s\n", rel.c_str(), p.error.c_str());
        }
    }
    std::printf("slang-shaders: %zu presets found in %llu ms%s, %zu failed inspection (%llu ms total)\n", l.size(),
                (unsigned long long)(t1 - t0), trunc ? " (truncated)" : "", bad, (unsigned long long)(SDL_GetTicks() - t0));
    CHECK(l.size() > 100, "a slang-shaders checkout has hundreds of presets");
    CHECK(inspectPreset(root + "crt/crt-easymode.slangp").ok, "crt-easymode.slangp");
    CHECK(inspectPreset(root + "crt/crt-geom.slangp").ok, "crt-geom.slangp");
}

static int runUnit() {
    testRect();
    testPresetParsing();
    testFallback();
    testRealCheckout();
    std::printf("shader_tests unit: %s (%s headers)\n", g_fail ? "FAIL" : "OK",
                ShaderChain::builtWithHeaders() ? "with" : "without");
    return g_fail ? 1 : 0;
}

// ---------------------------------------------------------------------------------------
// bundled slang-shaders subset
// ---------------------------------------------------------------------------------------

#ifdef VJ_SLANG_DIR
static const char* const kBundledMustWork[] = {
    "crt/crt-easymode.slangp",        "crt/crt-geom.slangp",
    "crt/crt-royale.slangp",          "crt/crt-lottes.slangp",
    "pixel-art-scaling/sharp-bilinear-simple.slangp",
    "interpolation/bicubic.slangp",   "bilinear.slangp", "nearest.slangp",
};

static int runBundled() {
    std::string root = VJ_SLANG_DIR;
    if (root.empty() || !fs::is_directory(fs::u8path(root))) {
        std::printf("shader_tests bundled: SKIP (no bundled subset)\n");
        return 77;
    }
    if (root.back() != '/') root += '/';
    bool trunc = false;
    std::vector<std::string> l = scanPresets(root, 100000, 12, &trunc);
    size_t bad = 0;
    for (const std::string& rel : l) {
        PresetInfo p = inspectPreset(root + rel);
        if (!p.ok && bad++ < 15) std::printf("  not valid: %s: %s\n", rel.c_str(), p.error.c_str());
    }
    std::printf("bundled slang-shaders: %zu presets, %zu failed inspection\n", l.size(), bad);
    CHECK(l.size() > 400, "expected hundreds of presets, got %zu", l.size());
    // A handful may be broken upstream; the closure must still be essentially complete.
    CHECK(bad * 50 <= l.size(), "too many presets with missing files: %zu of %zu", bad, l.size());
    for (const char* rel : kBundledMustWork) {
        PresetInfo p = inspectPreset(root + rel);
        CHECK(p.ok, "%s: %s", rel, p.error.c_str());
    }
    CHECK(fs::exists(fs::u8path(root + "NOTICE-virtualjaguar.txt")), "notice file");
    CHECK(fs::exists(fs::u8path(root + "crt/shaders/crt-royale/LICENSE.TXT")), "crt-royale licence travels with it");
    std::printf("shader_tests bundled: %s\n", g_fail ? "FAIL" : "OK");
    return g_fail ? 1 : 0;
}
#else
static int runBundled() {
    std::printf("shader_tests bundled: SKIP (no bundled subset)\n");
    return 77;
}
#endif

// ---------------------------------------------------------------------------------------
// library
// ---------------------------------------------------------------------------------------

static int runLibrary() {
    if (!ShaderChain::builtWithHeaders()) {
        std::printf("shader_tests library: SKIP (built without librashader headers)\n");
        return 77;
    }
    ShaderChain sc;
    sc.setLibrarySearchDirs({});
    if (!sc.probeLibrary()) {
        // VJ_REQUIRE_LIBRASHADER=1 (CI, on the legs that ship a runtime): a runtime that does not load
        // is a failure, never a skip.
        const char* req = std::getenv("VJ_REQUIRE_LIBRASHADER");
        if (req && *req && *req != '0') {
            std::printf("shader_tests library: FAIL (VJ_REQUIRE_LIBRASHADER is set but the runtime did not load: %s)\n",
                        sc.libraryError().c_str());
            return 1;
        }
        std::printf("shader_tests library: SKIP (%s)\n", sc.libraryError().c_str());
        return 77;
    }
    std::printf("librashader %s, API version %u\n", sc.libraryPath().c_str(), sc.libraryApiVersion());
    CHECK(sc.libraryApiVersion() >= 1, "API version");

    for (const char* name : {"vj-sharp-bilinear.slangp", "vj-scanlines.slangp"}) {
        std::string err;
        CHECK(sc.usePreset(std::string(VJ_SHADER_DIR) + "/" + name, err), "%s: %s", name, err.c_str());
    }
    // The scanlines preset declares three parameters.
    CHECK(sc.params().size() == 3, "scanlines parameters: %zu", sc.params().size());
    bool found = false;
    for (const ShaderParam& p : sc.params())
        if (p.name == "SCAN_STRENGTH") found = p.initial > 0.4f && p.initial < 0.5f && p.maximum == 1.0f;
    CHECK(found, "SCAN_STRENGTH range read from #pragma parameter");

    // A preset librashader itself rejects (syntactically valid for us, nonsense for it).
    fs::path d = makeTemp("lib");
    write(d / "a.slang", "x");
    write(d / "weird.slangp", "shaders = 1\nshader0 = a.slang\nscale_type0 = sideways\nscale0 = banana\n");
    std::string err;
    bool accepted = sc.usePreset(str(d / "weird.slangp"), err);
    std::printf("librashader on a nonsense preset: %s%s\n", accepted ? "accepted" : "rejected: ", accepted ? "" : err.c_str());
    CHECK(accepted || !err.empty(), "a rejection carries the library's message");
    std::error_code ec;
    fs::remove_all(d, ec);
    std::printf("shader_tests library: %s\n", g_fail ? "FAIL" : "OK");
    return g_fail ? 1 : 0;
}

// ---------------------------------------------------------------------------------------
// gl
// ---------------------------------------------------------------------------------------

typedef void (*PFNBindFb)(unsigned, unsigned);
typedef void (*PFNGenFb)(int, unsigned*);
typedef void (*PFNDelFb)(int, const unsigned*);
typedef void (*PFNFbTex)(unsigned, unsigned, unsigned, unsigned, int);

static std::vector<unsigned char> readTexture(unsigned tex, int w, int h) {
    PFNBindFb bindFb = (PFNBindFb)SDL_GL_GetProcAddress("glBindFramebuffer");
    PFNGenFb genFb = (PFNGenFb)SDL_GL_GetProcAddress("glGenFramebuffers");
    PFNDelFb delFb = (PFNDelFb)SDL_GL_GetProcAddress("glDeleteFramebuffers");
    PFNFbTex fbTex = (PFNFbTex)SDL_GL_GetProcAddress("glFramebufferTexture2D");
    std::vector<unsigned char> px((size_t)w * h * 4, 0);
    unsigned fb = 0;
    genFb(1, &fb);
    bindFb(0x8D40 /*GL_FRAMEBUFFER*/, fb);
    fbTex(0x8D40, 0x8CE0 /*COLOR_ATTACHMENT0*/, GL_TEXTURE_2D, tex, 0);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, 0x1908 /*GL_RGBA*/, 0x1401 /*GL_UNSIGNED_BYTE*/, px.data());
    bindFb(0x8D40, 0);
    delFb(1, &fb);
    return px;
}

static unsigned makeTexture(int w, int h, const std::vector<unsigned char>& rgba) {
    GLuint t = 0;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, 0x813D /*GL_TEXTURE_MAX_LEVEL*/, 0);   // no mip chain; see video.cpp
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, 0x8058 /*GL_RGBA8*/, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
    return t;
}

static int runGl() {
    if (!ShaderChain::builtWithHeaders()) {
        std::printf("shader_tests gl: SKIP (built without librashader headers)\n");
        return 77;
    }
    ShaderChain sc;
    sc.setLibrarySearchDirs({});
    if (!sc.probeLibrary()) {
        std::printf("shader_tests gl: SKIP (%s)\n", sc.libraryError().c_str());
        return 77;
    }
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::printf("shader_tests gl: SKIP (no video: %s)\n", SDL_GetError());
        return 77;
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
#ifdef __APPLE__
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
#endif
    SDL_Window* win = SDL_CreateWindow("vj shader test", 64, 64, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    SDL_GLContext ctx = win ? SDL_GL_CreateContext(win) : nullptr;
    if (!win || !ctx) {
        std::printf("shader_tests gl: SKIP (no OpenGL 3.3 context: %s)\n", SDL_GetError());
        if (win) SDL_DestroyWindow(win);
        SDL_Quit();
        return 77;
    }
    SDL_GL_MakeCurrent(win, ctx);
    if (imgl3wInit() != 0) {
        std::printf("shader_tests gl: FAIL cannot load GL entry points\n");
        return 1;
    }
    std::printf("GL: %s / %s\n", (const char*)glGetString(GL_VERSION), (const char*)glGetString(GL_RENDERER));

    // Source picture, uploaded the way the app does: row 0 is the TOP of the image.
    // Top half red, bottom half blue.
    const int SW = 32, SH = 16, OW = 128, OH = 64;
    std::vector<unsigned char> src((size_t)SW * SH * 4);
    for (int y = 0; y < SH; y++)
        for (int x = 0; x < SW; x++) {
            unsigned char* p = &src[((size_t)y * SW + x) * 4];
            bool top = y < SH / 2;
            p[0] = top ? 255 : 0;
            p[1] = 0;
            p[2] = top ? 0 : 255;
            p[3] = 255;
        }
    unsigned srcTex = makeTexture(SW, SH, src);

    ShaderFrame f;
    f.srcTex = srcTex;
    f.srcW = SW;
    f.srcH = SH;
    f.outW = OW;
    f.outH = OH;
    f.aspect = 2.0f;
    f.fps = 60.0f;

    auto px = [&](const std::vector<unsigned char>& v, int x, int y) { return &v[((size_t)y * OW + x) * 4]; };

    // ---- sharp bilinear: an integer 4x scale must reproduce the picture crisply ----
    sc.enabled = true;
    std::string err;
    CHECK(sc.usePreset(std::string(VJ_SHADER_DIR) + "/vj-sharp-bilinear.slangp", err), "preset: %s", err.c_str());
    f.frameCount = 1;
    unsigned out = sc.render(f);
    CHECK(out != 0, "render returned no texture: %s", sc.lastError().c_str());
    if (out) {
        std::vector<unsigned char> o = readTexture(out, OW, OH);
        // The output texture keeps the input's row order (row 0 = top of the picture),
        // which is why the presenter draws it with plain UVs: red on top, blue below.
        const unsigned char* top = px(o, 64, 6);
        const unsigned char* bottom = px(o, 64, OH - 7);
        CHECK(top[0] > 240 && top[2] < 15, "top of the picture (row 6) should be red, got %d,%d,%d", top[0], top[1], top[2]);
        CHECK(bottom[2] > 240 && bottom[0] < 15, "bottom of the picture should be blue, got %d,%d,%d", bottom[0], bottom[1], bottom[2]);
        // crisp edge: the red/blue seam is on a 4-pixel source boundary; one row either
        // side of it is pure, not blended.
        const unsigned char* a = px(o, 64, OH / 2 - 1);
        const unsigned char* b = px(o, 64, OH / 2 + 1);
        CHECK((a[0] > 240) != (b[0] > 240), "seam rows differ: %d vs %d", a[0], b[0]);
        // Resizing the output re-allocates cleanly.
        ShaderFrame g = f;
        g.outW = 96;
        g.outH = 48;
        CHECK(sc.render(g) != 0, "render after a size change");
        CHECK(sc.render(f) != 0, "and back");
        // The same emulated frame at the same size is not rendered twice; a new
        // frame (or a changed size) is.
        uint64_t r0 = sc.chainRuns(), u0 = sc.chainReuses();
        unsigned again = sc.render(f);
        CHECK(again != 0 && sc.chainRuns() == r0 && sc.chainReuses() == u0 + 1, "same frame, same size: output reused");
        ShaderFrame nf = f;
        nf.frameCount = f.frameCount + 1;
        CHECK(sc.render(nf) != 0 && sc.chainRuns() == r0 + 1, "next frame: chain runs");
        f.frameCount = nf.frameCount;
    }

    // ---- scanlines: a flat white source must come out with bright and dark rows ----
    std::vector<unsigned char> white((size_t)SW * SH * 4, 255);
    unsigned whiteTex = makeTexture(SW, SH, white);
    f.srcTex = whiteTex;
    CHECK(sc.usePreset(std::string(VJ_SHADER_DIR) + "/vj-scanlines.slangp", err), "preset: %s", err.c_str());
    out = sc.render(f);
    CHECK(out != 0, "scanlines render: %s", sc.lastError().c_str());
    if (out) {
        std::vector<unsigned char> o = readTexture(out, OW, OH);
        int mn = 255, mx = 0;
        for (int y = 0; y < OH; y++) {
            int v = px(o, 64, y)[1];
            mn = std::min(mn, v);
            mx = std::max(mx, v);
        }
        CHECK(mx >= 245 && mn <= 200, "scanline rows should vary: min %d max %d", mn, mx);
        // Turning the strength to 0 flattens it again, live, through the parameter API.
        for (size_t i = 0; i < sc.params().size(); i++)
            if (sc.params()[i].name == "SCAN_STRENGTH") {
                sc.params()[i].value = 0.0f;
                sc.paramChanged(i);
            }
        out = sc.render(f);
        o = readTexture(out, OW, OH);
        mn = 255;
        mx = 0;
        for (int y = 0; y < OH; y++) {
            int v = px(o, 64, y)[1];
            mn = std::min(mn, v);
            mx = std::max(mx, v);
        }
        CHECK(mx - mn <= 2, "strength 0 should be flat: min %d max %d", mn, mx);
    }

#ifdef VJ_SLANG_DIR
    // ---- the bundled slang-shaders presets that must work on this GL ----
    {
        std::string root = VJ_SLANG_DIR;
        if (!root.empty() && root.back() != '/') root += '/';
        for (const char* rel : kBundledMustWork) {
            if (!fs::exists(fs::u8path(root + rel))) continue;   // bundling was skipped
            // crt-geom declares GL_ARB_arrays_of_arrays, which macOS's GL 4.1 lacks: there it
            // must fall back cleanly (that is itself the check), elsewhere it must render.
            bool optional = std::strstr(rel, "crt-geom") != nullptr;
            ShaderChain ex;
            ex.setLibrarySearchDirs({});
            ex.enabled = true;
            std::string e2;
            bool used = ex.usePreset(root + rel, e2);
            unsigned t = 0;
            for (int i = 1; used && i <= 4; i++) {
                f.srcTex = srcTex;
                f.frameCount = (uint64_t)i;
                t = ex.render(f);
            }
            long sum = 0;
            if (t) {
                std::vector<unsigned char> o = readTexture(t, OW, OH);
                for (size_t i = 0; i < o.size(); i += 4) sum += o[i] + o[i + 1] + o[i + 2];
            }
            std::printf("bundled %s: %s (energy %ld)%s%s\n", rel, t && sum > 0 ? "renders" : "FAILS", sum,
                        t ? "" : " -- ", t ? "" : (used ? ex.lastError().c_str() : e2.c_str()));
            if (optional && !t) CHECK(!ex.lastError().empty() && !ex.active(), "%s: failure must be clean", rel);
            else CHECK(t != 0 && sum > 0, "%s did not render a picture", rel);
            ex.destroyGL();
        }
    }
#endif

    // ---- any other preset on request (a slang-shaders checkout): VJ_TEST_PRESETS="a.slangp;b.slangp" ----
    if (const char* extra = std::getenv("VJ_TEST_PRESETS")) {
        f.srcTex = srcTex;
        std::string list = extra;
        size_t a0 = 0;
        while (a0 < list.size()) {
            size_t b0 = list.find(';', a0);
            std::string one = list.substr(a0, b0 == std::string::npos ? std::string::npos : b0 - a0);
            a0 = b0 == std::string::npos ? list.size() : b0 + 1;
            if (one.empty()) continue;
            ShaderChain ex;
            ex.setLibrarySearchDirs({});
            ex.enabled = true;
            std::string e2;
            bool used = ex.usePreset(one, e2);
            unsigned t = 0;
            for (int i = 1; used && i <= 3; i++) {   // a few frames: history/feedback passes warm up
                f.frameCount = (uint64_t)i;
                t = ex.render(f);
            }
            long sum = 0;
            if (t) {
                std::vector<unsigned char> o = readTexture(t, OW, OH);
                for (size_t i = 0; i < o.size(); i += 4) sum += o[i] + o[i + 1] + o[i + 2];
            }
            std::printf("preset %s: %s, %zu params, output energy %ld%s%s\n", one.c_str(),
                        t ? "rendered" : "NOT rendered", ex.params().size(), sum,
                        t ? "" : " -- ", t ? "" : (used ? ex.lastError().c_str() : e2.c_str()));
            CHECK(t != 0 && sum > 0, "%s did not render a picture", one.c_str());
            ex.destroyGL();
        }
    }

    // ---- a shader that does not compile: clean fallback, one notice, no crash ----
    fs::path d = makeTemp("gl");
    write(d / "broken.slang", "#version 450\n#pragma stage vertex\nvoid main() { this is not glsl }\n#pragma stage fragment\nvoid main() {}\n");
    write(d / "broken.slangp", "shaders = 1\nshader0 = broken.slang\n");
    ShaderChain bad;
    bad.setLibrarySearchDirs({});
    bad.enabled = true;
    bool accepted = bad.usePreset(str(d / "broken.slangp"), err);   // may be refused up front
    unsigned bo = accepted ? bad.render(f) : 0;
    std::string note;
    bool noted = bad.takeNotice(note);
    CHECK(bo == 0, "a broken shader must not produce a texture");
    CHECK(!accepted || (noted && note.find("plain picture") != std::string::npos), "notice: %s", note.c_str());
    CHECK(!accepted || !bad.lastError().empty(), "error text recorded");
    if (accepted) {
        CHECK(bad.render(f) == 0 && !bad.takeNotice(note), "after a failure: plain, silent, no retry storm");
        bad.retry();   // the user asking again tries again (and fails again, cleanly)
        CHECK(bad.render(f) == 0 && bad.takeNotice(note), "retry reports again");
    }
    std::printf("broken shader -> %s: %s\n", accepted ? "chain build failed" : "refused up front",
                accepted ? bad.lastError().c_str() : err.c_str());
    std::error_code ec;
    fs::remove_all(d, ec);

    sc.destroyGL();
    bad.destroyGL();
    SDL_GL_DestroyContext(ctx);
    SDL_DestroyWindow(win);
    SDL_Quit();
    std::printf("shader_tests gl: %s\n", g_fail ? "FAIL" : "OK");
    return g_fail ? 1 : 0;
}

// ---------------------------------------------------------------------------------------
// probe (packaging smoke test, not a CTest entry)
// ---------------------------------------------------------------------------------------
//
// vj_shader_tests probe <dir>: finds the library the way the APP does (beside the executable,
// <base>/../Frameworks in a macOS bundle; no VJ_LIBRASHADER, no extra directories) and fails
// unless it loaded from inside <dir>.  CI copies this executable into the unpacked AppImage,
// the installed .deb tree and the .app, where it proves the packaged layout is the probed one.
static int runProbe(const char* dir) {
    if (!ShaderChain::builtWithHeaders()) {
        std::printf("shader_tests probe: FAIL (built without librashader headers)\n");
        return 1;
    }
    setEnv("VJ_LIBRASHADER", nullptr);
    Config cfg;
    ShaderChain sc;
    sc.loadSettings(cfg, "");   // the same search list the app builds
    if (!sc.probeLibrary()) {
        std::printf("shader_tests probe: FAIL (%s)\n", sc.libraryError().c_str());
        return 1;
    }
    std::error_code ec;
    std::string got = fs::weakly_canonical(fs::path(sc.libraryPath()), ec).string();
    std::string want = dir ? fs::weakly_canonical(fs::path(dir), ec).string() : "";
    std::printf("shader_tests probe: found %s (API %u); expected inside %s\n", got.c_str(), sc.libraryApiVersion(), want.c_str());
    if (want.empty() || got.find(want) != 0) {
        std::printf("shader_tests probe: FAIL (not loaded from the package)\n");
        return 1;
    }
    std::printf("shader_tests probe: OK\n");
    return 0;
}

int main(int argc, char** argv) {
    std::string mode = argc > 1 ? argv[1] : "unit";
    if (mode == "probe") return runProbe(argc > 2 ? argv[2] : nullptr);
    if (mode == "unit") return runUnit();
    if (mode == "bundled") return runBundled();
    if (mode == "library") return runLibrary();
    if (mode == "gl") return runGl();
    std::fprintf(stderr, "usage: %s unit|bundled|library|gl|probe <dir>\n", argv[0]);
    return 2;
}
