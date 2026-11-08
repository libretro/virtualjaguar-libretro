// Headless smoke test for the Symbols window (app/src/debug/symbols_ui.cpp, #1195): real ImGui frames over the mock core
// and the committed rln output.  Pins where the symbols come from, the search, and that a row's breakpoint lands on the
// processor its address belongs to.
#include "debug_mock.h"
#include "debug_session.h"
#include "debug_ui_fixture.h"
#include "symbols_ui.h"

#include "imgui.h"

#include <cstdio>
#include <string>

using namespace vj;

static int g_fail = 0, g_checks = 0;
#define CHECK(cond)                                                                \
    do {                                                                           \
        g_checks++;                                                                \
        if (!(cond)) {                                                             \
            g_fail++;                                                              \
            std::fprintf(stderr, "FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);   \
        }                                                                          \
    } while (0)

static void acknowledgeTextures() {
    for (ImTextureData* t : ImGui::GetPlatformIO().Textures) {
        if (t->Status == ImTextureStatus_WantCreate) t->SetTexID((ImTextureID)1);
        if (t->Status == ImTextureStatus_WantCreate || t->Status == ImTextureStatus_WantUpdates) t->SetStatus(ImTextureStatus_OK);
        else if (t->Status == ImTextureStatus_WantDestroy) t->SetStatus(ImTextureStatus_Destroyed);
    }
}

static UiFixture* g_fx = nullptr;

static void frame(DebugSession& s) {
    ImGui::GetIO().DeltaTime = 1.0f / 60.0f;
    ImGui::NewFrame();
    s.tick(0.0, false);
    drawSymbolsTool(s);
    ImGui::Render();
    acknowledgeTextures();
    g_fx->fill();
}

static bool contains(const std::string& haystack, const char* needle) { return haystack.find(needle) != std::string::npos; }

static size_t rowOf(const char* name) {
    for (size_t i = 0; i < symbols_test::rowCount(); i++)
        if (symbols_test::rowName(i) == name) return i;
    return (size_t)-1;
}

int main() {
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2(1100, 760);
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;

    UiFixture fx;
    g_fx = &fx;
    DebugSession& s = fx.s;
    const std::string dir = VJ_SYMBOL_FIXTURES;

    std::string content = dir + "/globals.cof";
    int reads = 0;
    SymbolsUiContext ctx;
    ctx.contentPath = [&content, &reads] {
        reads++;
        return content;
    };
    registerSymbolsTool(s, ctx);
    DebugTool* tool = s.findTool("symbols");
    CHECK(tool && tool->toggle);

    // 1. Closed, nothing is looked at.
    frame(s);
    CHECK(reads == 0);

    // 2. Open: the content's own table.
    tool->toggle();
    frame(s);
    CHECK(reads > 0);
    CHECK(symbols_test::rowCount() == 11);
    CHECK(contains(symbols_test::status(), "11 symbols from globals.cof"));

    // 3. The search narrows and clears, on names and on values; the default order is by value.
    CHECK(symbols_test::rowName(0) == "_start");
    symbols_test::setSearch("GPU_");
    frame(s);
    CHECK(symbols_test::rowCount() == 2);
    CHECK(symbols_test::rowName(0) == "gpu_blit");
    symbols_test::setSearch("$f03004");
    frame(s);
    CHECK(symbols_test::rowCount() == 1);
    CHECK(symbols_test::rowName(0) == "gpu_end");
    symbols_test::setSearch("no_such_symbol");
    frame(s);
    CHECK(symbols_test::rowCount() == 0);
    symbols_test::setSearch("");
    frame(s);
    CHECK(symbols_test::rowCount() == 11);

    // 4. A row's breakpoint goes to the processor that runs the address, and comes off again.
    symbols_test::toggleBreakpoint(s, rowOf("main_loop"));
    CHECK(s.hasBreakpoint(VJ_DBG_TARGET_M68K, 0x400A));
    CHECK(contains(symbols_test::status(), "Breakpoint set on main_loop"));
    symbols_test::toggleBreakpoint(s, rowOf("gpu_blit"));
    CHECK(s.hasBreakpoint(VJ_DBG_TARGET_GPU, 0xF03000));
    CHECK(!s.hasBreakpoint(VJ_DBG_TARGET_M68K, 0xF03000));
    frame(s);   // the rows draw with the boxes ticked
    symbols_test::toggleBreakpoint(s, rowOf("main_loop"));
    symbols_test::toggleBreakpoint(s, rowOf("gpu_blit"));
    CHECK(s.breakpoints().empty());
    CHECK(contains(symbols_test::status(), "Breakpoint removed from gpu_blit"));

    // 5. Another game: the table follows.  A linked-without-symbols file says what to do about it.
    content = dir + "/stripped.cof";
    frame(s);
    CHECK(symbols_test::rowCount() == 0);
    CHECK(contains(symbols_test::status(), "rln -s"));
    CHECK(!symbols_test::statusIsError());

    // 6. Content with no symbols of its own and nothing beside it, then a cartridge image with a .cof of the same name.
    content = dir + "/README.md";
    frame(s);
    CHECK(symbols_test::rowCount() == 0);
    CHECK(contains(symbols_test::status(), "carries no symbols"));
    content = dir + "/locals.j64";   // no such file: locals.cof beside it answers
    frame(s);
    CHECK(symbols_test::rowCount() == 12);
    CHECK(contains(symbols_test::status(), "12 symbols from locals.cof"));

    // 7. A picked file wins until the content changes; a bad one says so.
    symbolsToolFilePicked(dir + "/symbols.abs");
    frame(s);
    CHECK(symbols_test::rowCount() == 10);
    CHECK(contains(symbols_test::status(), "10 symbols from symbols.abs"));
    symbolsToolFilePicked(dir + "/symbols.s");
    frame(s);
    CHECK(symbols_test::rowCount() == 0);
    CHECK(symbols_test::statusIsError());
    content = dir + "/globals.cof";
    frame(s);
    CHECK(symbols_test::rowCount() == 11);

    // 8. Switching the tools off closes the window and drops the table.
    s.closeAllTools("hardcore");
    CHECK(!(tool->isOpen && tool->isOpen()));
    frame(s);
    CHECK(symbols_test::rowCount() == 0);

    ImGui::DestroyContext();
    std::printf("debug_symbols_ui_smoke: %d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
