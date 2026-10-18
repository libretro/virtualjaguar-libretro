// ImGui for the remapping layer: the Controls tab (profiles, per-game overrides,
// "press a button to bind") and the Jaguar keypad overlay window.
#include "app.h"
#include "devices.h"
#include "libretro.h"
#include "imgui.h"
#include "ui_util.h"

#include <cstdio>
#include <cstring>
#include <iterator>

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

// The every-gamepad view of the TV menu row: the Share hold default also means Guide on pads without Share.
std::string tvMenuText(const std::vector<Binding>& v) {
    if (v.empty()) return "(unbound)";
    const Binding share = Binding::button(SDL_GAMEPAD_BUTTON_MISC1).withHold();
    std::string s;
    for (const Binding& b : v) {
        if (!s.empty()) s += ", ";
        s += b == share ? std::string("Share (hold); Guide (hold) on pads without Share") : b.display();
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
    {"Pro Controller (6-button): read on a Pro Controller port only", C_PRO_X, C_PRO_R},
    {"Analog, driving, 6D and paddle controllers", C_D, C_AX_R1},
    {"Pointer: mouse, rotary and light gun (motion is also taken from the mouse itself)", C_PT_L, C_GUN_RELOAD},
    {"App", C_OVERLAY, C_OVERLAY},
    {"App", C_HELP, C_HELP},
    {"App", C_TVMENU, C_TVMENU},
};


// Rebinding the library / TV menu's pad buttons (#1021, pad_profile.h): the model's family and one button per intent.  All
// of it is the GLOBAL per-model layer (`input.padguid.<guid>.nav.*`), so the "This game only" scope does not apply to it.
// `pad` null = the every-gamepad view: the last-used pad's family table, read-only.
void drawMenuButtons(App& app, const InputManager::PadInfo* pad, bool gameScope) {
    InputManager& in = app.input;
    if (!ImGui::CollapsingHeader("Menu buttons")) return;
    const PadFamily fam = pad ? pad->nav.family : in.lastPadFamily();
    const NavProfile shown = pad ? pad->nav : familyNavProfile(fam);
    const std::string prefix = pad ? navKeyPrefix(pad->guid) : std::string();
    if (!pad) {
        ui::hint("Menu buttons are set per controller model: pick one in the Device list. This is the %s layout the last pad you "
                 "used gets by default.",
                 familyName(fam));
    } else {
        ui::hint("Which button does what in the library and the TV menu. The D-pad and left stick always move. Saved for this "
                 "controller model, for every game.");
        if (gameScope) ui::hint("Menu buttons are global: this edits the controller's layer, not just this game.");
    }

    bool dirty = false;
    if (pad) {
        // "Auto" follows what SDL says the pad is; a family choice also clears the model's per-button changes (setNavFamily),
        // so it is applied only when the choice actually changes.
        PadFamily cur = pad->detected;
        const bool overridden = pad->nav.familyOverridden;
        if (overridden) cur = pad->nav.family;
        const std::string autoLabel = std::string("Auto (") + familyName(pad->detected) + ")";
        const PadFamily kFamilies[] = {PadFamily::Xbox, PadFamily::PlayStation, PadFamily::Switch, PadFamily::Generic};
        ImGui::SetNextItemWidth(ui::fitWidth(0.4f, 10.0f, 18.0f));
        if (ImGui::BeginCombo("Family", overridden ? familyName(cur) : autoLabel.c_str())) {
            if (ImGui::Selectable(autoLabel.c_str(), !overridden) && overridden) {
                setNavFamily(app.cfg, pad->guid, "auto");
                dirty = true;
            }
            for (PadFamily f : kFamilies)
                if (ImGui::Selectable(familyName(f), overridden && cur == f) && !(overridden && cur == f)) {
                    setNavFamily(app.cfg, pad->guid, familyKey(f));
                    dirty = true;
                }
            ImGui::EndCombo();
        }
    }

    if (ImGui::BeginTable("##menubtns", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Does", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 7.5f);
        ImGui::TableSetupColumn("Button", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed,
                                ui::Flow::buttonW("Set") + ui::Flow::buttonW("Default") + ImGui::GetStyle().ItemSpacing.x +
                                    ImGui::GetStyle().CellPadding.x);
        for (size_t i = 0; i < std::size(kNavIntents); i++) {
            const tv::Intent it = kNavIntents[i];
            ImGui::PushID((int)i);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(navIntentLabel(it));
            ImGui::TableNextColumn();
            if (in.navCapturing() && in.navCaptureIntent() == it) ImGui::TextDisabled("...waiting for input...");
            else ImGui::TextUnformatted(navButtonName(fam, shown.binds[i]).c_str());
            ImGui::TableNextColumn();
            if (pad) {
                const std::string key = prefix + navIntentKey(it);
                if (ImGui::SmallButton("Set")) in.beginNavCapture(pad->guid, it);
                ImGui::SameLine();
                ImGui::BeginDisabled(!app.cfg.has(key));
                if (ImGui::SmallButton("Default")) in.resetNavBinding(pad->guid, it);   // saves and re-resolves
                ImGui::EndDisabled();
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    if (pad) {
        ImGui::BeginDisabled(app.cfg.withPrefix(prefix).empty());
        if (ImGui::SmallButton("Reset menu buttons")) {
            clearNavProfile(app.cfg, pad->guid);
            dirty = true;
        }
        ImGui::EndDisabled();
        // What the last Set did (it names the model it was for: a Set on another controller is not news here).
        const InputManager::NavResult& r = in.lastNav();
        if (r.valid && r.guid == pad->guid) {
            const std::string btn = navButtonName(fam, r.bound);
            if (r.set == NavSet::Swapped)
                ui::hint("Swapped with %s: it now has the button %s had.", navIntentLabel(r.other), navIntentLabel(r.intent));
            else if (r.set == NavSet::Reserved)
                ui::coloredWrapped(ui::vec4(ui::pal().warning),
                                   (btn + " is reserved (D-pad, left stick, or the TV-menu button).").c_str());
        }
    }
    if (dirty) {
        in.clearNavResult();
        app.cfg.save();
        in.reresolve();
    }
}

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
    ImGui::SetNextItemWidth(ui::fitWidth(0.6f, 12.0f, 28.0f));
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
    if (haveGame) ui::hint("Game: %s", in.content().c_str());

    LayerId layer;
    layer.cls = device == 0 ? DeviceClass::Keyboard : DeviceClass::Gamepad;
    layer.guid = device >= 2 ? pads[(size_t)device - 2].guid : "";
    layer.gameId = scope == 1 ? in.content() : "";

    if (device == 0) {
        int kp = st->keyboardPort();
        ImGui::SetNextItemWidth(ui::fitWidth(0.4f, 9.0f, 14.0f));
        if (ImGui::Combo("Keyboard plays", &kp, "Jaguar port 1\0Jaguar port 2\0")) {
            st->setKeyboardPort(kp);
            app.cfg.save();
        }
    } else {
        ImGui::Checkbox("Left stick also drives the D-pad", &in.stickAsDpad);
        if (pads.empty()) ui::hint("No gamepad connected.");
        else
            for (const InputManager::PadInfo& p : pads) {
                ImGui::Bullet();
                ImGui::TextWrapped("%s: %s", p.slot >= 0 ? slotName((unsigned)p.slot).c_str() : "(not assigned)", p.name.c_str());
            }
        ui::hint("Which controller plays which player: Settings > Input > Devices & players.");
    }

    // ---- caveats the core imposes --------------------------------------------------------
    ui::hint("Keypad keys 7-9, * and # have no RetroPad button; the app presses them as keyboard keys the core reads.");

    // ---- the menu buttons (the library / TV menu; per controller model, never per game) ----------------------------
    if (device >= 1) drawMenuButtons(app, device >= 2 ? &pads[(size_t)device - 2] : nullptr, scope == 1);

    // ---- the table -----------------------------------------------------------------------------
    // A specific controller shows what that pad gets (a pad without Share holds Guide for the TV menu); the
    // every-gamepad view shows the Share default and says what pads without Share do.
    PadCaps caps;
    if (device >= 2) caps.hasMisc1 = pads[(size_t)device - 2].hasMisc1;
    bool anyNoShare = false;
    for (const InputManager::PadInfo& p : pads)
        if (!p.hasMisc1) anyNoShare = true;
    // A gamepad driving a Pro Controller port plays the Pro defaults (keypad 0-4 give their buttons to X Y Z L R).
    const bool proView = layer.cls == DeviceClass::Gamepad && in.proPortFor(layer.guid);
    Profile prof = st->resolve(layer.cls, layer.guid, layer.gameId, caps, proView);
    if (proView)
        ui::hint("Showing the Pro Controller port defaults: X / Y / Z = Left Shoulder / North / Right Shoulder, L / R = the "
                 "triggers; keypad 0-4 are on the keypad overlay there.");
    bool dirty = false;
    ImGui::Separator();
    if (in.navCapturing()) {
        char m[192];
        std::snprintf(m, sizeof m, "Press a gamepad button for the menu's \"%s\"  (Esc cancels)", navIntentLabel(in.navCaptureIntent()));
        ui::coloredWrapped(ui::vec4(ui::pal().success), m);
    } else if (in.capturing()) {
        char m[192];
        std::snprintf(m, sizeof m, "Press %s for \"%s\"  (Esc cancels)",
                      layer.cls == DeviceClass::Keyboard ? "a key" : "a gamepad button or push a stick/trigger",
                      ctlLabel(in.captureTarget()));
        ui::coloredWrapped(ui::vec4(ui::pal().success), m);
    }
    if (ImGui::SmallButton("Reset this layer")) {
        st->clearLayer(layer);
        dirty = true;
    }
    ui::hint("%s", scope == 1 ? "Overrides for this game." : "Your profile; the built-in defaults are underneath.");

    ImGuiTableFlags tf = ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_ScrollY;
    // The table is as tall as the window allows (what is left minus room for the rows below it).
    const float tableH = std::min(ImGui::GetFontSize() * 40.0f,
                                  std::max(ImGui::GetFontSize() * 8.0f, ImGui::GetContentRegionAvail().y - ImGui::GetFontSize() * 7.0f));
    // Fixed columns for the short things; "Bound to" stretches (its text wraps) and gives way first.
    const float btnsW = ui::Flow::buttonW("Set") + ui::Flow::buttonW("Add") + ui::Flow::buttonW("Unbind") +
                        ui::Flow::buttonW("Default") + ImGui::GetStyle().ItemSpacing.x * 3 +
                        ui::Flow::buttonW("Add hold") + ImGui::GetStyle().ItemSpacing.x;   // the TV menu row's extra button
    if (ImGui::BeginTable("##binds", 3, tf, ImVec2(0, tableH))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Control", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 6.5f);
        ImGui::TableSetupColumn("Bound to", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, btnsW + ImGui::GetStyle().CellPadding.x);
        ImGui::TableHeadersRow();
        for (const Section& sec : kSections) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(1);
            ui::hint("%s", sec.title);
            for (int i = sec.first; i <= sec.last; i++) {
                Ctl c = (Ctl)i;
                ImGui::PushID(i);
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextWrapped("%s", ctlLabel(c));
                ImGui::TableNextColumn();
                if (in.capturing() && in.captureTarget() == c) ImGui::TextDisabled("...waiting for input...");
                else if (c == C_TVMENU && device == 1)
                    ImGui::TextWrapped("%s", tvMenuText(prof.binds[(size_t)i]).c_str());
                else ImGui::TextWrapped("%s", bindingsText(prof.binds[(size_t)i]).c_str());
                if (c == C_TVMENU) {
                    // Guide held opens the menu, and Guide is also the keypad's # (a tap): a chord that holds # collides.
                    // In the every-gamepad view that is also the case on any connected pad without Share.
                    bool guideHold = device == 1 && anyNoShare;
                    for (const Binding& b : prof.binds[(size_t)c])
                        if (b == Binding::button(SDL_GAMEPAD_BUTTON_GUIDE).withHold()) guideHold = true;
                    if (guideHold)
                        ui::hint("Guide (hold) opens the TV menu, so a keypad chord that holds # (such as * + #) needs the keypad "
                                 "overlay, a pad with Share, or this binding removed.");
                }
                // Where the binding comes from sits under it (short form; the long one is the tooltip).
                ImGui::TextDisabled("%s", sourceShort(prof.source[(size_t)i]));
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", sourceLabel(prof.source[(size_t)i]));
                ImGui::TableNextColumn();
                if (ImGui::SmallButton("Set")) in.beginCapture(layer, c, false);
                ImGui::SameLine();
                if (ImGui::SmallButton("Add")) in.beginCapture(layer, c, true);
                if (c == C_TVMENU) {   // the TV menu is also bound to a long press (a tap stays free for the button's other job)
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Add hold")) in.beginCapture(layer, c, true, true);
                }
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
    ui::hint("Opens the on-screen keypad (F2 or the 'Keypad overlay' control).");
    if (ImGui::Button("Controls overlay")) {
        overlayEvent(app.input.controls, OverlayEvent::Toggle);
    }
    ui::hint("A cheat sheet of Player 1's bindings (F3, or the 'Controls overlay' control).");
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
    // The keys are 3 across; the window is as wide as they are and the help text wraps to that.
    // In a small main window the keys shrink so the pad never takes more than half of its height.
    const float k = std::min(1.0f, vp->WorkSize.y * 0.5f / (f * 2.6f * 5.4f));
    const ImVec2 sz(f * 3.4f * k, f * 2.6f * k);
    const float gridW = sz.x * 3.0f + ImGui::GetStyle().ItemSpacing.x * 2.0f;
    ImGui::SetNextWindowSizeConstraints(ImVec2(gridW + ImGui::GetStyle().WindowPadding.x * 2.0f, 0.0f),
                                        ImVec2(vp->WorkSize.x * 0.95f, vp->WorkSize.y * 0.95f));
    bool shown = ImGui::Begin("Jaguar keypad", &ov.open, wf);
    ui::keepOnScreen();
    if (!shown) {
        ImGui::End();
        return;
    }
    int p = (int)ov.port;
    ImGui::RadioButton("Player 1", &p, 0);
    ImGui::SameLine();
    ImGui::RadioButton("Player 2", &p, 1);
    ov.port = (unsigned)p;
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
            if (cursor) ImGui::PushStyleColor(ImGuiCol_Button, ui::vec4(ui::pal().cursorButton));
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
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + std::max(gridW, f * 15.0f));
    ImGui::TextDisabled("Gamepad: D-pad moves, A/B presses, C closes. The game keeps running underneath.");
    ImGui::PopTextWrapPos();
    ImGui::End();
}

}  // namespace vj
