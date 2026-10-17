// TV mode (#991): the Base layer's window and the chrome shared by every screen -- the top bar (logo, section tabs,
// RetroAchievements status, clock) and the hint bar -- with the screen stack drawn between them.  Sizes are the design
// units of the spec (u = viewport height / 1080).  Like the in-game menu the window owns no ImGui item: hover and click
// are rectangle tests the screens make from DrawCtx, so it never holds ImGui's active item and never makes
// io.WantCaptureKeyboard true.
#include <algorithm>

#include "imgui_internal.h"
#include "theme.h"
#include "tv/tv_shell.h"
#include "tv/tv_widgets.h"

namespace vj::tv {

namespace {

constexpr float kBarH = 56, kContentTop = 150, kHintTop = 968;   // design units

const char* sectionLabel(Section s) {
    switch (s) {
    case Section::Home: return "Home";
    case Section::Library: return "Library";
    }
    return "";
}

// A key pill (LB / RB): an outlined rounded box round a short label.  Returns its width.
float keyPill(ImDrawList* dl, const Metrics& m, float x, float barY, const std::string& label) {
    const theme::Palette& p = theme::pal();
    const float px = 24 * m.u, padX = 10 * m.u, padY = 4 * m.u;
    const float w = widgets::textWidth(px, label) + 2 * padX, h = px + 2 * padY;
    const float y = barY + (kBarH * m.u - h) * 0.5f;
    dl->AddRect(ImVec2(x, y), ImVec2(x + w, y + h), widgets::col(p.textMuted), 8 * m.u, 0, 2 * m.u);
    widgets::text(dl, px, ImVec2(x + padX, y + padY), widgets::col(p.textMuted), label);
    return w;
}

}  // namespace

void Shell::drawBase(ImVec2 pos, ImVec2 size) {
    const Metrics m = metrics(size.y);
    const theme::Palette& pal = theme::pal();
    const double now = host_.now();
    const float u = m.u;

    const ImGuiWindowFlags wf = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoBringToFrontOnFocus |
                                ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove;
    ImGui::SetNextWindowPos(pos);
    ImGui::SetNextWindowSize(size);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    const bool shown = ImGui::Begin(kBaseWindowName, nullptr, wf);
    ImGui::PopStyleVar(2);
    if (!shown) {
        ImGui::End();
        return;
    }
    // A click in a window's empty space (all of this one) activates its move ID, and an active ID makes
    // io.WantCaptureKeyboard true -- which gates F4 and the hold edges.  The Base layer never wants the keyboard.
    ImGui::SetNextFrameWantCaptureKeyboard(false);
    // Under every desktop window: ImGui puts a window it creates in front, and this one can appear (TV mode turned on) with
    // Settings or the state manager already open.
    ImGui::BringWindowToDisplayBack(ImGui::GetCurrentWindow());
    if (padDriven_) ImGui::SetMouseCursor(ImGuiMouseCursor_None);   // a pad is driving: no pointer until the mouse moves
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImGuiIO& io = ImGui::GetIO();
    const bool hovered = ImGui::IsWindowHovered();
    const bool leftClick = hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
    int tabClicked = -1;   // a click on a section tab: applied once the screens are drawn

    // ---- the top bar ------------------------------------------------------------------------------------------------
    const float barY = pos.y + m.safeY;
    const float right = pos.x + size.x - m.safeX;
    float x = pos.x + m.safeX;
    dl->AddRectFilled(ImVec2(x, barY + (kBarH - 22) * 0.5f * u), ImVec2(x + 22 * u, barY + (kBarH + 22) * 0.5f * u),
                      widgets::col(pal.hoverEdge), 5 * u);
    x += (22 + 16) * u;
    const float logoPx = 34 * u;
    const ImVec2 logo = widgets::text(dl, logoPx, ImVec2(x, barY + (kBarH * u - logoPx) * 0.5f), widgets::col(pal.text), "VIRTUAL JAGUAR");
    x += logo.x + 56 * u;

    // The section tabs between the shoulder-button pills.
    std::string prev = host_.glyph(Intent::SectionPrev), next = host_.glyph(Intent::SectionNext);
    if (prev.empty()) prev = "LB";
    if (next.empty()) next = "RB";
    x += keyPill(dl, m, x, barY, prev) + 20 * u;
    const Section sections[] = {Section::Home, Section::Library};
    for (size_t i = 0; i < sizeof sections / sizeof sections[0]; i++) {
        const Section s = sections[i];
        const bool active = s == section_;
        const std::string label = sectionLabel(s);
        const float ty = barY + (kBarH * u - m.body) * 0.5f;
        const float tw = widgets::textWidth(m.body, label);
        widgets::text(dl, m.body, ImVec2(x, ty), widgets::col(active ? pal.text : pal.textMuted), label);
        if (leftClick && io.MousePos.x >= x && io.MousePos.x < x + tw && io.MousePos.y >= barY && io.MousePos.y < barY + kBarH * u)
            tabClicked = (int)i;
        if (active)
            dl->AddRectFilled(ImVec2(x, ty + m.body + 6 * u), ImVec2(x + tw, ty + m.body + 10 * u), widgets::col(pal.hoverEdge));
        x += tw + (i + 1 < sizeof sections / sizeof sections[0] ? 40 * u : 20 * u);
    }
    x += keyPill(dl, m, x, barY, next);

    // The right end: RetroAchievements status (only when signed in), then the clock.
    const float statusPx = 26 * u, statusY = barY + (kBarH * u - statusPx) * 0.5f;
    const std::string clock = host_.clockText(), ra = host_.raStatus();
    const float clockW = widgets::textWidth(statusPx, clock);
    widgets::text(dl, statusPx, ImVec2(right - clockW, statusY), widgets::col(pal.text), clock);
    if (!ra.empty()) {
        const float room = right - clockW - 24 * u - (x + 40 * u);   // between the tabs and the clock
        if (room > 0) {
            const float raW = std::min(widgets::textWidth(statusPx, ra), room);
            widgets::text(dl, statusPx, ImVec2(right - clockW - 24 * u - raW, statusY), widgets::col(pal.textMuted), ra, room);
        }
    }

    // ---- the screens: the topmost non-modal one, and every modal above it, in order ----------------------------------
    DrawCtx c;
    c.dl = dl;
    c.m = m;
    c.pos = ImVec2(pos.x, pos.y + kContentTop * u);
    c.size = ImVec2(size.x, (kHintTop - kContentTop) * u);
    c.hovered = hovered;
    c.mouse = io.MousePos;
    c.clicked = leftClick;
    c.rightClicked = c.hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right);
    c.wheel = c.hovered ? io.MouseWheel : 0.0f;
    c.now = now;
    if (!stack_.empty()) {
        size_t first = stack_.size() - 1;
        while (first > 0 && stack_[first]->modal()) first--;
        std::vector<Screen*> list;
        for (size_t i = first; i < stack_.size(); i++) list.push_back(stack_[i].get());
        const unsigned gen = stackGen_;
        for (Screen* s : list) {
            s->draw(c);
            if (stackGen_ != gen) break;   // a screen popped itself or pushed another: the rest of the list is stale
        }
    }

    if (tabClicked >= 0 && sections[tabClicked] != section_) setSection(sections[tabClicked]);

    // ---- the hint bar: the top screen's hints, with the section hint ahead of the way out ----------------------------
    std::vector<widgets::Hint> hints;
    if (Screen* t = top()) hints = t->hints();
    const widgets::Hint sections_hint{prev + " " + next, "Sections"};
    hints.insert(hints.empty() ? hints.end() : hints.end() - 1, sections_hint);
    widgets::hintBar(dl, m, ImVec2(pos.x + m.safeX, pos.y + kHintTop * u), size.x - 2 * m.safeX, hints);
    ImGui::End();
}

}  // namespace vj::tv
