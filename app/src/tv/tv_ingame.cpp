// TV mode (#991): the in-game menu drawn over the dimmed game -- Shell's row model (tv_shell.h) as a rail and a list.
// The rail names the game and the page; the list is the rows (a slot page shows each slot's picture); a hint bar at the
// bottom lists only what the focused row can do.  Hover and click are rectangle tests (no ImGui item), so this window
// never holds ImGui's active item and never makes io.WantCaptureKeyboard true.
#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>

#include "theme.h"
#include "tv/tv_shell.h"
#include "tv/tv_widgets.h"

namespace vj::tv {

namespace {

std::string stamp(int64_t t) {
    SDL_DateTime dt;
    if (t <= 0 || !SDL_TimeToDateTime((SDL_Time)t, &dt, true)) return "Saved";
    char b[48];
    std::snprintf(b, sizeof b, "%04d-%02d-%02d  %02d:%02d", dt.year, dt.month, dt.day, dt.hour, dt.minute);
    return b;
}

}  // namespace

void Shell::drawMenu(ImVec2 pos, ImVec2 size) {
    const Metrics m = metrics(size.y);
    const theme::Palette& pal = theme::pal();
    const double now = host_.now();

    const ImGuiWindowFlags wf = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoSavedSettings |
                                ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoNavInputs;   // no keyboard / gamepad navigation of its own
    ImGui::SetNextWindowPos(pos);
    ImGui::SetNextWindowSize(size);
    if (focusWindow_) {
        ImGui::SetNextWindowFocus();
        focusWindow_ = false;
    }
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    const bool shown = ImGui::Begin(kMenuWindowName, nullptr, wf);
    ImGui::PopStyleVar(2);
    if (!shown) {
        ImGui::End();
        return;
    }
    // A click in a window's empty space (every row is one, to ImGui) activates its move ID, and an active ID makes
    // io.WantCaptureKeyboard true -- which gates F4 and the hold edges.  The menu never wants the keyboard.
    ImGui::SetNextFrameWantCaptureKeyboard(false);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImGuiIO& io = ImGui::GetIO();
    const bool pointerMoved = io.MouseDelta.x != 0 || io.MouseDelta.y != 0;
    const bool slotPage = page_ == Page::SaveSlots || page_ == Page::LoadSlots;

    widgets::scrim(dl, pos, size, 0.62f);

    // ---- the surface: one panel inside the safe area, at most 1500u wide -------------------------------------------
    const float panelW = std::min(size.x - 2 * m.safeX, 1500 * m.u);
    const ImVec2 pp(pos.x + (size.x - panelW) * 0.5f, pos.y + m.safeY);
    const ImVec2 ps(panelW, size.y - 2 * m.safeY);
    widgets::panel(dl, m, pp, ps);
    const float inset = 48 * m.u;
    const float railW = 470 * m.u;
    const float hintY = pp.y + ps.y - inset - (m.caption + 8 * m.u);

    // ---- the rail: the page, and the game -------------------------------------------------------------------------
    const bool root = page_ == Page::Root;
    const float railX = pp.x + inset;
    float y = pp.y + inset;
    const ImVec2 t1 = widgets::text(dl, m.titleSmall, ImVec2(railX, y), widgets::col(pal.text), title_, 0, railW);
    y += t1.y + 12 * m.u;
    widgets::text(dl, m.caption, ImVec2(railX, y), widgets::col(pal.textMuted),
                  root ? std::string(host_.netplayActive() ? "Menu" : "Paused") : host_.gameTitle(), railW);

    // ---- the list -------------------------------------------------------------------------------------------------
    const float listX = railX + railW + inset;
    const float listW = pp.x + ps.x - inset - listX;
    float listY = pp.y + inset;
    if (host_.netplayActive()) listY += widgets::banner(dl, m, ImVec2(listX, listY), listW, "Netplay: the game keeps running") + 20 * m.u;
    const float rowH = (slotPage ? 124 : 88) * m.u, gap = 14 * m.u;
    const float listH = hintY - 24 * m.u - listY;
    const int visible = std::max(1, (int)((listH + gap) / (rowH + gap)));
    // The mouse wheel walks the list (a mouse-only player reaches the last slots): one focus step per notch.
    wheelAcc_ += io.MouseWheel;
    const int steps = (int)wheelAcc_;
    wheelAcc_ -= (float)steps;
    for (int s = 0; s < std::abs(steps); s++) focus_.move(steps > 0 ? -1 : 1);   // wheel up = toward the first row
    focus_.scrollToShow(visible);
    tweens_.update(focus_.count, focus_.index, (int)page_, now);

    std::vector<SlotInfo> slots;
    if (slotPage) slots = host_.slots();   // thumbnails are good for this frame only
    const float rowInset = 0.5f * (m.focusScale - 1.0f) * listW + 2 * m.ring;   // room for the focused row to grow
    int hovered = -1;
    for (int i = focus_.first; i < focus_.count && i < focus_.first + visible; i++) {
        const Row& r = rows_[(size_t)i];
        widgets::RowArgs a;
        a.label = r.label;
        a.enabled = r.enabled;
        a.focus = tweens_.at(i, now);
        if (r.kind == Row::Slot) {
            a.thumbBox = true;
            for (const SlotInfo& s : slots)
                if (s.slot == r.slot) {
                    a.sub = s.exists ? stamp(s.time) : "Empty";
                    a.thumb = s.exists ? s.tex : 0;
                    a.thumbSrcW = s.w;
                    a.thumbSrcH = s.h;
                }
        }
        const ImVec2 rp(listX + rowInset, listY + (i - focus_.first) * (rowH + gap));
        if (widgets::row(dl, m, rp, ImVec2(listW - 2 * rowInset, rowH), a).hovered) hovered = i;
    }
    if (focus_.first > 0) widgets::moreArrow(dl, m, ImVec2(listX + listW * 0.5f, listY - 10 * m.u), true);
    if (focus_.first + visible < focus_.count)
        widgets::moreArrow(dl, m, ImVec2(listX + listW * 0.5f, listY + visible * (rowH + gap) - 2 * m.u), false);

    // ---- the hint bar: only what works on the focused row ---------------------------------------------------------
    std::vector<widgets::Hint> hints;
    if (focus_.count > 1) hints.push_back({host_.glyph(Intent::Down), "Move"});
    const bool canAccept = focus_.index >= 0 && focus_.index < (int)rows_.size() && rows_[(size_t)focus_.index].enabled;
    if (canAccept) {
        const char* verb = "Select";
        if (page_ == Page::SaveSlots) verb = "Save";
        else if (page_ == Page::LoadSlots) verb = "Load";
        else if (page_ == Page::ConfirmReset || page_ == Page::ConfirmClose) verb = "Choose";
        hints.push_back({host_.glyph(Intent::Accept), verb});
    }
    hints.push_back({host_.glyph(Intent::Back), root ? "Resume" : "Back"});
    widgets::hintBar(dl, m, ImVec2(railX, hintY), ps.x - 2 * inset, hints);

    // ---- the pointer: hover moves the focus (only when it moved, so it does not fight a pad), a click picks ---------
    int clicked = -1;
    if (hovered >= 0 && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) clicked = hovered;
    else if (hovered >= 0 && pointerMoved) setMenuFocus(hovered);
    ImGui::End();
    if (clicked >= 0) {   // after End(): accepting rebuilds the rows this loop read
        setMenuFocus(clicked);
        intent(Intent::Accept);
    }
}

}  // namespace vj::tv
