#include "bitmap_view.h"
#include "debug_mock.h"
#include "debug_ui_fixture.h"
#include <cstdio>
#include <filesystem>
using namespace vj;
static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::fprintf(stderr, "FAIL %d: %s\n", __LINE__, #c); failures++; } } while (0)

int main() {
    MockDebugIface mock;
    BitmapSettings q;
    q.address = MockDebugIface::kBitmapData;
    q.width = 16; q.height = 4; q.depth = 3;
    q.end = q.address + 64;
    OpImage raw, op;
    std::string err;
    CHECK(bitmapRender(mock.iface(), q, raw, err));
    auto obj = opDecode(0, 2, ((uint64_t)4 << 14) | ((uint64_t)(q.address >> 3) << 43),
                        ((uint64_t)3 << 12) | ((uint64_t)1 << 15) | ((uint64_t)2 << 18) | ((uint64_t)2 << 28), 0);
    CHECK(opRender(mock.iface(), obj, {}, op, err));
    CHECK(raw.rgba == op.rgba);
    CHECK(raw.raw == op.raw);
    // Every depth, odd widths, unaligned addresses, exact end of region, no bus wrap.
    for (unsigned d = 0; d < 6; d++) {
        q.depth = d; q.width = 13; q.height = 3; q.address = 0x1fff00 + 1;
        q.end = q.address + bitmapRowBytes(q.width, d) * 3;
        const unsigned row = bitmapRowBytes(q.width, d);
        for (unsigned y = 0; y < 3; y++)
            std::fill_n(mock.dram().begin() + q.address + y * row, row, (uint8_t)(y == 1 ? 0xe4 : 0x1b));
        CHECK(bitmapRender(mock.iface(), q, raw, err));
        const unsigned expected[] = {1, 3, 14, 0xe4, 0xe4e4, 0xe4e4e4};
        CHECK(raw.rawAt(0, 1) == expected[d]);
        CHECK(raw.w == 13 && raw.h == 3 && !raw.unreadable);
        q.height = 0;
        CHECK(bitmapRender(mock.iface(), q, raw, err));
        CHECK(raw.h == 3);
    }
    // Aligned raw images match genuine decoded OP objects at every depth and colour mode.
    for (unsigned d = 0; d < 6; d++) for (unsigned mode = 0; mode < 4; mode++) {
        BitmapSettings aligned;
        aligned.address = 0x10000; aligned.depth = d; aligned.width = d == 5 ? 4 : 128 / opBitsPerPixel(d);
        aligned.height = 4; aligned.color = (OpColorMode)mode;
        const uint64_t p0 = ((uint64_t)4 << 14) | ((uint64_t)(aligned.address >> 3) << 43);
        const uint64_t p1 = ((uint64_t)d << 12) | ((uint64_t)1 << 15) | ((uint64_t)2 << 18) | ((uint64_t)2 << 28);
        OpRenderOptions opt; opt.color = aligned.color;
        CHECK(bitmapRender(mock.iface(), aligned, raw, err));
        CHECK(opRender(mock.iface(), opDecode(0, 2, p0, p1, 0), opt, op, err));
        CHECK(raw.rgba == op.rgba);
    }
    for (unsigned addr : {0x800001u, 0xf03001u, 0xf1b001u}) {
        BitmapSettings mapped; mapped.address = addr; mapped.end = addr + 64;
        mapped.width = 8; mapped.height = 2;
        CHECK(bitmapRender(mock.iface(), mapped, raw, err) && !raw.unreadable);
    }
    q.address = q.end;
    CHECK(!bitmapRender(mock.iface(), q, raw, err));
    q.address = 0xffffff; q.end = 0x1000000; q.depth = 4; q.width = 1;
    CHECK(!bitmapRender(mock.iface(), q, raw, err));
    q = BitmapSettings(); q.address = 0x10000; q.end = 0x11000; q.width = 16; q.height = 2;
    q.clutAddress = 0x12000;
    mock.dram()[0x12000] = 0xf8; mock.dram()[0x12001] = 0;
    mock.dram()[0x10000] = 0;
    q.color = OpColorMode::Rgb16;
    CHECK(bitmapRender(mock.iface(), q, raw, err));
    CHECK(raw.rgba[0] == 248 && raw.rgba[1] == 0 && raw.rgba[2] == 0);
    const auto path = std::filesystem::temp_directory_path() / "vj-bitmap-test.png";
    CHECK(opSavePng(raw, path.string(), err));
    CHECK(std::filesystem::file_size(path) > 0);
    std::filesystem::remove(path);
    // Direct colour never dereferences the CLUT, even if its saved address is invalid.
    q.clutAddress = 0xffffff;
    for (unsigned d : {4u, 5u}) {
        q.depth = d;
        CHECK(bitmapRender(mock.iface(), q, raw, err));
    }
    q.depth = 3;
    CHECK(!bitmapRender(mock.iface(), q, raw, err));
    // Adjacent rows share a random horizontal signature, with a slowly changing intensity.
    // Distinct chroma/intensity distributions in the 16-bit fixture; matched in 8-bit.
    for (unsigned depth : {3u, 4u}) {
        const unsigned width = 80, height = 64, bytes = depth == 3 ? 1 : 2;
        unsigned rng = 77;
        std::vector<uint8_t> signature(width);
        for (auto& v : signature) { rng = rng * 1664525u + 1013904223u; v = (uint8_t)(rng >> 24); }
        for (unsigned y = 0; y < height; y++) for (unsigned x = 0; x < width; x++) {
            unsigned at = 0x10000 + (y * width + x) * bytes;
            mock.dram()[at] = bytes == 1 ? signature[x & ~1u] : (uint8_t)(0xa0 | (signature[x] & 15));
            if (bytes == 2) mock.dram()[at + 1] = (uint8_t)(16 + (signature[x] & 63) + y);
        }
        auto g = bitmapGuess(mock.iface(), 0x10000, width * height * bytes, depth);
        CHECK(!g.candidates.empty());
        if (!g.candidates.empty()) CHECK(g.candidates[0].width == width);
        CHECK(g.hint == (bytes == 1 ? BitmapDepthHint::Indexed8 : BitmapDepthHint::Color16));
    }
    std::fill(mock.dram().begin(), mock.dram().end(), 0);
    auto g = bitmapGuess(mock.iface(), 0x10000, 4096, 3);
    CHECK(g.candidates.empty() && g.hint == BitmapDepthHint::Unknown);
    CHECK(bitmapGuess(nullptr, 0, 4096, 3).candidates.empty());
    CHECK(bitmapGuess(mock.iface(), 0, 3, 3).hint == BitmapDepthHint::Unknown);
    {
        CountingIface::reset();
        UiFixture fx;
        BitmapViewModel vm;
        vm.settings.address = MockDebugIface::kBitmapData;
        vm.settings.width = 16; vm.settings.height = 4;
        vm.update(fx.s); vm.update(fx.s);
        CHECK(vm.result().ok);
        auto mark = fx.mark();
        vm.update(fx.s);
        CHECK(fx.quietSince(mark));
        vm.guess(fx.s); vm.update(fx.s);
        CHECK(CountingIface::offCore() == 0);
        // Delayed jobs: settings edits and core epochs must reject old pictures.
        std::vector<std::function<void()>> queued;
        fx.s.corePost = [&](std::function<void()> fn) { queued.push_back(std::move(fn)); };
        vm.changed(); vm.update(fx.s);
        CHECK(!queued.empty());
        vm.settings.width = 8; vm.changed();
        { CountingIface::OnCore on; for (auto& fn : queued) fn(); }
        queued.clear();
        vm.update(fx.s);
        CHECK(!vm.result().ok); // old width 16 result was discarded
        { CountingIface::OnCore on; for (auto& fn : queued) fn(); }
        queued.clear(); vm.update(fx.s);
        CHECK(vm.result().ok && vm.result().image.w == 8);
        vm.changed(); vm.update(fx.s);
        vm.clear();
        { CountingIface::OnCore on; for (auto& fn : queued) fn(); }
        queued.clear();
        CHECK(!vm.result().ok);
        CHECK(CountingIface::offCore() == 0);
        fx.s.corePost = {};
    }
    std::printf("bitmap: %d failures\n", failures);
    return failures ? 1 : 0;
}
