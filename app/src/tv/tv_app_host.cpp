#include "tv/tv_app_host.h"

#include <SDL3/SDL.h>

#include "app.h"

namespace vj::tv {

double AppHost::now() const { return app_.now(); }
bool AppHost::gameLoaded() const { return app_.host.isLoaded(); }
std::string AppHost::gameTitle() const { return app_.host.contentName(); }
bool AppHost::menuPause(std::string& why) { return app_.menuPause(why); }
void AppHost::menuResume() { app_.menuResume(); }
bool AppHost::netplayActive() const { return app_.netplay.configured(); }
bool AppHost::canLoadState() const { return app_.ach.allowStateLoad(); }

std::vector<SlotInfo> AppHost::slots() {
    std::vector<SlotInfo> v;
    for (int i = 0; i < 10; i++) {
        SlotInfo s{i, app_.slotExists(i), 0, 0, 0, 0};
        if (s.exists) {
            s.time = (int64_t)app_.slotTime(i);
            if (const SlotThumb* t = app_.slotThumb(i)) {   // null headless (no GL) or without a screenshot
                s.tex = t->tex;
                s.w = t->w;
                s.h = t->h;
            }
        }
        v.push_back(s);
    }
    return v;
}

bool AppHost::saveState(int slot) {
    if (!app_.saveState(slot)) return false;
    app_.stateSlot = slot;   // F5 / F7 then act on the slot just used
    return true;
}

bool AppHost::loadState(int slot) {
    if (!app_.loadState(slot)) return false;
    app_.stateSlot = slot;
    return true;
}

void AppHost::reset() { app_.reset(); }
void AppHost::closeGame() { app_.closeContent(); }
void AppHost::toggleControlsOverlay() { overlayEvent(app_.input.controls, OverlayEvent::Toggle); }
void AppHost::armReleaseGuard() { app_.input.armReleaseGuard(); }
void AppHost::clearHolds() { app_.input.clearHolds(); }
void AppHost::toast(const std::string& text) { app_.toast(text, 2.5, 2); }

// The button caption for a hint: the face buttons as the last-used pad labels them (A / Cross / ...), the keyboard's
// keys while the last input was the keyboard or no pad is connected.  "Last-used" = the controls overlay's
// lastInputWasPad flag and the first connected pad.
std::string AppHost::glyph(Intent i) const {
    SDL_GamepadButton b = SDL_GAMEPAD_BUTTON_INVALID;
    const char* key = "";
    switch (i) {
    case Intent::Up: b = SDL_GAMEPAD_BUTTON_DPAD_UP; key = "Up"; break;
    case Intent::Down: b = SDL_GAMEPAD_BUTTON_DPAD_DOWN; key = "Down"; break;
    case Intent::Left: b = SDL_GAMEPAD_BUTTON_DPAD_LEFT; key = "Left"; break;
    case Intent::Right: b = SDL_GAMEPAD_BUTTON_DPAD_RIGHT; key = "Right"; break;
    case Intent::Accept: b = SDL_GAMEPAD_BUTTON_SOUTH; key = "Enter"; break;
    case Intent::Back: b = SDL_GAMEPAD_BUTTON_EAST; key = "Esc"; break;
    case Intent::Alt: b = SDL_GAMEPAD_BUTTON_WEST; break;
    case Intent::Aux: b = SDL_GAMEPAD_BUTTON_NORTH; break;
    case Intent::Menu: b = SDL_GAMEPAD_BUTTON_START; key = "Menu"; break;
    case Intent::SectionPrev: b = SDL_GAMEPAD_BUTTON_LEFT_SHOULDER; key = "Shift+Tab"; break;
    case Intent::SectionNext: b = SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER; key = "Tab"; break;
    case Intent::PagePrev: key = "Page Up"; break;
    case Intent::PageNext: key = "Page Down"; break;
    case Intent::None: break;
    }
    std::vector<SDL_Gamepad*> pads = app_.input.padHandles();
    if (!pads.empty() && app_.input.controls.lastInputWasPad && b != SDL_GAMEPAD_BUTTON_INVALID) {
        SDL_Gamepad* g = pads[0];
        const bool sony = SDL_GetGamepadButtonLabel(g, SDL_GAMEPAD_BUTTON_SOUTH) == SDL_GAMEPAD_BUTTON_LABEL_CROSS;
        switch (b) {
        case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: return sony ? "L1" : "LB";
        case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: return sony ? "R1" : "RB";
        case SDL_GAMEPAD_BUTTON_START: return "Start";
        case SDL_GAMEPAD_BUTTON_DPAD_UP:
        case SDL_GAMEPAD_BUTTON_DPAD_DOWN:
        case SDL_GAMEPAD_BUTTON_DPAD_LEFT:
        case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: return "D-pad";
        default: break;
        }
        switch (SDL_GetGamepadButtonLabel(g, b)) {
        case SDL_GAMEPAD_BUTTON_LABEL_A: return "A";
        case SDL_GAMEPAD_BUTTON_LABEL_B: return "B";
        case SDL_GAMEPAD_BUTTON_LABEL_X: return "X";
        case SDL_GAMEPAD_BUTTON_LABEL_Y: return "Y";
        case SDL_GAMEPAD_BUTTON_LABEL_CROSS: return "Cross";
        case SDL_GAMEPAD_BUTTON_LABEL_CIRCLE: return "Circle";
        case SDL_GAMEPAD_BUTTON_LABEL_SQUARE: return "Square";
        case SDL_GAMEPAD_BUTTON_LABEL_TRIANGLE: return "Triangle";
        default: break;
        }
    }
    return key;
}

}  // namespace vj::tv
