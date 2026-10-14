#include "devices_ui.h"
#include "instance.h"

#include "app.h"
#include "libretro.h"
#include "ui_util.h"

#include <algorithm>
#include <cstdio>
#include <vector>

namespace vj {

// ---------------------------------------------------------------------------------
// claiming devices
// ---------------------------------------------------------------------------------
static std::string optionOf(const App& app, const char* key) {
    for (const OptionDef& d : app.host.optionDefs())
        if (d.key == key) return app.host.optionValue(d);
    return "";
}

void applyPortDevices(App& app) {
    const std::string game = app.input.content();
    DevKind kinds[2] = {DevKind::Pad, DevKind::Pad};
    for (unsigned p = 0; p < kJaguarPorts; p++) {
        int d = resolvePortDevice(app.cfg, p, game);
        unsigned claim = (d >= 0 && portOffers(p, (unsigned)d)) ? (unsigned)d : (unsigned)RETRO_DEVICE_JOYPAD;
        app.host.setControllerDevice(p, claim);   // JOYPAD releases the claim: the core option (locked to auto) decides
        kinds[p] = effectiveDevKind(claim == RETRO_DEVICE_JOYPAD ? -1 : (int)claim,
                                    optionOf(app, p == 0 ? "virtualjaguar_p1_device" : "virtualjaguar_p2_device"));
    }
    app.input.setPortKinds(kinds[0], kinds[1]);
    app.input.updateRoutes();
}

// ---------------------------------------------------------------------------------
// per frame
// ---------------------------------------------------------------------------------
void inputFrame(App& app) {
    InputManager& in = app.input;
    bool uiOpen = app.showSettings || app.showStates || app.showCheats || app.showAbout || app.library.show ||
                  in.overlay.open;
    bool wantsMouse = false;
    if (ImGui::GetCurrentContext()) {
        ImGuiIO& io = ImGui::GetIO();
        if (ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId)) uiOpen = true;
        // A captured mouse has no position, so what ImGui thinks it hovers is stale then.
        wantsMouse = io.WantCaptureMouse && !in.mouseCaptured();
        in.uiWantsKeyboard = io.WantCaptureKeyboard;
    }
    // The controls overlay is informational (NoInputs) and deliberately NOT part of uiOpen.
    in.syncMouseCapture(app.window(), app.host.isLoaded() && !app.paused && !app.debug.halted(), uiOpen || wantsMouse, wantsMouse);   // a debugger halt frees the mouse

    if (in.takeHelpToggle() && app.host.isLoaded()) {
        overlayEvent(in.controls, OverlayEvent::Toggle);
    }
    overlayTick(in.controls, app.now());
}

void controlsOverlayOnLoad(App& app) {
    InputManager& in = app.input;
    in.updateRoutes();
    AutoShowInput ai;
    ai.enabled = app.cfg.getBool("ui.controls.autoshow", false);
    ai.hintShown = in.controls.hintShown;
    ai.contentLoaded = app.host.isLoaded();
    int pad = in.routes().pad[0];
    ai.padOnPlayer1 = pad >= 0;
    std::string guid;
    if (pad >= 0) {
        std::vector<InputManager::PadInfo> pads = in.pads();
        if ((size_t)pad < pads.size()) guid = pads[(size_t)pad].guid;
    }
    std::string seen = "," + app.cfg.get("ui.controls.seen_pads", "") + ",";
    ai.padSeenBefore = !guid.empty() && seen.find("," + guid + ",") != std::string::npos;
    AutoShow a = autoShowDecision(ai);
    in.controls.lastInputWasPad = ai.padOnPlayer1;
    if (a == AutoShow::No) return;
    if (a == AutoShow::Hint) {
        in.controls.hintShown = true;
        app.toast("Press F3 for the controls", 4.0);
        return;
    }
    overlayAutoShow(in.controls, app.now());
    if (a == AutoShow::Gamepad && !guid.empty()) {
        std::string list = app.cfg.get("ui.controls.seen_pads", "");
        list += (list.empty() ? "" : ",") + guid;
        app.cfg.set("ui.controls.seen_pads", list);
    }
}

// ---------------------------------------------------------------------------------
// Settings > Input > Devices
// ---------------------------------------------------------------------------------
namespace {

struct Choice {
    std::string desc;
    int id;
};

std::vector<Choice> choicesFor(App& app, unsigned port) {
    std::vector<Choice> v;
    const auto& ports = app.host.controllerPorts();
    if (app.host.isLoaded() && port < ports.size()) {
        for (const ControllerType& t : ports[port]) v.push_back({t.desc, (int)t.id});   // SET_CONTROLLER_INFO
    } else {
        for (int i = 0; i < kDevKinds; i++)   // before a game loads: the same lists, from devices.cpp
            if (portOffers(port, devLibretroId((DevKind)i))) v.push_back({devKindLabel((DevKind)i), (int)devLibretroId((DevKind)i)});
    }
    return v;
}

void deviceChanged(App& app) {
    app.cfg.save();
    app.input.reresolve();
    applyPortDevices(app);
}

}  // namespace

// Which controllers THIS window listens to.  SDL gives every connected controller to every running
// copy of the app, so with two windows side by side (instance.h) both would answer the same pad;
// this pins a window to one pad, or to the keyboard.  Saved per instance number.
static void drawPadFilter(App& app) {
    InputManager& in = app.input;
    const std::vector<std::string> names = InputManager::connectedPadNames();
    const int cur = in.padFilter();
    std::string caption = cur < 0 ? "All controllers" : cur == 0 ? "Keyboard only (no controllers)" : "Controller " + std::to_string(cur);
    if (cur > 0 && (size_t)cur <= names.size()) caption += ": " + names[(size_t)cur - 1];
    else if (cur > 0) caption += " (not connected)";
    const std::string label = "Controllers used by this window" + instanceTitleSuffix(app.paths.instance);
    ImGui::SetNextItemWidth(ui::fitWidth(0.55f, 14.0f, 30.0f));
    int pick = -2;
    if (ImGui::BeginCombo("##padfilter", caption.c_str())) {
        if (ImGui::Selectable("All controllers", cur < 0)) pick = -1;
        if (ImGui::Selectable("Keyboard only (no controllers)", cur == 0)) pick = 0;
        for (size_t i = 0; i < names.size() && i < 9; i++) {
            std::string l = "Controller " + std::to_string(i + 1) + ": " + names[i];
            if (ImGui::Selectable(l.c_str(), cur == (int)i + 1)) pick = (int)i + 1;
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::TextUnformatted(label.c_str());
    if (pick != -2) {
        in.setPadFilter(pick);
        app.cfg.set(instancePadKey(app.paths.instance), padFilterValue(pick));
        app.cfg.save();
        deviceChanged(app);
    }
    ui::hint("Running two windows for local play or netplay tests? Give each its own controller here; "
             "the keyboard goes to whichever window has focus. Controller numbers follow the order they were connected.");
}

void drawDevicesPanel(App& app) {
    InputManager& in = app.input;
    drawPadFilter(app);
    ImGui::Separator();
    const std::string game = in.content();
    static int scope = 0;   // 0 everywhere, 1 this game
    if (game.empty()) scope = 0;
    const std::string sc = scope == 1 ? game : "";

    ImGui::RadioButton("Everywhere", &scope, 0);
    ImGui::SameLine();
    ImGui::BeginDisabled(game.empty());
    ImGui::RadioButton("This game only", &scope, 1);
    ImGui::EndDisabled();
    if (!game.empty()) {
        ui::hint("(%s)", game.c_str());
    }
    ui::hint("Choices apply to this scope; a game's own choice overrides the global one.");
    ImGui::Separator();

    // ---- ports ----------------------------------------------------------------------------------
    ImGui::SeparatorText("Controller ports");
    for (unsigned p = 0; p < kJaguarPorts; p++) {
        std::vector<Choice> ch = choicesFor(app, p);
        int cur = resolvePortDevice(app.cfg, p, sc);
        bool defined = portDeviceDefined(app.cfg, p, sc);
        std::string shown = ch.empty() ? "Standard Joypad" : ch[0].desc;
        for (const Choice& c : ch)
            if (c.id == cur) shown = c.desc;
        if (scope == 1 && !defined) shown = "(same as everywhere) " + shown;
        char label[32];
        std::snprintf(label, sizeof label, "Jaguar port %u", p + 1);
        ImGui::SetNextItemWidth(ui::fitWidth(0.55f, 14.0f, 30.0f));
        if (ImGui::BeginCombo(label, shown.c_str())) {
            if (scope == 1 && ImGui::Selectable("(same as everywhere)", !defined)) {
                clearPortDevice(app.cfg, p, sc);
                deviceChanged(app);
            }
            for (const Choice& c : ch) {
                if (ImGui::Selectable(c.desc.c_str(), c.id == cur && (defined || scope == 0))) {
                    setPortDevice(app.cfg, p, sc, c.id);
                    deviceChanged(app);
                }
            }
            ImGui::EndCombo();
        }
        DevKind k = in.portKind(p);
        ui::hint("Now: %s. %s", devKindLabel(k), devKindHelp(k));
    }
    if (!app.host.isLoaded()) ui::hint("The list is the core's own once a game is loaded.");

    // ---- players --------------------------------------------------------------------------------
    ImGui::SeparatorText("Players: which controller plays whom");
    in.updateRoutes();
    std::array<bool, kSlots> act = activeSlots(in.portKind(0) == DevKind::TeamTap, in.portKind(1) == DevKind::TeamTap);
    std::vector<InputManager::PadInfo> pads = in.pads();
    for (unsigned s = 0; s < kSlots; s++) {
        if (!act[s]) continue;
        SlotSource cur = resolveSlot(app.cfg, s, sc);
        bool defined = slotDefined(app.cfg, s, sc);
        // What it resolves to right now, for the combo's caption.
        std::string now = "nobody";
        int p = in.routes().pad[s];
        if (p >= 0 && (size_t)p < pads.size()) now = pads[(size_t)p].name;
        if (in.routes().kb[s]) now = p >= 0 ? now + " + keyboard" : "keyboard";
        std::string label = slotName(s);
        ImGui::PushID((int)s);
        ImGui::SetNextItemWidth(ui::fitWidth(0.55f, 14.0f, 30.0f));
        std::string caption = (scope == 1 && !defined ? "(same as everywhere) " : "") +
                              (cur.kind == SlotSource::Auto ? "Automatic" : cur.kind == SlotSource::None ? "Nobody"
                               : cur.kind == SlotSource::Keyboard ? "Keyboard" : "Gamepad: " + cur.guid.substr(0, 8)) +
                              "  ->  " + now;
        if (ImGui::BeginCombo(label.c_str(), caption.c_str())) {
            if (scope == 1 && ImGui::Selectable("(same as everywhere)", !defined)) {
                clearSlot(app.cfg, s, sc);
                deviceChanged(app);
            }
            auto pick = [&](const char* name, const SlotSource& src) {
                if (ImGui::Selectable(name, defined && cur == src)) {
                    setSlot(app.cfg, s, sc, src);
                    deviceChanged(app);
                }
            };
            SlotSource a;
            pick("Automatic (next free controller)", a);
            SlotSource k;
            k.kind = SlotSource::Keyboard;
            pick("Keyboard", k);
            SlotSource n;
            n.kind = SlotSource::None;
            pick("Nobody", n);
            for (const InputManager::PadInfo& pi : pads) {
                SlotSource g;
                g.kind = SlotSource::Pad;
                g.guid = pi.guid;
                g.instance = pi.instance;
                std::string nm = pi.name + (pi.instance ? " #" + std::to_string(pi.instance + 1) : "");
                pick(nm.c_str(), g);
            }
            ImGui::EndCombo();
        }
        ImGui::PopID();
    }
    int kp = in.store() ? in.store()->keyboardPort() : 0;
    ui::hint("With no explicit choice the keyboard plays %s and controllers are handed out in the order they were plugged in.",
                        kp == 0 ? "Player 1" : "Player 2");

    // ---- tuning ---------------------------------------------------------------------------------
    ImGui::SeparatorText("Tuning");
    DeviceSettings ds = DeviceSettings::load(app.cfg, sc);
    bool changed = false;
    auto slider = [&](const char* key, const char* label, float v, float lo, float hi, const char* fmt) {
        ImGui::SetNextItemWidth(ui::fitWidth(0.4f, 10.0f, 20.0f));
        if (ImGui::SliderFloat(label, &v, lo, hi, fmt, ImGuiSliderFlags_Logarithmic)) {
            char b[32];
            std::snprintf(b, sizeof b, "%.3f", v);
            DeviceSettings::set(app.cfg, sc, key, b);
            changed = true;
        }
    };
    slider("mouse_speed", "Mouse speed", ds.mouseSpeed, 0.1f, 8.0f, "%.2fx");
    slider("rotary_speed", "Rotary speed", ds.rotarySpeed, 0.1f, 8.0f, "%.2fx");
    slider("stick_speed", "Stick / key pointer speed", ds.stickSpeed, 0.1f, 8.0f, "%.2fx");
    int dz = ds.deadzonePct;
    ImGui::SetNextItemWidth(ui::fitWidth(0.4f, 10.0f, 20.0f));
    if (ImGui::SliderInt("Stick dead zone", &dz, 0, 40, "%d%%")) {
        DeviceSettings::set(app.cfg, sc, "deadzone", std::to_string(dz));
        changed = true;
    }
    int mp = ds.mousePort + 1;
    ImGui::SetNextItemWidth(ui::fitWidth(0.4f, 10.0f, 20.0f));
    if (ImGui::Combo("Mouse drives", &mp, "Automatic\0Jaguar port 1\0Jaguar port 2\0")) {
        DeviceSettings::set(app.cfg, sc, "mouse_port", std::to_string(mp - 1));
        changed = true;
    }
    int gs = ds.gunSource;
    ImGui::SetNextItemWidth(ui::fitWidth(0.4f, 10.0f, 20.0f));
    if (ImGui::Combo("Light gun aims with", &gs, "Automatic\0Mouse only\0Stick / keys only\0")) {
        DeviceSettings::set(app.cfg, sc, "gun_source", std::to_string(gs));
        changed = true;
    }
    ui::hint("Sensitivity, dead zone and response curve are also in Core options (applied inside the emulated device).");
    if (changed) deviceChanged(app);
    if (in.mouseWanted()) {
        ui::hint("The mouse is captured while the game has focus and no menu is open. Esc or F10 releases it; click the picture or press F10 to capture it again.");
    }
}

// Settings > Interface > Overlays: the controls overlay's auto-show rule and a button to toggle it now.
void drawControlsOverlaySettings(App& app) {
    InputManager& in = app.input;
    bool as = app.cfg.getBool("ui.controls.autoshow", false);
    if (ImGui::Checkbox("Show the controls overlay when a game starts", &as)) {
        app.cfg.setBool("ui.controls.autoshow", as);
        app.cfg.save();
    }
    ui::hint("Off by default: a keyboard player gets a short \"Press F3 for the controls\" hint instead. On: keyboard players see it at boot, "
             "gamepad players once per controller model, and it hides itself after a few seconds. A click, tap, Esc or F3 hides it.");
    if (ImGui::Button(in.controls.show ? "Hide the overlay now" : "Show the overlay now")) overlayEvent(in.controls, OverlayEvent::Toggle);
}

}  // namespace vj
