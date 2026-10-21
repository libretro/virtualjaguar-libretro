// TV mode (#991): the Overlays screen of the in-game menu -- one row per window or drawing the player can switch on over the
// game: Show FPS, the keypad overlay, the controls overlay, and every debug tool window the Tools menu lists.  A row is its label
// on the left and "On" / "Off" on the right; one that cannot be switched now (a debug window under hardcore or the emulation
// thread) is drawn muted with the reason under its label.  Accept toggles in place and the menu stays, except on the keypad and
// the controls overlay: those are drawn over the game and answered by mouse or touch, so Accept closes the menu onto them.
//
// Not a section: it exists only in the menu's slot (Shell::menuPushScreen), so the area it is drawn in is the menu panel's list
// area and its way out is "Menu".  The rows come from Host::overlayRows() on every draw and every intent; where the focus was
// lives in Shell::memory() under "overlays" (index and the row's id), so a list that changed is followed by item, not by place.
#include <algorithm>
#include <cfloat>
#include <cstdlib>

#include "theme.h"
#include "tv/tv_screen.h"
#include "tv/tv_shell.h"

namespace vj::tv {

namespace {

constexpr float kRowH = 72, kNoteRowH = 112, kRowGap = 14;   // a row whose reason wraps to two lines is the tall one
constexpr float kRowsMargin = 12;                            // room above the first row for a focused row's ring and shadow
constexpr const char* kKey = "overlays";

class OverlaysScreen : public Screen {
public:
    OverlaysScreen(Host& h, Shell& s) : host_(h), shell_(s) {}

    const char* id() const override { return "overlays"; }

    void intent(Intent in) override {
        refresh();
        switch (in) {
        case Intent::Up: move(-1); break;
        case Intent::Down: move(1); break;
        case Intent::Accept: accept(); break;
        default: break;
        }
    }

    void draw(const DrawCtx& c) override {
        refresh();
        if (rows_.empty()) return;
        const Metrics& m = c.m;
        const float u = m.u;
        // The menu's own inset: room for the focused row to grow about its centre.
        const float inset = 0.5f * (m.focusScale - 1.0f) * c.size.x + 2 * m.ring;
        const float lx = c.pos.x + inset, lw = std::max(1.0f, c.size.x - 2 * inset);

        const float rowsTop = c.pos.y + kRowsMargin * u, viewBottom = c.pos.y + c.size.y, viewH = viewBottom - rowsTop;
        std::vector<float> tops(rows_.size()), heights(rows_.size());
        float total = 0;
        for (size_t i = 0; i < rows_.size(); i++) {
            heights[i] = (noteWraps(m, rows_[i], lw) ? kNoteRowH : kRowH) * u;
            tops[i] = total;
            total += heights[i] + kRowGap * u;
        }
        total -= kRowGap * u;
        float target = scrollTo_;
        const size_t f = (size_t)index_;
        if (tops[f] < target) target = tops[f];
        else if (tops[f] + heights[f] > target + viewH) target = tops[f] + heights[f] - viewH;
        target = std::clamp(target, 0.0f, std::max(0.0f, total - viewH));
        scrollTo_ = target;
        if (!scrollInit_ || lastU_ != u) {
            scroll_.from = scroll_.to = target;
            scroll_.start = c.now;
            scrollInit_ = true;
            lastU_ = u;
        } else if (target != scroll_.to) {
            scroll_.retarget(target, c.now);
        }
        const float scroll = scroll_.at(c.now);
        tweens_.update((int)rows_.size(), index_, 0, c.now);

        if (!mouseSeen_) {   // a pointer already resting on a row when the screen appears has not chosen it
            lastMouse_ = c.mouse;
            mouseSeen_ = true;
        }
        const bool moved = c.mouse.x != lastMouse_.x || c.mouse.y != lastMouse_.y;
        lastMouse_ = c.mouse;

        int hover = -1;
        const bool moreBelow = scroll + viewH < total - 0.5f;
        // The arrow has the room under the rows: a half-cut last row must not run into it.
        c.dl->PushClipRect(ImVec2(c.pos.x, rowsTop - kRowsMargin * u), ImVec2(c.pos.x + c.size.x, viewBottom - (moreBelow ? 24 * u : 0.0f)), true);
        for (size_t i = 0; i < rows_.size(); i++) {
            const float y = rowsTop + tops[i] - scroll;
            if (y + heights[i] < rowsTop - kRowsMargin * u || y > viewBottom) continue;
            const float fo = (int)i == index_ ? tweens_.at((int)i, c.now) : 0.0f;
            widgets::RowArgs a;   // the card and the focus ring; the label, the value and the reason are drawn here
            a.focus = fo;
            const widgets::Hit hit = widgets::row(c.dl, m, ImVec2(lx, y), ImVec2(lw, heights[i]), a);
            drawRow(c, rows_[i], ImVec2(lx, y), ImVec2(lw, heights[i]), fo);
            if (hit.hovered && c.mouse.y >= rowsTop && c.mouse.y < viewBottom) hover = (int)i;   // not the part the clip hides
        }
        c.dl->PopClipRect();
        if (scroll > 0.5f) widgets::moreArrow(c.dl, m, ImVec2(lx + lw * 0.5f, rowsTop - 4 * u), true);
        if (moreBelow) widgets::moreArrow(c.dl, m, ImVec2(lx + lw * 0.5f, viewBottom - 10 * u), false);

        // ---- the pointer: hovering a row focuses it, a click toggles it, a notch is a row --------------------------------
        if (hover >= 0 && (moved || c.clicked)) focusRow(hover);
        const bool overList = c.hovered && c.mouse.x >= lx && c.mouse.x < lx + lw && c.mouse.y >= rowsTop && c.mouse.y < viewBottom;
        if (overList) wheel_ += c.wheel;
        const int steps = (int)wheel_;
        wheel_ -= (float)steps;
        for (int s = 0; s < std::abs(steps); s++) move(steps > 0 ? -1 : 1);   // wheel up = toward the first row
        if (hover >= 0 && c.clicked) accept();   // may close the menu, which retires this screen: nothing after it
    }

    std::vector<widgets::Hint> hints() const override {
        std::vector<widgets::Hint> h;
        if (index_ >= 0 && index_ < (int)rows_.size() && rows_[(size_t)index_].enabled) h.push_back({host_.glyph(Intent::Accept), "Toggle"});
        h.push_back({host_.glyph(Intent::Back), "Menu"});
        return h;
    }

private:
    // Fetches the rows and settles the focus on the row the memory names (else its remembered place).
    void refresh() {
        rows_ = host_.overlayRows();
        index_ = -1;
        if (rows_.empty()) return;
        const int n = (int)rows_.size();
        int idx = shell_.memory().get(kKey);
        const std::string item = shell_.memory().getItem(kKey);
        if (!item.empty())
            for (int i = 0; i < n; i++)
                if (rows_[(size_t)i].id == item) idx = i;
        focusRow(std::clamp(idx, 0, n - 1));
    }

    void focusRow(int i) {
        index_ = i;
        shell_.memory().put(kKey, i);
        shell_.memory().putItem(kKey, rows_[(size_t)i].id);
    }

    void move(int delta) {
        if (index_ < 0) return;
        const int j = index_ + delta;
        if (j >= 0 && j < (int)rows_.size()) focusRow(j);
    }

    // Toggles the focused row.  The keypad and the controls overlay are answered by mouse or touch over the game, so the menu
    // closes onto them (the Controls row does the same).
    void accept() {
        if (index_ < 0 || index_ >= (int)rows_.size()) return;
        const ToggleRow r = rows_[(size_t)index_];   // a copy: closing the menu retires this screen
        if (!r.enabled) return;
        host_.toggleOverlay(r.id);
        if (r.id == "keypad" || r.id == "controls") shell_.closeMenu();
    }

    // The room the reason has: the row's width less its padding.
    static float noteWidth(float rowW, const Metrics& m) { return std::max(1.0f, rowW - 2 * 24 * m.u); }
    static bool noteWraps(const Metrics& m, const ToggleRow& r, float rowW) {
        if (r.note.empty()) return false;
        return widgets::textSize(m.minText, r.note, noteWidth(rowW, m)).y > m.minText * 1.5f;
    }

    // The label and its "On" / "Off", and the reason (wrapped) under the label of a row that cannot be switched.
    void drawRow(const DrawCtx& c, const ToggleRow& r, ImVec2 pos, ImVec2 size, float f) const {
        const Metrics& m = c.m;
        const theme::Palette& pal = theme::pal();
        const float u = m.u, k = 1.0f + (m.focusScale - 1.0f) * f;   // the row grows about its centre (widgets::row)
        const ImVec2 mid(pos.x + size.x * 0.5f, pos.y + size.y * 0.5f);
        const ImVec2 p0(mid.x - size.x * k * 0.5f, mid.y - size.y * k * 0.5f), p1(mid.x + size.x * k * 0.5f, mid.y + size.y * k * 0.5f);
        const float pad = 24 * u * (0.6f + 0.4f * k);
        const float px = m.body * (1.0f + 0.08f * f);
        const ImU32 tc = widgets::col(r.enabled ? pal.text : pal.textMuted);
        const std::string value = r.on ? "On" : "Off";
        const float vw = widgets::textWidth(px, value);
        float noteH = 0;
        if (!r.note.empty()) noteH = widgets::textSize(m.minText, r.note, p1.x - p0.x - 2 * pad).y + 4 * u;
        const float y = p0.y + (p1.y - p0.y - (px + noteH)) * 0.5f;
        widgets::text(c.dl, px, ImVec2(p0.x + pad, y), tc, r.label, std::max(1.0f, p1.x - p0.x - 3 * pad - vw));
        widgets::text(c.dl, px, ImVec2(p1.x - pad - vw, y), widgets::col(r.enabled && r.on ? pal.text : pal.textMuted), value);
        if (!r.note.empty())
            widgets::text(c.dl, m.minText, ImVec2(p0.x + pad, y + px + 4 * u), widgets::col(pal.textMuted), r.note, 0, p1.x - p0.x - 2 * pad);
    }

    Host& host_;
    Shell& shell_;
    std::vector<ToggleRow> rows_;   // from the last refresh()
    int index_ = -1;                // the focused row; -1 when there are no rows
    Tween scroll_;                  // vertical scroll of the rows, in pixels
    float scrollTo_ = 0;            // ... and where it is heading
    bool scrollInit_ = false;
    float lastU_ = 0;
    widgets::FocusTweens tweens_;
    ImVec2 lastMouse_{-FLT_MAX, -FLT_MAX};
    bool mouseSeen_ = false;
    float wheel_ = 0;               // wheel notches not yet spent (a trackpad sends fractions)
};

}  // namespace

std::unique_ptr<Screen> makeOverlaysScreen(Host& h, Shell& s) { return std::make_unique<OverlaysScreen>(h, s); }

}  // namespace vj::tv
