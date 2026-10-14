// The Tools menu, the "halted" bar and the hook every debug tool window draws through.  The logic lives
// in DebugSession (debug_session.*), which has no ImGui and is unit-tested against the mock core.
#include "app.h"
#include "blitter_ui.h"
#include "cpu_ui.h"
#include "memory_ui.h"
#include "op_ui.h"
#include "scanner_ui.h"
#include "scope_ui.h"
#include "texture_ui.h"
#include "ui_util.h"
#include "imgui.h"

namespace vj {

namespace {

const char* targetName(int t) {
    switch (t) {
    case VJ_DBG_TARGET_M68K: return "68000";
    case VJ_DBG_TARGET_GPU: return "GPU";
    case VJ_DBG_TARGET_DSP: return "DSP";
    default: return "?";
    }
}

const char* reasonName(int r) {
    switch (r) {
    case VJ_DBG_STOP_BREAKPOINT: return "breakpoint";
    case VJ_DBG_STOP_STEP: return "step";
    case VJ_DBG_STOP_USER: return "break";
    default: return "";
    }
}

}  // namespace

void drawToolsMenu(App& app) {
    DebugSession& d = app.debug;
    const bool halted = d.halted();
    // "###tools" keeps the menu's identity (and its open state) when the label changes while halted.
    if (!ImGui::BeginMenu(halted ? "Tools - HALTED###tools" : "Tools###tools")) return;
    const bool on = d.enabled();
    if (!on) {
        ImGui::TextDisabled("%s", d.disabledReason().c_str());
        ImGui::Separator();
    }
    for (const DebugTool& t : d.tools()) {
        const bool avail = d.toolAvailable(t);
        const bool open = t.isOpen && t.isOpen();
        if (ImGui::MenuItem(t.label.c_str(), t.shortcut.empty() ? nullptr : t.shortcut.c_str(), open, avail) && t.toggle)
            t.toggle();
        if (on && !t.toggle && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Not available yet");
        else if (on && t.toggle && t.needsHalt && !halted && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Break into the game first (F9)");
    }
    ImGui::Separator();
    if (halted) {
        if (ImGui::MenuItem("Continue", "F9")) d.resume();
        if (ImGui::MenuItem("Step", "F10")) d.step();
    } else if (ImGui::MenuItem("Break", "F9", false, on)) {
        d.breakNow();
    }
    if (ImGui::MenuItem("Detach debugger", nullptr, false, d.attached())) d.detach();
    ImGui::EndMenu();
}

void drawDebugOverlay(App& app, float areaX, float areaY) {
    DebugSession& d = app.debug;
    if (!d.halted()) return;
    ImGui::SetNextWindowPos(ImVec2(areaX + 10, areaY + 10), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.85f);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                                   ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                                   ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove;
    if (ImGui::Begin("##debug_halt_bar", nullptr, flags)) {
        ImGui::TextColored(ui::vec4(ui::pal().warning), "HALTED");
        ImGui::SameLine();
        ImGui::Text("%s at $%06X", targetName(d.haltedTarget()), d.haltedPc());
        const char* why = reasonName(d.haltReason());
        if (*why) {
            ImGui::SameLine();
            ImGui::TextDisabled("(%s)", why);
        }
        if (ImGui::Button("Continue  F9")) d.resume();
        ImGui::SameLine();
        if (ImGui::Button("Step  F10")) d.step();
        ImGui::SameLine();
        if (ImGui::Button("Detach")) d.detach();
    }
    ImGui::End();
}

void drawDebugWindows(App& app) { app.debug.drawTools(); }

// One provider per tool file: `registerTools` puts the Tools-menu entry in place of the greyed placeholder (and
// hooks the forced-close path), `draw` runs once per UI frame -- also during a halt -- and the window checks its
// own open state.  Nothing else in the app calls a tool's draw function.
void addDebugTools(App& app) {
    DebugSession& d = app.debug;
    d.addProvider({[](DebugSession& s) { registerMemoryTools(s); },     // Memory viewer + Palette (CLUT)
                   [&app](DebugSession& s) { drawMemoryTools(s, &app.cheats); }});
    d.addProvider({[](DebugSession& s) { registerScannerTool(s); },
                   [&app](DebugSession& s) { drawScanner(s, app.cheats, app.host); }});
    d.addProvider({[](DebugSession& s) { registerObjectsTool(s); },
                   [&app](DebugSession&) { drawObjectsTool(app); }});
    d.addProvider({[](DebugSession& s) { blitterToolInit(s); },
                   [](DebugSession&) { blitterToolDraw(); }});
    // The Textures window reads paths.instance (settled by now) and opens file dialogs on the main window.
    d.addProvider({[&app](DebugSession& s) { textureToolInit(s, app.host, app.paths, [&app] { return app.window(); }); },
                   [](DebugSession&) { textureToolDraw(); }});
    // The scope chains onto host.onAudio, so App must have wired that callback before this call.
    d.addProvider({[&app](DebugSession& s) { scopeToolInit(s, app.host); },
                   [](DebugSession&) { scopeToolDraw(); }});
    d.addProvider({registerCpuTools, drawCpuTools});                    // CPU debugger: 68000 / GPU / DSP
}

void shutdownDebugTools() {
    textureToolShutdown();
    releaseObjectsTextures();
}

bool openDebugWindow(DebugSession& session, const std::string& name) {
    const std::string id = name == "palette" ? "clut" : name;
    DebugTool* t = session.findTool(id);
    if (!t || !t->toggle) return false;
    if (!(t->isOpen && t->isOpen())) t->toggle();
    return true;
}

}  // namespace vj
