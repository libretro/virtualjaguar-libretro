#include "cheatsheet.h"

namespace vj {

std::string bindingsLabel(const std::vector<Binding>& v) {
    if (v.empty()) return "(unbound)";
    std::string s;
    for (const Binding& b : v) {
        if (!s.empty()) s += ", ";
        s += b.display();
    }
    return s;
}

std::string Sheet::text() const {
    std::string t = title + "\n";
    for (const SheetLine& l : lines) {
        if (l.heading) t += "\n" + l.label + "\n";
        else t += "  " + l.label + ": " + l.keys + "\n";
    }
    return t;
}

namespace {

struct Builder {
    const Profile& p;
    Sheet& s;
    void head(const char* t) {
        SheetLine l;
        l.label = t;
        l.heading = true;
        s.lines.push_back(l);
    }
    void line(const std::string& label, const std::string& keys) {
        SheetLine l;
        l.label = label;
        l.keys = keys;
        s.lines.push_back(l);
    }
    void ctl(Ctl c, const char* label = nullptr) { line(label ? label : ctlLabel(c), bindingsLabel(p.binds[(size_t)c])); }
    // Two controls shown on one line ("Z / Right trigger") would hide which is which; keep them apart.
    void dpad(const char* what = nullptr) {
        (void)what;
        ctl(C_UP);
        ctl(C_DOWN);
        ctl(C_LEFT);
        ctl(C_RIGHT);
    }
    void buttons() {
        ctl(C_A, "A");
        ctl(C_B, "B");
        ctl(C_C, "C");
        ctl(C_OPTION, "Option");
        ctl(C_PAUSE, "Pause");
    }
    void keypad() {
        for (int c = C_K0; c <= C_HASH; c++) ctl((Ctl)c);
    }
    void app() {
        head("App");
        ctl(C_OVERLAY);
        ctl(C_HELP);
        ctl(C_TVMENU);
    }
};

}  // namespace

Sheet buildSheet(const Profile& profile, DevKind kind, SheetSource src, const std::string& deviceName) {
    Sheet sh;
    Builder b{profile, sh};
    sh.title = std::string("Player 1: ") + devKindLabel(kind) + " - " +
               (src == SheetSource::Keyboard ? "keyboard" : deviceName.empty() ? "gamepad" : deviceName);
    const bool pad = src == SheetSource::Gamepad;
    switch (kind) {
    case DevKind::Pad:
    case DevKind::TeamTap:
        b.head("Directions");
        b.dpad();
        if (pad && profile.stickAsDpad) b.line("Left stick", "also the D-pad");
        b.head("Buttons");
        b.buttons();
        b.head("Keypad (also the Pro Controller's X Y Z and shoulders)");
        b.keypad();
        break;

    case DevKind::ProPad: {
        b.head("Directions");
        b.dpad();
        if (pad && profile.stickAsDpad) b.line("Left stick", "also the D-pad");
        b.head("Buttons");
        b.buttons();
        // Not headed "Keypad...": the overlay window folds those sections away in a small window.
        b.head("Pro buttons (the Jaguar reads them as keypad 9 8 7, 4 6)");
        static const struct { Ctl pro, key; } kPro[] = {
            {C_PRO_X, C_K9}, {C_PRO_Y, C_K8}, {C_PRO_Z, C_K7}, {C_PRO_L, C_K4}, {C_PRO_R, C_K6}};
        for (const auto& e : kPro) {
            // The keyboard has no Pro bindings of its own: its keypad keys are the same wires (A S D / Q E by default).
            if (profile.binds[(size_t)e.pro].empty() && !profile.binds[(size_t)e.key].empty())
                b.line(ctlLabel(e.pro), bindingsLabel(profile.binds[(size_t)e.key]) + " (keypad " +
                                            std::to_string(e.key - C_K0) + ")");
            else b.ctl(e.pro);
        }
        b.head("Keypad");
        b.keypad();
        bool noLow = true;   // the Pro defaults spend keypad 0-4's pad buttons on X Y Z L R
        for (int c = C_K0; c <= C_K4; c++) noLow = noLow && profile.binds[(size_t)c].empty();
        if (pad && noLow)
            b.line("Keypad 0-4", "no pad button on a Pro Controller port: use the keypad overlay (" +
                                     bindingsLabel(profile.binds[C_OVERLAY]) + ", or View > Keypad overlay)");
        break;
    }

    case DevKind::Rotary:
        b.head("Rotary");
        b.line("Spin", "mouse left / right");
        b.ctl(C_PT_L, "Spin counter-clockwise");
        b.ctl(C_PT_R, "Spin clockwise");
        b.head("Buttons");
        b.buttons();
        b.head("Keypad");
        b.keypad();
        break;

    case DevKind::MouseST:
    case DevKind::MouseAmiga:
    case DevKind::MouseAmigaAD:
        b.head("Mouse");
        b.line("Move / click", "your mouse or trackpad (Esc or F10 releases it)");
        b.ctl(C_PT_L, "Move left");
        b.ctl(C_PT_R, "Move right");
        b.ctl(C_PT_U, "Move up");
        b.ctl(C_PT_D, "Move down");
        b.ctl(C_MB_L, "Left button");
        b.ctl(C_MB_R, "Right button");
        break;

    case DevKind::Analog:
        b.head("Analog stick");
        b.ctl(C_AX_LX_N, "Left");
        b.ctl(C_AX_LX_P, "Right");
        b.ctl(C_AX_LY_N, "Forward (up)");
        b.ctl(C_AX_LY_P, "Back (down)");
        b.head("Buttons");
        b.ctl(C_A, "A");
        b.ctl(C_B, "B");
        b.ctl(C_C, "C");
        b.ctl(C_D, "D");
        b.ctl(C_UP, "Hat up");
        b.ctl(C_DOWN, "Hat down");
        b.ctl(C_LEFT, "Hat left");
        b.ctl(C_RIGHT, "Hat right");
        break;

    case DevKind::Driving:
        b.head("Driving controller");
        b.ctl(C_AX_LX_N, "Steer left");
        b.ctl(C_AX_LX_P, "Steer right");
        b.ctl(C_AX_R2, "Accelerator");
        b.ctl(C_AX_L2, "Brake");
        b.ctl(C_UP, "Gear up");
        b.ctl(C_DOWN, "Gear down");
        b.head("Buttons");
        b.ctl(C_A, "A");
        b.ctl(C_B, "B");
        b.ctl(C_C, "C");
        b.ctl(C_D, "D");
        break;

    case DevKind::Paddle:
        b.head("Paddle / analog stick (BattleSphere)");
        b.ctl(C_AX_LX_N, "Left");
        b.ctl(C_AX_LX_P, "Right");
        b.ctl(C_AX_LY_N, "Up");
        b.ctl(C_AX_LY_P, "Down");
        b.head("Pad (stays live)");
        b.dpad();
        b.buttons();
        b.head("Keypad");
        b.keypad();
        break;

    case DevKind::SixD:
        b.head("Translate");
        b.ctl(C_AX_LX_N, "Left");
        b.ctl(C_AX_LX_P, "Right");
        b.ctl(C_AX_LY_N, "Up");
        b.ctl(C_AX_LY_P, "Down");
        b.ctl(C_AX_R2, "Fore (thrust)");
        b.ctl(C_AX_L2, "Aft");
        b.head("Rotate");
        b.ctl(C_AX_RX_N, "Yaw left");
        b.ctl(C_AX_RX_P, "Yaw right");
        b.ctl(C_AX_RY_N, "Pitch up");
        b.ctl(C_AX_RY_P, "Pitch down");
        b.ctl(C_AX_R1, "Roll right");
        b.ctl(C_AX_L1, "Roll left");
        b.head("Buttons");
        b.ctl(C_A, "A");
        b.ctl(C_B, "B");
        b.ctl(C_C, "C");
        b.ctl(C_D, "D");
        b.ctl(C_E, "E");
        b.ctl(C_F, "F");
        b.ctl(C_OPTION, "G");
        b.ctl(C_PAUSE, "Rezero");
        break;

    case DevKind::LightGun:
        b.head("Light gun");
        b.line("Aim / shoot", "the mouse over the picture; left click fires");
        b.ctl(C_PT_L, "Aim left");
        b.ctl(C_PT_R, "Aim right");
        b.ctl(C_PT_U, "Aim up");
        b.ctl(C_PT_D, "Aim down");
        b.ctl(C_B, "Trigger (pad B)");
        b.ctl(C_GUN_RELOAD, "Reload (shoot off-screen; also right click)");
        b.ctl(C_A, "Aux A (pad A)");
        b.ctl(C_C, "Aux B (pad C)");
        b.ctl(C_OPTION, "Start (Option)");
        b.ctl(C_PAUSE, "Select (Pause)");
        break;
    }
    b.app();
    return sh;
}

AutoShow autoShowDecision(const AutoShowInput& in) {
    if (!in.contentLoaded) return AutoShow::No;
    if (!in.enabled) return (!in.padOnPlayer1 && !in.hintShown) ? AutoShow::Hint : AutoShow::No;
    if (!in.padOnPlayer1) return AutoShow::Keyboard;
    return in.padSeenBefore ? AutoShow::No : AutoShow::Gamepad;
}

void overlayAutoShow(ControlsOverlayState& s, double now) {
    s.show = true;
    s.autoShown = true;
    s.hideAt = now + kAutoShowSeconds;
}

bool overlayEvent(ControlsOverlayState& s, OverlayEvent e) {
    if (e == OverlayEvent::Toggle) {
        s.show = !s.show;
        s.autoShown = false;   // the player's own choice: it stays until they hide it
        s.hideAt = 0;
        return false;
    }
    if (!s.show) return false;
    s.show = false;
    s.autoShown = false;
    s.hideAt = 0;
    return true;
}

bool overlayTick(ControlsOverlayState& s, double now) {
    if (!s.show || !s.autoShown || s.hideAt <= 0 || now < s.hideAt) return false;
    s.show = false;
    s.autoShown = false;
    s.hideAt = 0;
    return true;
}

}  // namespace vj
