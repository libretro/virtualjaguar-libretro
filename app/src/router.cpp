#include "router.h"

#include "libretro.h"

#include <algorithm>
#include <cmath>

namespace vj {

namespace {

constexpr uint64_t keypadMask() {
    uint64_t m = 0;
    for (int c = C_K0; c <= C_HASH; c++) m |= 1ull << c;
    return m;
}

// Whole counts out of a fractional running total (the remainder is kept for the next frame).
int takeCounts(float& frac, float add) {
    frac += add;
    int n = (int)frac;   // truncates toward zero
    frac -= (float)n;
    return n;
}

bool anyPointerControl(const SlotAcc& a) {
    for (int c = C_PT_L; c <= C_PT_D; c++)
        if (a.an.v[(size_t)c]) return true;
    return a.ex.test(C_MB_L) || a.ex.test(C_MB_R) || a.ex.test(C_GUN_RELOAD);
}

}  // namespace

void Router::reset() {
    for (auto& f : frac_) f[0] = f[1] = 0;
    for (auto& c : cursor_) c[0] = c[1] = 0.5f;
    gunOnStick_[0] = gunOnStick_[1] = false;
    lastX_[0] = lastX_[1] = lastY_[0] = lastY_[1] = -1;
}

void Router::route(const RouterConfig& cfg, const std::vector<HostSource>& pads, const HostSource* kb,
                             const PointerInput& ptr, KeypadOverlay* overlay, CoreInput& out) {
    std::array<SlotAcc, kSlots> acc;
    const int dz = cfg.settings.deadzone();

    auto gather = [&](const HostSource& src, unsigned slot) {
        if (!src.profile) return;
        ControlSet cs = evaluate(*src.profile, src.snap);
        ControlSet ex = evaluateExtra(*src.profile, src.snap);
        AnalogSet an = evaluateAnalog(*src.profile, src.snap, dz);
        if (overlay) {
            // Open: the keypad overlay owns the controls (and the sticks): nothing reaches the game.
            cs = overlay->filter(src.overlaySlot, cs);
            if (overlay->open) {
                ex = ControlSet();
                an = AnalogSet();
            }
        } else {
            cs.clear(C_OVERLAY);
        }
        ex.clear(C_HELP);   // an app action, handled where the key event arrives (InputManager::handleEvent)
        ex.clear(C_TVMENU);   // likewise (plain presses there, holds via the HoldArbiter)
        SlotAcc& a = acc[slot];
        a.cs.bits |= cs.bits;
        a.ex.bits |= ex.bits;
        for (int i = 0; i < C_COUNT; i++) a.an.v[(size_t)i] = std::max(a.an.v[(size_t)i], an.v[(size_t)i]);
    };

    for (unsigned s = 0; s < kSlots; s++) {
        int p = cfg.routes.pad[s];
        if (p >= 0 && (size_t)p < pads.size()) gather(pads[(size_t)p], s);
        if (cfg.routes.kb[s] && kb) gather(*kb, s);
    }

    // ---- every slot ---------------------------------------------------------------------------------
    for (unsigned s = 0; s < kSlots; s++) {
        SlotAcc& a = acc[s];
        const unsigned jp = slotJaguarPort(s);

        if (slotSocket(s) > 0) {
            // libretro.c update_teamtap_input(): sockets 1-3 read the RetroPad of frontend ports 2-4 /
            // 5-7 through teamtap_map[], nothing else.
            out.addTapControls(s, a.cs);
            continue;
        }
        const DevKind kind = cfg.kind[jp];
        SlotAcc& c = a;
        const bool pointerDevice = devIsPointerClass(kind);
        const bool ptActive = anyPointerControl(c);
        // While the stick / keys steer a pointer device, the keypad digits that share them stay quiet
        // (the default right stick is also keypad 7 / 8 / 9 / *).
        if (pointerDevice && ptActive) c.cs.bits &= ~keypadMask();

        // The pad half.  A mouse port keeps its pad until the mouse proves live (libretro.c
        // inputdev_live: "selecting a mouse must never leave the port with no working input"),
        // a rotary keeps A/B/C/Option/Pause and the keypad (only U/D/L/R are withheld), a gun
        // and the paddle keep the whole pad.
        out.addControls(jp, c.cs);

        switch (kind) {
        case DevKind::Pad:
        case DevKind::TeamTap:
            break;

        case DevKind::MouseST:
        case DevKind::MouseAmiga:
        case DevKind::MouseAmigaAD: {
            // libretro.c: dx = MOUSE_X; dy = MOUSE_Y; buttons from MOUSE_LEFT / MOUSE_RIGHT.
            float fx = 0, fy = 0;
            unsigned buttons = 0;
            if ((int)jp == cfg.mousePort) {
                fx += ptr.dx * cfg.settings.mouseSpeed;
                fy += ptr.dy * cfg.settings.mouseSpeed;
                buttons |= ptr.buttons & 3u;
            }
            float st = cfg.settings.stickSpeed * kStickPixelsPerFrame / 32767.0f;
            fx += (float)c.an.axis(C_PT_L, C_PT_R) * st;
            fy += (float)c.an.axis(C_PT_U, C_PT_D) * st;
            if (c.ex.test(C_MB_L)) buttons |= 1u;
            if (c.ex.test(C_MB_R)) buttons |= 2u;
            out.addMouse(jp, takeCounts(frac_[jp][0], fx), takeCounts(frac_[jp][1], fy), buttons);
            break;
        }

        case DevKind::Rotary: {
            // libretro.c: dx = MOUSE_X only (host +X = clockwise, inputdev.c InputDevFeed); the pad's
            // A/B/C/Option/Pause and keypad stay on the RetroPad.
            float fx = 0;
            if ((int)jp == cfg.mousePort) fx += ptr.dx * cfg.settings.rotarySpeed;
            fx += ((float)c.an.axis(C_PT_L, C_PT_R) / 32767.0f) * kRotaryCountsPerFrame * cfg.settings.rotarySpeed;
            out.addMouse(jp, takeCounts(frac_[jp][0], fx), 0, 0);
            break;
        }

        case DevKind::Analog:
        case DevKind::Driving:
        case DevKind::SixD:
        case DevKind::Paddle: {
            // libretro.c: ANALOG LEFT X / Y (the paddle, analog and driving read only these; driving
            // adds ANALOG_BUTTON R2 / L2; 6D reads both sticks and ANALOG_BUTTON L2 R2 L R).
            out.setAnalog(jp, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_X, c.an.axis(C_AX_LX_N, C_AX_LX_P));
            out.setAnalog(jp, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_Y, c.an.axis(C_AX_LY_N, C_AX_LY_P));
            if (kind == DevKind::Paddle) break;   // the pad stays a pad (no X / L3 / R3 aliases)
            if (kind == DevKind::SixD) {
                out.setAnalog(jp, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_X, c.an.axis(C_AX_RX_N, C_AX_RX_P));
                out.setAnalog(jp, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_Y, c.an.axis(C_AX_RY_N, C_AX_RY_P));
                out.setAnalogButton(jp, RETRO_DEVICE_ID_JOYPAD_L, c.an.get(C_AX_L1));
                out.setAnalogButton(jp, RETRO_DEVICE_ID_JOYPAD_R, c.an.get(C_AX_R1));
            }
            out.setAnalogButton(jp, RETRO_DEVICE_ID_JOYPAD_L2, c.an.get(C_AX_L2));
            out.setAnalogButton(jp, RETRO_DEVICE_ID_JOYPAD_R2, c.an.get(C_AX_R2));
            // D = RetroPad X (INPUTDEV_SW_D); 6D's E / F are the stick clicks L3 / R3.
            uint16_t raw = 0;
            if (c.ex.test(C_D)) raw |= (uint16_t)(1u << RETRO_DEVICE_ID_JOYPAD_X);
            if (kind == DevKind::SixD) {
                if (c.ex.test(C_E)) raw |= (uint16_t)(1u << RETRO_DEVICE_ID_JOYPAD_L3);
                if (c.ex.test(C_F)) raw |= (uint16_t)(1u << RETRO_DEVICE_ID_JOYPAD_R3);
            }
            out.addRawPad(jp, raw);
            break;
        }

        case DevKind::LightGun: {
            // libretro.c: SCREEN_X / SCREEN_Y, TRIGGER, AUX_A, AUX_B, START, SELECT, IS_OFFSCREEN.  START /
            // SELECT / AUX_B ride the pad's Option / Pause / C (the same matrix slots), so they come from the
            // pad half above; the trigger is the pointer's left button, AUX_A its middle button.
            const bool mouse = (int)jp == cfg.mousePort;
            const bool moved = mouse && ptr.havePos && (ptr.x != lastX_[jp] || ptr.y != lastY_[jp] || ptr.dx != 0 || ptr.dy != 0);
            if (mouse && ptr.havePos) {
                lastX_[jp] = ptr.x;
                lastY_[jp] = ptr.y;
            }
            const int stickX = c.an.axis(C_PT_L, C_PT_R), stickY = c.an.axis(C_PT_U, C_PT_D);
            const bool stick = stickX != 0 || stickY != 0;
            if (cfg.settings.gunSource == 1) gunOnStick_[jp] = false;
            else if (cfg.settings.gunSource == 2) gunOnStick_[jp] = true;
            else if (stick) gunOnStick_[jp] = true;
            else if (moved) gunOnStick_[jp] = false;
            else if (!mouse) gunOnStick_[jp] = true;

            float px = 0, py = 0;
            bool have = false;
            if (gunOnStick_[jp]) {
                float k = cfg.settings.stickSpeed * kAimFractionPerFrame / 32767.0f;
                cursor_[jp][0] = std::max(0.0f, std::min(0.99999f, cursor_[jp][0] + (float)stickX * k));
                cursor_[jp][1] = std::max(0.0f, std::min(0.99999f, cursor_[jp][1] + (float)stickY * k));
                px = cfg.pic.x + cursor_[jp][0] * cfg.pic.w;
                py = cfg.pic.y + cursor_[jp][1] * cfg.pic.h;
                have = true;
            } else if (mouse && ptr.havePos) {
                px = ptr.x;
                py = ptr.y;
                have = true;
            }
            const bool reload = c.ex.test(C_GUN_RELOAD) || (mouse && (ptr.buttons & 2u));
            int32_t gx = 0, gy = 0;
            bool inside = have && gunCoords(cfg.pic, px, py, gx, gy);
            unsigned b = 0;
            if (mouse && (ptr.buttons & 1u)) b |= CoreInput::GUN_TRIGGER;
            if (mouse && (ptr.buttons & 4u)) b |= CoreInput::GUN_AUX_A;
            if (reload) b |= CoreInput::GUN_TRIGGER;
            // Off-screen is "the photodiode sees no light": the aim keeps its last value, the trigger still reports.
            out.setGun(jp, gx, gy, reload || !inside, b);
            break;
        }
        }
    }
}

}  // namespace vj
