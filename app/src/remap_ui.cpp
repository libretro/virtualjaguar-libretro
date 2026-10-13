// ImGui for the remapping layer: the Controls tab (profiles, per-game overrides,
// "press a button to bind") and the Jaguar keypad overlay window.
#include "app.h"
#include "devices.h"
#include "libretro.h"
#include "imgui.h"

#include <cstdio>
#include <cstring>

namespace vj {

namespace {

std::string bindingsText(const std::vector<Binding>& v) {
    if (v.empty()) return "(unbound)";
    std::string s;
    for (const Binding& b : v) {
        if (!s.empty()) s += ", ";
        s += b.display();
    }
    return s;
}

// The "From" column is narrow: the long forms ("global, this controller") are in the tooltip.
const char* sourceShort(Source s) {
    switch (s) {
    case SRC_DEFAULT: return "default";
    case SRC_GLOBAL: return "global";
    case SRC_GLOBAL_PAD: return "controller";
    case SRC_GAME: return "this game";
    case SRC_GAME_PAD: return "game+pad";
    }
    return "";
}

struct Section {
    const char* title;
    Ctl first, last;
};
// The controls table is long; the sections say which device each group belongs to.
const Section kSections[] = {
    {"Standard pad", C_UP, C_PAUSE},
    {"Keypad (the Pro Controller's X Y Z and shoulders are keypad 9 8 7 and 4 6)", C_K0, C_HASH},
    {"Analog, driving, 6D and paddle controllers", C_D, C_AX_R1},
    {"Pointer: mouse, rotary and light gun (motion is also taken from the mouse itself)", C_PT_L, C_GUN_RELOAD},
    {"App", C_OVERLAY, C_OVERLAY},
    {"App", C_HELP, C_HELP},
};

}  // namespace

void drawControlsTab(App& app) {
    InputManager& in = app.input;
    BindingStore* st = in.store();
    if (!st) return;

    static int device = 0;   // 0 keyboard, 1 every gamepad, 2+ one connected controller model
    static int scope = 0;    // 0 global, 1 this game

    std::vector<InputManager::PadInfo> pads = in.pads();
    if (device >= 2 + (int)pads.size()) device = 1;

    // ---- which layer is being edited ----------------------------------------------------
    std::string devLabel = device == 0 ? "Keyboard" : device == 1 ? "Gamepads (every controller)"
                                                                  : pads[(size_t)device - 2].name + " (this model)";
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 22.0f);
    if (ImGui::BeginCombo("Device", devLabel.c_str())) {
        if (ImGui::Selectable("Keyboard", device == 0)) device = 0;
        if (ImGui::Selectable("Gamepads (every controller)", device == 1)) device = 1;
        for (size_t i = 0; i < pads.size(); i++) {
            std::string l = pads[i].name + " (this model, port " + std::to_string(pads[i].port + 1) + ")";
            if (ImGui::Selectable(l.c_str(), device == (int)i + 2)) device = (int)i + 2;
        }
        ImGui::EndCombo();
    }
    const bool haveGame = !in.content().empty();
    if (!haveGame) scope = 0;
    ImGui::RadioButton("Everywhere", &scope, 0);
    ImGui::SameLine();
    ImGui::BeginDisabled(!haveGame);
    ImGui::RadioButton("This game only", &scope, 1);
    ImGui::EndDisabled();
    if (haveGame) {
        ImGui::SameLine();
        ImGui::TextDisabled("(%s)", in.content().c_str());
    }

    LayerId layer;
    layer.cls = device == 0 ? DeviceClass::Keyboard : DeviceClass::Gamepad;
    layer.guid = device >= 2 ? pads[(size_t)device - 2].guid : "";
    layer.gameId = scope == 1 ? in.content() : "";

    if (device == 0) {
        int kp = st->keyboardPort();
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 11.0f);
        if (ImGui::Combo("Keyboard plays", &kp, "Jaguar port 1\0Jaguar port 2\0")) {
            st->setKeyboardPort(kp);
            app.cfg.save();
        }
    } else {
        ImGui::Checkbox("Left stick also drives the D-pad", &in.stickAsDpad);
        if (pads.empty()) ImGui::TextDisabled("No gamepad connected.");
        else
            for (const InputManager::PadInfo& p : pads)
                ImGui::BulletText("%s: %s", p.slot >= 0 ? slotName((unsigned)p.slot).c_str() : "(not assigned)", p.name.c_str());
        ImGui::TextDisabled("Which controller plays which player: Settings > Input > Players.");
    }

    // ---- caveats the core imposes --------------------------------------------------------
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("Keypad keys 7-9, * and # have no RetroPad button; the app presses them as keyboard keys the core reads.");
    ImGui::PopStyleColor();

    // ---- the table -----------------------------------------------------------------------------
    Profile prof = st->resolve(layer.cls, layer.guid, layer.gameId);
    bool dirty = false;
    ImGui::Separator();
    if (in.capturing()) {
        ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "Press %s for \"%s\"  (Esc cancels)",
                           layer.cls == DeviceClass::Keyboard ? "a key" : "a gamepad button or push a stick/trigger",
                           ctlLabel(in.captureTarget()));
    }
    if (ImGui::SmallButton("Reset this layer")) {
        st->clearLayer(layer);
        dirty = true;
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%s", scope == 1 ? "overrides for this game" : "your profile; built-in defaults underneath");

    ImGuiTableFlags tf = ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_ScrollY;
    if (ImGui::BeginTable("##binds", 4, tf, ImVec2(0, ImGui::GetFontSize() * 17.0f))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Control", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 7.0f);
        ImGui::TableSetupColumn("Bound to");
        ImGui::TableSetupColumn("From", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 6.5f);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 14.0f);
        ImGui::TableHeadersRow();
        for (const Section& sec : kSections) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextDisabled("%s", sec.title);
            for (int i = sec.first; i <= sec.last; i++) {
                Ctl c = (Ctl)i;
                ImGui::PushID(i);
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(ctlLabel(c));
                ImGui::TableNextColumn();
                if (in.capturing() && in.captureTarget() == c) ImGui::TextDisabled("...waiting for input...");
                else ImGui::TextWrapped("%s", bindingsText(prof.binds[(size_t)i]).c_str());
                ImGui::TableNextColumn();
                ImGui::TextDisabled("%s", sourceShort(prof.source[(size_t)i]));
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", sourceLabel(prof.source[(size_t)i]));
                ImGui::TableNextColumn();
                if (ImGui::SmallButton("Set")) in.beginCapture(layer, c, false);
                ImGui::SameLine();
                if (ImGui::SmallButton("Add")) in.beginCapture(layer, c, true);
                ImGui::SameLine();
                if (ImGui::SmallButton("Unbind")) {
                    st->set(layer, c, {});
                    dirty = true;
                }
                ImGui::SameLine();
                ImGui::BeginDisabled(!st->defined(layer, c));
                if (ImGui::SmallButton("Default")) {
                    st->clear(layer, c);
                    dirty = true;
                }
                ImGui::EndDisabled();
                ImGui::PopID();
            }
        }
        ImGui::EndTable();
    }
    if (dirty) {
        app.cfg.save();
        in.reresolve();
    }
    ImGui::Spacing();
    if (ImGui::Button("Keypad overlay")) in.overlay.open = !in.overlay.open;
    ImGui::SameLine();
    ImGui::TextDisabled("Opens the on-screen keypad (F2 or the 'Keypad overlay' control).");
    if (ImGui::Button("Controls overlay")) app.input.controls.show = !app.input.controls.show; app.input.controls.hideAt = 0;
    ImGui::SameLine();
    ImGui::TextDisabled("A cheat sheet of Player 1's bindings (F3, or the 'Controls overlay' control).");
}

void drawKeypadOverlay(App& app) {
    KeypadOverlay& ov = app.input.overlay;
    if (!ov.open) return;
    ImGuiViewport* vp = ImGui::GetMainViewport();
    float f = ImGui::GetFontSize();
    // Bottom-right corner, clear of the OSD and the menu bar.
    ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + vp->Size.x - f, vp->Pos.y + vp->Size.y - f * 2.5f),
                            ImGuiCond_FirstUseEver, ImVec2(1.0f, 1.0f));
    ImGuiWindowFlags wf = ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse |
                          ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;
    if (!ImGui::Begin("Jaguar keypad", &ov.open, wf)) {
        ImGui::End();
        return;
    }
    int p = (int)ov.port;
    ImGui::RadioButton("Player 1", &p, 0);
    ImGui::SameLine();
    ImGui::RadioButton("Player 2", &p, 1);
    ov.port = (unsigned)p;
    ImVec2 sz(f * 3.4f, f * 2.6f);
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 3; c++) {
            Ctl key = KeypadOverlay::keyAt(r, c);
            const char* label = key == C_STAR ? "*" : key == C_HASH ? "#" : nullptr;
            char buf[8];
            if (!label) {
                std::snprintf(buf, sizeof buf, "%d", (int)(key - C_K0));
                label = buf;
            }
            bool cursor = (r == ov.row && c == ov.col);
            ImGui::PushID(r * 3 + c);
            if (cursor) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.25f, 0.5f, 0.85f, 1.0f));
            if (ImGui::Button(label, sz)) {
                ov.row = r;
                ov.col = c;
                ov.tap(key);
            }
            if (cursor) ImGui::PopStyleColor();
            ImGui::PopID();
            if (c < 2) ImGui::SameLine();
        }
    }
    ImGui::TextDisabled("Gamepad: D-pad moves, A/B presses, C closes.");
    ImGui::TextDisabled("The game keeps running underneath.");
    ImGui::End();
}

}  // namespace vj
