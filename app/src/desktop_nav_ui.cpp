#include "desktop_nav_ui.h"

#include <cmath>

#include "imgui_internal.h"

#include "app.h"
#include "desktop_nav.h"
#include "ui_util.h"

namespace vj {

bool deskPadShown(App& app) { return app.deskPad.active && app.deskPad.nav.visible(); }

namespace {

float dpi() { return ImGui::GetStyle().FontScaleDpi; }   // the display scale the UI was set up at (app.cpp initImGui)

// A face button as SDL labels it on this very pad (an 8BitDo in Switch mode says A/B/X/Y, a DualSense Cross/Circle/...),
// else the family's name for it (pad_profile.h).  As tv::AppHost::glyph does for TV hints.
std::string buttonName(SDL_Gamepad* g, const NavProfile& np, tv::Intent in) {
    const Binding bd = np.binding(in);
    if (bd.kind == Binding::None) return std::string();
    if (g && bd.kind == Binding::PadButton) {
        switch (SDL_GetGamepadButtonLabel(g, (SDL_GamepadButton)bd.code)) {
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
    return navButtonName(np.family, bd);
}

}  // namespace

void deskDrawFocusShadow(ImDrawList* dl, ImVec2 a0, ImVec2 a1, float round) {
    const float u = dpi();
    for (int i = 3; i >= 1; i--) {   // three soft layers, as the TV tiles have; the gap between tiles holds the widest
        const float e = i * 2.0f * u;
        dl->AddRectFilled(ImVec2(a0.x - e, a0.y - e + 3.0f * u), ImVec2(a1.x + e, a1.y + e + 3.0f * u),
                          IM_COL32(0, 0, 0, 40), round + e);
    }
}

void deskDrawFocusRing(ImDrawList* dl, ImVec2 a0, ImVec2 a1, float round) {
    const float t = 2.5f * dpi(), h = t * 0.5f;   // stroked inside the box: a tile at the edge of its scroll area keeps the whole ring
    dl->AddRect(ImVec2(a0.x + h, a0.y + h), ImVec2(a1.x - h, a1.y - h), ui::u32(ui::pal().text), round, 0, t);
}

void deskEnsureVisibleY(float itemMin, float itemMax) {
    const float top = ImGui::GetWindowPos().y;
    const float d = deskScrollToShow(itemMin, itemMax, top, top + ImGui::GetWindowHeight() - ImGui::GetStyle().ScrollbarSize,
                                     ImGui::GetStyle().ItemSpacing.y);
    if (d != 0.0f) ImGui::SetScrollY(ImGui::GetScrollY() + d);
}

void deskEnsureVisibleX(float itemMin, float itemMax) {
    const float left = ImGui::GetWindowPos().x;
    const float d = deskScrollToShow(itemMin, itemMax, left, left + ImGui::GetWindowWidth(), ImGui::GetStyle().ItemSpacing.x);
    if (d != 0.0f) ImGui::SetScrollX(ImGui::GetScrollX() + d);
}

void deskLeaveTextInput() { ImGui::ClearActiveID(); }

void deskStickScroll(App& app) {
    const DesktopPad& d = app.deskPad;
    const float a = std::fabs(d.scrollY);
    if (!d.active || a < 0.25f) return;
    const float v = (d.scrollY < 0 ? -1.0f : 1.0f) * (a - 0.25f) / 0.75f;   // 0 at the edge of the deadzone, 1 at full
    ImGui::SetScrollY(ImGui::GetScrollY() + v * ImGui::GetFontSize() * 40.0f * ImGui::GetIO().DeltaTime);
}

std::string deskHintLine(App& app) {
    const uint32_t id = app.input.lastPadId();
    const NavProfile& np = app.input.navProfile(id);
    SDL_Gamepad* g = SDL_GetGamepadFromID(id);   // null once that pad is gone
    const std::string ok = buttonName(g, np, tv::Intent::Accept), fav = buttonName(g, np, tv::Intent::Aux);
    const std::string prev = buttonName(g, np, tv::Intent::SectionPrev), next = buttonName(g, np, tv::Intent::SectionNext);
    std::string s;
    auto add = [&](const std::string& part) {
        if (!s.empty()) s += "  |  ";
        s += part;
    };
    if (!ok.empty()) {
        add(ok + " Play");
        add("hold " + ok + " Menu");
    }
    if (!fav.empty()) add(fav + " Favorite");
    if (!prev.empty() && !next.empty()) add(prev + "/" + next + " Tabs");
    return s;
}

}  // namespace vj
