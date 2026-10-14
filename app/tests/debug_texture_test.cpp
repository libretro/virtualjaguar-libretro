// Unit tests for the Textures window's logic (app/src/debug/texture_pack.*): manifest parsing on a
// synthetic dump folder (incremental, partial lines, sightings, 'both' mode previews), the pack
// install path and size rules, the replace flow's calls against the mock core interface, and the
// bounded thumbnail cache.  No window, no GL, no core.
#include "debug_mock.h"
#include "texture_pack.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

using namespace vj;
namespace fs = std::filesystem;

static int g_fail = 0, g_checks = 0;
#define CHECK(cond)                                                                \
    do {                                                                           \
        g_checks++;                                                                \
        if (!(cond)) {                                                             \
            g_fail++;                                                              \
            std::fprintf(stderr, "FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);   \
        }                                                                          \
    } while (0)

static std::string tmpRoot() {
    static int n = 0;
    char b[64];
    std::snprintf(b, sizeof b, "vj_dbg_tex_%ld_%d", (long)std::rand(), n++);
    fs::path p = fs::temp_directory_path() / b;
    fs::create_directories(p);
    return p.string();
}

static void writeText(const std::string& path, const std::string& text, bool append = false) {
    fs::create_directories(fs::path(path).parent_path());
    std::ofstream f(path, std::ios::binary | (append ? std::ios::app : std::ios::trunc));
    f << text;
}

// A PNG signature + IHDR (the CRC is not checked by the header reader).
static std::string pngHeader(unsigned w, unsigned h, int depth = 8, int ctype = 6, int interlace = 0) {
    std::string s("\x89PNG\r\n\x1a\n", 8);
    auto be32 = [&](unsigned v) {
        s += (char)(v >> 24); s += (char)(v >> 16); s += (char)(v >> 8); s += (char)v;
    };
    be32(13);
    s += "IHDR";
    be32(w); be32(h);
    s += (char)depth; s += (char)ctype; s += (char)0; s += (char)0; s += (char)interlace;
    be32(0);
    return s;
}

static const char* const kH1 = "0e4ae2eb24870790";
static const char* const kH2 = "0f772bb6bc839bd8";
static const char* const kH3 = "16e8fa46e9f385ac";

static std::string manifestText() {
    return std::string("# texdump v1\tcrc=0e4ae2eb\ttitle=Yet Another RC\n") +
           kH1 + "\t16x8\t16\tframe=12\tsrc=012340\tclut=-\tflags=-\n" +
           kH2 + "\t32x32\t8\tframe=40\tsrc=0a0000\tclut=11223344\tflags=bcompen,dcompen\n" +
           kH2 + "\tclut=55667788\tframe=90\n" +
           kH1 + "\t16x8\t16\tframe=999\tsrc=000000\tclut=-\tflags=-\n";   // a later session's duplicate row
}

static void testNamesAndPaths() {
    CHECK(tex::crcHex(0xABCDEFu) == "00abcdef");
    CHECK(tex::isHash16(kH1) && !tex::isHash16("0E4AE2EB24870790") && !tex::isHash16("0e4ae2eb2487079"));
    CHECK(tex::joinPath("/sys/", "x") == "/sys/x" && tex::joinPath("/sys", "x") == "/sys/x");
    CHECK(tex::joinPath("C:\\sys\\", "x") == "C:\\sys\\x");
    CHECK(tex::dumpDir("/sys/", 0x0e4ae2eb) == "/sys/vj_texdump/0e4ae2eb");
    CHECK(tex::packFile("/sys", 0x0e4ae2eb, kH1) == std::string("/sys/vj_texpacks/0e4ae2eb/") + kH1 + ".png");
    CHECK(tex::fileUrl("/Users/me/Library/Application Support/VJ/system/vj_texpacks/0e4ae2eb") ==
          "file:///Users/me/Library/Application%20Support/VJ/system/vj_texpacks/0e4ae2eb");
    CHECK(tex::fileUrl("C:\\Users\\me\\data\\system") == "file:///C:/Users/me/data/system");
    CHECK(tex::fileUrl("/tmp/a#b%c") == "file:///tmp/a%23b%25c");
    CHECK(tex::hashOfDumpFile(std::string(kH1) + ".png") == kH1);
    CHECK(tex::hashOfDumpFile(std::string(kH1) + "-cry.png") == kH1);
    CHECK(tex::hashOfDumpFile(std::string(kH1) + "-rgb.png") == kH1);
    CHECK(tex::hashOfDumpFile("manifest.tsv").empty() && tex::hashOfDumpFile("notes.png").empty());
    CHECK(tex::hashOfDumpFile(std::string(kH1) + "-xyz.png").empty());
}

static void testManifestParse() {
    tex::Manifest m;
    std::string t = manifestText();
    CHECK(tex::parseManifest(m, t) == t.size());
    CHECK(m.haveHeader && m.version == 1 && m.crc == "0e4ae2eb" && m.title == "Yet Another RC");
    CHECK(m.entries.size() == 2 && m.badLines == 0);
    const tex::Entry* a = m.find(kH1);
    CHECK(a && a->w == 16 && a->h == 8 && a->bpp == 16 && a->frame == 12 && a->src == 0x012340 &&
          a->clut.empty() && a->flags == "-" && a->inManifest);
    const tex::Entry* b = m.find(kH2);
    CHECK(b && b->w == 32 && b->h == 32 && b->bpp == 8 && b->frame == 40 && b->clut == "11223344" &&
          b->flags == "bcompen,dcompen" && b->sightings == 1);
    CHECK(!m.find(kH3));
    // Garbage rows are counted, not fatal; the sighting of an unknown tile is ignored.
    tex::Manifest g;
    std::string bad = std::string("nonsense\n") + kH3 + "\tcolour\t16\n" + kH3 + "\tclut=00000001\tframe=1\n\n";
    tex::parseManifest(g, bad);
    CHECK(g.entries.empty() && g.badLines == 2);
}

static void testRealCoreOutput() {
    // Verbatim manifest.tsv written by the core for yarc.j64 (600 frames, texture dump on, 16bpp 'both'):
    // 32bpp tiles, "title=unknown" (no database name), src= in hex.
    const std::string real =
        "# texdump v1\tcrc=714716b2\ttitle=unknown\n"
        "c2f123c336896a6c\t18x1\t32\tframe=1\tsrc=803460\tclut=-\tflags=-\n"
        "2bde3a05ce2964bc\t701x1\t32\tframe=1\tsrc=8027b8\tclut=-\tflags=-\n";
    tex::Manifest m;
    CHECK(tex::parseManifest(m, real) == real.size());
    CHECK(m.crc == "714716b2" && m.title == "unknown" && m.version == 1 && m.entries.size() == 2);
    const tex::Entry* e = m.find("2bde3a05ce2964bc");
    CHECK(e && e->w == 701 && e->h == 1 && e->bpp == 32 && e->src == 0x8027b8 && e->clut.empty());
}

static void testManifestPartialLines() {
    // The core writes through a buffer: a read can end mid-line.  Only complete lines are consumed.
    std::string t = manifestText();
    tex::Manifest m;
    size_t cut = t.find(kH2) + 20;   // inside the second row
    size_t used = tex::parseManifest(m, t.data(), cut);
    CHECK(used == t.find(kH2));      // up to the end of row 1
    CHECK(m.entries.size() == 1);
    used += tex::parseManifest(m, t.data() + used, t.size() - used);
    CHECK(used == t.size() && m.entries.size() == 2);
    // CRLF tolerated.
    tex::Manifest c;
    std::string crlf = std::string(kH1) + "\t4x4\t16\tframe=1\tsrc=000010\tclut=-\tflags=-\r\n";
    tex::parseManifest(c, crlf);
    CHECK(c.entries.size() == 1 && c.entries[0].flags == "-");
}

static void testDumpIndexIncremental() {
    std::string root = tmpRoot();
    std::string dir = tex::dumpDir(root, 0x0e4ae2eb);
    tex::DumpIndex idx;
    idx.setDir(dir);
    CHECK(!idx.poll() && !idx.dirExists());          // no folder yet

    fs::create_directories(dir);
    CHECK(!idx.poll() && idx.dirExists() && idx.size() == 0);

    // PNGs land first (the manifest is buffered): they already list, with the size from the PNG header.
    writeText(dir + "/" + kH1 + ".png", pngHeader(16, 8));
    writeText(dir + "/" + kH2 + "-cry.png", pngHeader(32, 32));       // 16bpp 'both' mode names
    writeText(dir + "/" + kH2 + "-rgb.png", pngHeader(32, 32));
    writeText(dir + "/notes.txt", "x");
    CHECK(idx.poll());
    CHECK(idx.size() == 2);
    CHECK(idx.manifest().find(kH1)->w == 16 && !idx.manifest().find(kH1)->inManifest);
    CHECK(tex::previewFile(dir, kH2) == dir + "/" + kH2 + "-cry.png");   // <hash>.png absent: -cry first
    CHECK(tex::previewFile(dir, kH1) == dir + "/" + kH1 + ".png");
    CHECK(tex::previewFile(dir, kH3).empty());

    // The manifest arrives (only part of it): rows upgrade the entries already listed.
    std::string t = manifestText();
    writeText(dir + "/manifest.tsv", t.substr(0, t.find(kH2) + 10));
    CHECK(idx.poll());
    CHECK(idx.manifest().find(kH1)->inManifest && idx.manifest().find(kH1)->frame == 12);
    CHECK(!idx.manifest().find(kH2)->inManifest);
    writeText(dir + "/manifest.tsv", t.substr(t.find(kH2) + 10), true);   // the rest
    CHECK(idx.poll());
    CHECK(idx.manifest().find(kH2)->bpp == 8 && idx.manifest().find(kH2)->sightings == 1);
    CHECK(idx.size() == 2);
    CHECK(!idx.poll());                                // nothing new

    // A brand new tile in both places; order is first-seen.
    writeText(dir + "/" + kH3 + ".png", pngHeader(8, 8));
    writeText(dir + "/manifest.tsv", std::string(kH3) + "\t8x8\t16\tframe=200\tsrc=000100\tclut=-\tflags=-\n", true);
    CHECK(idx.poll() && idx.size() == 3);
    CHECK(idx.manifest().entries[2].hash == kH3 && idx.manifest().entries[2].frame == 200);

    // Switching games clears everything.
    idx.setDir(tex::dumpDir(root, 0x11111111));
    CHECK(idx.size() == 0 && !idx.poll());
    fs::remove_all(root);
}

static void testPackDirListing() {
    std::string root = tmpRoot();
    std::string pd = tex::packDir(root, 0x0e4ae2eb);
    CHECK(tex::listPack(pd).empty());                  // no folder
    writeText(pd + "/" + kH1 + ".png", pngHeader(16, 8));
    writeText(pd + "/" + kH2 + ".PNG", "x");
    writeText(pd + "/" + kH3 + "-cry.png", "x");
    writeText(pd + "/readme.txt", "x");
    auto s = tex::listPack(pd);
    CHECK(s.size() == 1 && s.count(kH1));
    fs::remove_all(root);
}

static void testPngRules() {
    std::string h = pngHeader(64, 32, 8, 2);
    tex::PngInfo p = tex::parsePngHeader((const uint8_t*)h.data(), h.size());
    CHECK(p.ok && p.w == 64 && p.h == 32 && p.depth == 8 && p.colorType == 2 && p.interlace == 0);
    CHECK(!tex::parsePngHeader((const uint8_t*)"nope", 4).ok);
    std::string trunc = h.substr(0, 20);
    CHECK(!tex::parsePngHeader((const uint8_t*)trunc.data(), trunc.size()).ok);
    std::string noIhdr = h;
    noIhdr[12] = 'X';
    CHECK(!tex::parsePngHeader((const uint8_t*)noIhdr.data(), noIhdr.size()).ok);

    CHECK(tex::packScale(16, 8, 16, 8) == 1);
    CHECK(tex::packScale(16, 8, 32, 16) == 2);
    CHECK(tex::packScale(16, 8, 48, 24) == 0);         // 3x: beyond the core's 2x
    CHECK(tex::packScale(16, 8, 32, 8) == 0);          // one axis only
    CHECK(tex::packScale(16, 8, 17, 8) == 0);
    CHECK(tex::packScale(0, 8, 0, 8) == 0);

    auto info = [](unsigned w, unsigned h2, int depth = 8, int ct = 6, int il = 0) {
        std::string s = pngHeader(w, h2, depth, ct, il);
        return tex::parsePngHeader((const uint8_t*)s.data(), s.size());
    };
    tex::PackVerdict v = tex::checkPack(info(16, 8), 16, 8, 16);
    CHECK(v.ok && v.scale == 1 && !v.warn);
    v = tex::checkPack(info(32, 16), 16, 8, 16);
    CHECK(v.ok && v.scale == 2 && v.warn && v.message.find("Internal Resolution") != std::string::npos);
    v = tex::checkPack(info(20, 8), 16, 8, 16);
    CHECK(!v.ok && v.scale == 0 && v.message.find("16x8") != std::string::npos);
    CHECK(!tex::checkPack(info(16, 8, 16), 16, 8, 16).ok);          // 16-bit depth
    CHECK(!tex::checkPack(info(16, 8, 8, 6, 1), 16, 8, 16).ok);     // interlaced
    CHECK(!tex::checkPack(info(16, 8, 8, 1), 16, 8, 16).ok);        // colour type 1 does not exist
    for (int ct : {0, 2, 3, 4, 6}) CHECK(tex::checkPack(info(16, 8, 8, ct), 16, 8, 16).ok);
    v = tex::checkPack(info(16, 8), 16, 8, 8);                       // an indexed tile: loads, never presents
    CHECK(v.ok && v.warn);
    v = tex::checkPack(info(5, 5), 0, 0, 0);                         // tile size unknown
    CHECK(v.ok && v.warn);
    CHECK(!tex::checkPack(tex::PngInfo(), 16, 8, 16).ok);
}

static void testInstallAndExport() {
    std::string root = tmpRoot();
    std::string sys = root + "/system/";            // trailing separator, as Paths::system has
    std::string src = root + "/art.png";
    writeText(src, pngHeader(16, 8) + "payload");
    tex::InstallResult r = tex::installPack(sys, 0x0e4ae2eb, kH1, src);
    CHECK(r.ok);
    CHECK(r.dest == sys + "vj_texpacks/0e4ae2eb/" + kH1 + ".png");
    CHECK(fs::file_size(r.dest) == fs::file_size(src));
    CHECK(!fs::exists(r.dest + ".tmp"));
    // Installing again replaces; the source name does not matter, the destination is always <hash16>.png.
    writeText(src, pngHeader(16, 8) + "payload2payload2");
    CHECK(tex::installPack(sys, 0x0e4ae2eb, kH1, src).ok && fs::file_size(r.dest) == fs::file_size(src));
    CHECK(!tex::installPack(sys, 0x0e4ae2eb, "../evil", src).ok);                  // never a path
    CHECK(!tex::installPack(sys, 0x0e4ae2eb, "ABCDEF0123456789", src).ok);         // upper case is not a pack name
    writeText(root + "/notpng.png", "hello");
    CHECK(!tex::installPack(sys, 0x0e4ae2eb, kH2, root + "/notpng.png").ok);
    CHECK(!tex::installPack(sys, 0x0e4ae2eb, kH2, root + "/missing.png").ok);
    CHECK(tex::listPack(tex::packDir(sys, 0x0e4ae2eb)).size() == 1);

    // Export keeps the preview's own name (so -cry / -rgb stay apart).
    std::string dir = tex::dumpDir(sys, 0x0e4ae2eb);
    writeText(dir + "/" + kH2 + "-rgb.png", pngHeader(32, 32) + "rgbdata");
    std::string out = root + "/exported";
    CHECK(!tex::exportPreview(tex::previewFile(dir, kH2), out).ok);   // folder missing
    fs::create_directories(out);
    tex::InstallResult e = tex::exportPreview(tex::previewFile(dir, kH2), out);
    CHECK(e.ok && e.dest == out + "/" + kH2 + "-rgb.png" && fs::file_size(e.dest) == fs::file_size(dir + "/" + kH2 + "-rgb.png"));
    CHECK(!tex::exportPreview("", out).ok);
    fs::remove_all(root);
}

struct FakeOptions {
    std::vector<std::pair<std::string, std::string>> sets;
    std::map<std::string, std::string> cur;
    tex::OptionIO io() {
        tex::OptionIO o;
        o.get = [this](const std::string& k) { auto it = cur.find(k); return it == cur.end() ? std::string("disabled") : it->second; };
        o.set = [this](const std::string& k, const std::string& v) { sets.emplace_back(k, v); cur[k] = v; };
        return o;
    }
};

static void testReplaceFlow() {
    std::string root = tmpRoot();
    std::string src = root + "/art.png";
    writeText(src, pngHeader(16, 8) + "payload");
    MockDebugIface mock;
    FakeOptions fo;
    tex::OptionIO io = fo.io();

    tex::ReplaceOutcome o = tex::replaceTile(io, mock.iface(), root, 0x0e4ae2eb, kH1, 16, 8, 16, src);
    CHECK(o.ok && o.reloadCalled && o.reloadActive);
    CHECK(mock.texReloads == 1);
    CHECK(fs::exists(tex::packFile(root, 0x0e4ae2eb, kH1)));
    CHECK(fo.sets.size() == 1 && fo.sets[0].first == tex::kOptReplace && fo.sets[0].second == "enabled");
    CHECK(tex::optionOn(io, tex::kOptReplace) && !tex::optionOn(io, tex::kOptDump));

    // A wrong size touches nothing: no file, no reload, no option change.
    writeText(src, pngHeader(20, 8));
    o = tex::replaceTile(io, mock.iface(), root, 0x0e4ae2eb, kH2, 16, 8, 16, src);
    CHECK(!o.ok && !o.reloadCalled && mock.texReloads == 1 && fo.sets.size() == 1);
    CHECK(!fs::exists(tex::packFile(root, 0x0e4ae2eb, kH2)));

    // 2x art: accepted with a note about Internal Resolution.
    writeText(src, pngHeader(32, 16));
    o = tex::replaceTile(io, mock.iface(), root, 0x0e4ae2eb, kH3, 16, 8, 16, src);
    CHECK(o.ok && o.verdict.scale == 2 && o.verdict.warn && mock.texReloads == 2);

    // A core without the entry (older interface) still installs and says so.
    vj_debug_iface old = *mock.iface();
    old.size = (unsigned)offsetof(vj_debug_iface, texreplace_reload);   // ends before texreplace_reload
    o = tex::replaceTile(io, &old, root, 0x0e4ae2eb, kH3, 16, 8, 16, src);
    CHECK(o.ok && !o.reloadCalled && o.message.find("restart") != std::string::npos && mock.texReloads == 2);
    o = tex::replaceTile(io, nullptr, root, 0x0e4ae2eb, kH3, 16, 8, 16, src);
    CHECK(o.ok && !o.reloadCalled);

    // Dumping: allowed on the first instance only.
    FakeOptions f2;
    tex::OptionIO io2 = f2.io();
    CHECK(tex::dumpingAllowed(1) && !tex::dumpingAllowed(2));
    CHECK(tex::setDumping(io2, true, 1) && tex::optionOn(io2, tex::kOptDump));
    CHECK(tex::setDumping(io2, false, 1) && !tex::optionOn(io2, tex::kOptDump));
    f2.sets.clear();
    CHECK(!tex::setDumping(io2, true, 2) && f2.sets.empty());
    CHECK(tex::setDumping(io2, false, 2));              // turning it off is always fine
    fs::remove_all(root);
}

static void testLru() {
    tex::LruKeys l(3);
    CHECK(l.touch("a").empty() && l.touch("b").empty() && l.touch("c").empty());
    CHECK(l.touch("a").empty());                          // a is now most recent: order a c b
    std::vector<std::string> ev = l.touch("d");           // evicts b
    CHECK(ev.size() == 1 && ev[0] == "b");
    CHECK(l.contains("a") && l.contains("c") && l.contains("d") && !l.contains("b") && l.size() == 3);
    l.erase("c");
    CHECK(!l.contains("c") && l.size() == 2);
    l.erase("zzz");
    CHECK(l.touch("e").empty() && l.touch("f").size() == 1);   // 4 keys do not fit in 3
    l.clear();
    CHECK(l.size() == 0);
    tex::LruKeys one(0);                                   // capacity floors at 1
    CHECK(one.capacity() == 1 && one.touch("x").empty() && one.touch("y").size() == 1);
}

int main() {
    testNamesAndPaths();
    testManifestParse();
    testRealCoreOutput();
    testManifestPartialLines();
    testDumpIndexIncremental();
    testPackDirListing();
    testPngRules();
    testInstallAndExport();
    testReplaceFlow();
    testLru();
    std::printf("debug_texture_test: %d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
