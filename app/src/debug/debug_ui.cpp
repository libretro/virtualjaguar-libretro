// The Tools menu, the "halted" bar and the hook every debug tool window draws through.  The logic lives
// in DebugSession (debug_session.*), which has no ImGui and is unit-tested against the mock core.
#include "app.h"
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
        const bool avail = on && t.toggle && (!t.needsHalt || halted);
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
        ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.35f, 1.0f), "HALTED");
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

void drawDebugWindows(App& app) {
    // A tool's draw hook may register or open things; walk a copy of the callables.
    std::vector<std::function<void()>> draws;
    for (const DebugTool& t : app.debug.tools())
        if (t.draw) draws.push_back(t.draw);
    for (auto& f : draws) f();
}

}  // namespace vj
