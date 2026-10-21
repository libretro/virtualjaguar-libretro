// TV mode (#991): the Settings screen -- a rail of groups (Picture, Speed, Sound, Interface) beside the focused group's
// rows, each a label on the left and its value on the right between two chevrons.  Left / Right step a row, Accept steps it
// forward and wraps (so a one-handed player can still change everything), and the last row of Interface opens the desktop
// Settings window.  Geometry is the Library's rail and the plan's "Screen geometry", in design units (u = viewport
// height / 1080).
//
// The rows come from Host::settingRows() on every draw and every intent: a row the build or the session does not have
// is simply absent, and one that changed under the player is followed by its id.  Where focus was lives in
// Shell::memory(): the group under "settings/rail", each group's row under "settings/<group>" (index and row id), and
// whether the list had the focus under "settings/list", so coming back lands there.
#include <algorithm>
#include <cfloat>
#include <cstdlib>

#include "theme.h"
#include "tv/tv_screen.h"
#include "tv/tv_settings_model.h"
#include "tv/tv_shell.h"

namespace vj::tv {

namespace {

constexpr float kRailW = 344, kRailGap = 64, kItemH = 54, kItemGap = 8;   // the Library's rail, design units
constexpr float kListTop = 196 - 150;                                     // from the top of the content area (y = 150)
constexpr float kRowH = 72, kRowGap = 14;
constexpr float kRowsMargin = 12;                                         // room above the first row for a focused row's ring and shadow
constexpr const char* kMoreId = "more";

std::string memoryKey(int group) { return "settings/" + std::to_string(group); }

class SettingsScreen : public Screen {
public:
    SettingsScreen(Host& h, Shell& s) : host_(h), shell_(s) {}

    const char* id() const override { return "settings"; }

    void intent(Intent in) override {
        refresh();
        if (onRail_) railIntent(in);
        else listIntent(in);
        remember();
    }

    // Back from a list lands on the rail (still this screen); from the rail it is the shell's (Home).
    bool back() override {
        refresh();
        if (onRail_) return false;
        onRail_ = true;
        remember();
        return true;
    }

    void draw(const DrawCtx& c) override {
        refresh();
        const Metrics& m = c.m;
        const theme::Palette& pal = theme::pal();
        const float u = m.u;
        const float x0 = c.pos.x + m.safeX, right = c.pos.x + c.size.x - m.safeX;

        if (!mouseSeen_) {   // a pointer already resting on a row when the screen appears has not chosen it
            lastMouse_ = c.mouse;
            mouseSeen_ = true;
        }
        const bool moved = c.mouse.x != lastMouse_.x || c.mouse.y != lastMouse_.y;
        lastMouse_ = c.mouse;
        const bool pick = c.hovered && (moved || c.clicked);

        // ---- the rail --------------------------------------------------------------------------------------------------
        int hoverItem = -1;
        for (int i = 0; i < (int)groups_.size(); i++) {
            const ImVec2 p0(x0, c.pos.y + (float)i * (kItemH + kItemGap) * u), p1(x0 + kRailW * u, p0.y + kItemH * u);
            const bool sel = i == rail_.index;
            if (sel) c.dl->AddRectFilled(p0, p1, widgets::col(pal.cardBgHover), 10 * u);
            if (sel && onRail_) c.dl->AddRect(p0, p1, widgets::col(pal.text), 10 * u, 0, m.ring);
            widgets::text(c.dl, m.body, ImVec2(p0.x + 20 * u, p0.y + 12 * u), widgets::col(sel ? pal.text : pal.textMuted),
                          settingGroupLabel(groups_[(size_t)i]), (kRailW - 40) * u);
            if (c.hovered && c.mouse.x >= p0.x && c.mouse.x < p1.x && c.mouse.y >= p0.y && c.mouse.y < p1.y) hoverItem = i;
        }
        if (hoverItem >= 0 && pick) {   // hovering a group selects it: its rows show at once
            selectGroup(hoverItem);
            onRail_ = true;
        }
        if (rows_.empty()) {
            remember();
            return;
        }

        // ---- the rows: scrolled so the focused one shows ---------------------------------------------------------------
        const float lx = x0 + (kRailW + kRailGap) * u, lw = std::max(1.0f, right - lx);
        const std::string count = std::to_string(rows_.size()) + (rows_.size() == 1 ? " setting" : " settings");
        widgets::text(c.dl, m.caption, ImVec2(lx, c.pos.y), widgets::col(pal.textMuted),
                      std::string(settingGroupLabel(group_)) + " \xC2\xB7 " + count, lw);

        const float rowsTop = c.pos.y + kListTop * u, viewBottom = c.pos.y + c.size.y, viewH = viewBottom - rowsTop;
        const float pitch = (kRowH + kRowGap) * u, rowH = kRowH * u;
        const float total = (float)rows_.size() * pitch - kRowGap * u;
        const float focusTop = (float)list_.index * pitch;
        float target = scrollTo_;
        if (group_ != lastGroup_) target = 0;
        if (focusTop < target) target = focusTop;
        else if (focusTop + rowH > target + viewH) target = focusTop + rowH - viewH;
        target = std::clamp(target, 0.0f, std::max(0.0f, total - viewH));
        scrollTo_ = target;
        if (!scrollInit_ || lastU_ != u || group_ != lastGroup_) {
            scroll_.from = scroll_.to = target;
            scroll_.start = c.now;
            scrollInit_ = true;
            lastU_ = u;
            lastGroup_ = group_;
        } else if (target != scroll_.to) {
            scroll_.retarget(target, c.now);
        }
        const float scroll = scroll_.at(c.now);
        tweens_.update((int)rows_.size(), list_.index, group_, c.now);

        int hoverRow = -1;
        c.dl->PushClipRect(ImVec2(c.pos.x, rowsTop - kRowsMargin * u), ImVec2(c.pos.x + c.size.x, viewBottom), true);
        for (int i = 0; i < (int)rows_.size(); i++) {
            const float y = rowsTop + (float)i * pitch - scroll;
            if (y + rowH < rowsTop - kRowsMargin * u || y > viewBottom) continue;
            const float f = !onRail_ && i == list_.index ? tweens_.at(i, c.now) : 0.0f;
            widgets::RowArgs a;   // the card and the focus ring; the label and the value are drawn here
            a.focus = f;
            const widgets::Hit hit = widgets::row(c.dl, m, ImVec2(lx, y), ImVec2(lw, rowH), a);
            drawRow(c, rows_[(size_t)i], ImVec2(lx, y), ImVec2(lw, rowH), f);
            if (hit.hovered && c.mouse.y >= rowsTop && c.mouse.y < viewBottom) hoverRow = i;   // not the part the clip hides
        }
        c.dl->PopClipRect();
        if (scroll > 0.5f) widgets::moreArrow(c.dl, m, ImVec2(lx + lw * 0.5f, rowsTop - 4 * u), true);
        if (scroll + viewH < total - 0.5f) widgets::moreArrow(c.dl, m, ImVec2(lx + lw * 0.5f, viewBottom - 10 * u), false);

        // ---- the pointer: hovering a row focuses it, a click on the value's left / right half steps that way, a click
        // elsewhere on the row is Accept, a notch is a row --------------------------------------------------------------
        if (hoverRow >= 0 && pick) {
            list_.index = hoverRow;
            onRail_ = false;
        }
        const bool overList = c.hovered && c.mouse.x >= lx && c.mouse.x < right && c.mouse.y >= rowsTop && c.mouse.y < viewBottom;
        if (overList) wheel_ += c.wheel;
        const int steps = (int)wheel_;
        wheel_ -= (float)steps;
        for (int s = 0; s < std::abs(steps); s++) {   // wheel up = toward the first row
            list_.move(steps > 0 ? -1 : 1);
            onRail_ = false;
        }
        remember();
        if (hoverRow >= 0 && c.clicked) click((c.mouse.x - lx) / lw);
        else if (hoverItem >= 0 && c.clicked) intent(Intent::Accept);
    }

    std::vector<widgets::Hint> hints() const override {
        std::vector<widgets::Hint> h;
        if (onRail_ || rows_.empty()) {
            if (!rows_.empty()) h.push_back({host_.glyph(Intent::Accept), "Open"});
            h.push_back({host_.glyph(Intent::Back), "Home"});
            return h;
        }
        const SettingRow& r = rows_[(size_t)list_.index];
        if (r.id == kMoreId) {
            h.push_back({host_.glyph(Intent::Accept), "Open"});
        } else if (!r.readOnly && !r.choices.empty()) {
            const std::string l = host_.glyph(Intent::Left), rt = host_.glyph(Intent::Right);
            h.push_back({l.empty() ? rt : rt.empty() || l == rt ? l : l + " " + rt, "Change"});   // "D-pad" once, "Left Right" on a keyboard
            h.push_back({host_.glyph(Intent::Accept), "Next"});
        }
        h.push_back({host_.glyph(Intent::Back), "Groups"});
        return h;
    }

private:
    // Fetches the rows, keeps the groups that have any, and settles the focus: the rail follows the group (not its
    // position: Speed appears once the core's options are known) and the list follows the row's id.
    void refresh() {
        const std::vector<SettingRow> all = host_.settingRows();
        bool has[kSettingGroups] = {};
        for (const SettingRow& r : all)
            if (r.group >= 0 && r.group < kSettingGroups) has[r.group] = true;
        groups_.clear();
        for (int g = 0; g < kSettingGroups; g++)
            if (has[g]) groups_.push_back(g);
        if (!built_) {
            built_ = true;
            onRail_ = shell_.memory().get("settings/list") == 0;   // a fresh visit starts on the rail
        }
        rail_.count = (int)groups_.size();
        rail_.index = 0;
        const int wantGroup = shell_.memory().get("settings/rail", groups_.empty() ? 0 : groups_[0]);
        for (int i = 0; i < (int)groups_.size(); i++)
            if (groups_[(size_t)i] == wantGroup) rail_.index = i;
        group_ = groups_.empty() ? -1 : groups_[(size_t)rail_.index];
        rows_.clear();
        for (const SettingRow& r : all)
            if (r.group == group_) rows_.push_back(r);
        list_.count = (int)rows_.size();
        const std::string key = memoryKey(group_);
        list_.index = shell_.memory().get(key);
        const std::string id = shell_.memory().getItem(key);
        if (!id.empty())
            for (int i = 0; i < (int)rows_.size(); i++)
                if (rows_[(size_t)i].id == id) list_.index = i;
        list_.index = std::clamp(list_.index, 0, std::max(0, list_.count - 1));
    }

    void remember() {
        shell_.memory().put("settings/list", onRail_ ? 0 : 1);
        if (group_ < 0 || rows_.empty()) return;
        shell_.memory().put("settings/rail", group_);
        shell_.memory().put(memoryKey(group_), list_.index);
        shell_.memory().putItem(memoryKey(group_), rows_[(size_t)list_.index].id);
    }

    // Moving on the rail shows that group's rows at once, at the place they were left.
    void selectGroup(int k) {
        if (k < 0 || k >= (int)groups_.size() || k == rail_.index) return;
        remember();
        shell_.memory().put("settings/rail", groups_[(size_t)k]);
        refresh();
    }

    void railIntent(Intent in) {
        LinearFocus f = rail_;
        switch (in) {
        case Intent::Up:
            if (f.move(-1)) selectGroup(f.index);
            break;
        case Intent::Down:
            if (f.move(1)) selectGroup(f.index);
            break;
        case Intent::Right:
        case Intent::Accept:
            if (!rows_.empty()) onRail_ = false;   // to the list, at the remembered row
            break;
        default: break;
        }
    }

    void listIntent(Intent in) {
        if (rows_.empty()) return;
        switch (in) {
        case Intent::Up: list_.move(-1); break;
        case Intent::Down: list_.move(1); break;
        case Intent::Left:
            if (canStep(focused(), -1)) step(focused(), -1, false);
            else onRail_ = true;   // the left edge of the row: the groups
            break;
        case Intent::Right: step(focused(), 1, false); break;
        case Intent::Accept: accept(); break;
        default: break;
        }
    }

    const SettingRow& focused() const { return rows_[(size_t)list_.index]; }

    static bool canStep(const SettingRow& r, int delta) {
        return !r.readOnly && !r.choices.empty() && r.index + delta >= 0 && r.index + delta < (int)r.choices.size();
    }

    void step(const SettingRow& r, int delta, bool wrap) {
        if (r.readOnly || r.choices.empty()) return;
        const int to = stepIndex(r, delta, wrap);
        if (to != r.index) host_.setSetting(r.id, to);
    }

    // Accept steps forward and wraps; on "more" it opens the desktop window; a read-only row ignores it.
    void accept() {
        const SettingRow r = focused();   // a copy: the host may rebuild the rows under the call
        if (r.readOnly) return;
        if (r.id == kMoreId) host_.openMoreSettings();
        else step(r, 1, true);
    }

    // A click on the focused row: `rel` is how far across it the pointer is.  The value sits in the right half: its left
    // half steps back, its right half steps forward.  Anywhere else (and on a row with no value) is Accept.
    void click(float rel) {
        if (rel < 0.5f || focused().choices.empty()) accept();
        else step(focused(), rel < 0.75f ? -1 : 1, false);
    }

    // The label, the value between its chevrons, and a read-only row's note under the value.
    void drawRow(const DrawCtx& c, const SettingRow& r, ImVec2 pos, ImVec2 size, float f) const {
        const Metrics& m = c.m;
        const theme::Palette& pal = theme::pal();
        const float u = m.u, k = 1.0f + (m.focusScale - 1.0f) * f;   // the row grows about its centre (widgets::row)
        const ImVec2 mid(pos.x + size.x * 0.5f, pos.y + size.y * 0.5f);
        const ImVec2 p0(mid.x - size.x * k * 0.5f, mid.y - size.y * k * 0.5f), p1(mid.x + size.x * k * 0.5f, mid.y + size.y * k * 0.5f);
        const float pad = 24 * u * (0.6f + 0.4f * k), half = (p1.x - p0.x - 2 * pad) * 0.5f;
        const float px = m.body * (1.0f + 0.08f * f);
        const ImU32 tc = widgets::col(r.readOnly ? pal.textMuted : pal.text), mc = widgets::col(pal.textMuted);
        const bool hasNote = !r.note.empty();
        const float total = hasNote ? px + m.minText + 4 * u : px;
        const float y = p0.y + (p1.y - p0.y - total) * 0.5f;
        widgets::text(c.dl, px, ImVec2(p0.x + pad, p0.y + (p1.y - p0.y - px) * 0.5f), tc, r.label, half);

        const float chevW = 9 * u * k, chevH = 18 * u * k, gap = 24 * u * k, thick = 3 * u;
        const float cy = y + px * 0.5f;
        auto chevron = [&](float x, bool toRight, ImU32 col) {   // an open "<" or ">" (a font may lack the glyphs)
            const ImVec2 pts[3] = {ImVec2(toRight ? x : x + chevW, cy - chevH * 0.5f), ImVec2(toRight ? x + chevW : x, cy),
                                   ImVec2(toRight ? x : x + chevW, cy + chevH * 0.5f)};
            c.dl->AddPolyline(pts, 3, col, 0, thick);
        };
        const float inner = p1.x - pad;
        if (r.choices.empty()) {   // "more": it opens something
            if (!r.readOnly) chevron(inner - chevW, true, mc);
            return;
        }
        const int idx = std::clamp(r.index, 0, (int)r.choices.size() - 1);
        const std::string& value = r.choices[(size_t)idx];
        const float valueRight = inner - chevW - gap;
        const float vw = std::min(widgets::textWidth(px, value), half - chevW - gap);
        widgets::text(c.dl, px, ImVec2(valueRight - vw, y), tc, value, vw);
        if (canStep(r, -1)) chevron(valueRight - vw - gap - chevW, false, mc);
        if (canStep(r, 1)) chevron(inner - chevW, true, mc);
        if (hasNote) {
            const float nw = std::min(widgets::textWidth(m.minText, r.note), half);
            widgets::text(c.dl, m.minText, ImVec2(inner - nw, y + px + 4 * u), mc, r.note, nw);
        }
    }

    Host& host_;
    Shell& shell_;
    std::vector<int> groups_;           // the groups that have rows, in order; the rail's items
    std::vector<SettingRow> rows_;      // the focused group's rows, from the last refresh()
    LinearFocus rail_, list_;
    int group_ = -1;                    // the focused group (settingGroupLabel); -1 when there are no rows
    int lastGroup_ = -1;
    bool built_ = false;
    bool onRail_ = true;
    Tween scroll_;                      // vertical scroll of the rows, in pixels
    float scrollTo_ = 0;                // ... and where it is heading
    bool scrollInit_ = false;
    float lastU_ = 0;
    widgets::FocusTweens tweens_;
    ImVec2 lastMouse_{-FLT_MAX, -FLT_MAX};
    bool mouseSeen_ = false;
    float wheel_ = 0;                   // wheel notches not yet spent (a trackpad sends fractions)
};

}  // namespace

std::unique_ptr<Screen> makeSettingsScreen(Host& h, Shell& s) { return std::make_unique<SettingsScreen>(h, s); }

}  // namespace vj::tv
