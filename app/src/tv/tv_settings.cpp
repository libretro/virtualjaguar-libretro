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
//
// In the in-game menu's slot (Screen::setEmbedded) the screen has no rail of its own -- the menu names the page -- so the groups
// are a first list level in the menu's list area: Accept on a group opens its rows, Back returns to the groups, and Back on the
// groups is the menu's.  Every visit starts on the groups.
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
        const bool emb = embedded();
        // The menu's list area has no safe margin; the room the focused row grows into is the menu's own inset.
        const float inset = emb ? 0.5f * (m.focusScale - 1.0f) * c.size.x + 2 * m.ring : 0.0f;
        const float x0 = emb ? c.pos.x + inset : c.pos.x + m.safeX, right = emb ? c.pos.x + c.size.x - inset : c.pos.x + c.size.x - m.safeX;

        if (!mouseSeen_) {   // a pointer already resting on a row when the screen appears has not chosen it
            lastMouse_ = c.mouse;
            mouseSeen_ = true;
        }
        const bool moved = c.mouse.x != lastMouse_.x || c.mouse.y != lastMouse_.y;
        lastMouse_ = c.mouse;
        const bool pick = c.hovered && (moved || c.clicked);

        // ---- the rail --------------------------------------------------------------------------------------------------
        int hoverItem = -1;
        for (int i = 0; !emb && i < (int)groups_.size(); i++) {
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
        if (emb && onRail_) {   // the menu's first level: the groups as rows
            drawGroups(c, x0, std::max(1.0f, right - x0), pick);
            return;
        }
        if (rows_.empty()) {
            remember();
            return;
        }

        // ---- the rows: scrolled so the focused one shows ---------------------------------------------------------------
        const float lx = emb ? x0 : x0 + (kRailW + kRailGap) * u, lw = std::max(1.0f, right - lx);
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
            widgets::row(c.dl, m, ImVec2(lx, y), ImVec2(lw, rowH), a);
            drawRow(c, i, ImVec2(lx, y), ImVec2(lw, rowH), f);
            // The unscaled card, widened by what a focused row grows: its right chevron lies beyond the unscaled edge.
            const float slack = (m.focusScale - 1.0f) * 0.5f * lw;
            if (c.hovered && c.mouse.x >= lx - slack && c.mouse.x < lx + lw + slack && c.mouse.y >= y && c.mouse.y < y + rowH &&
                c.mouse.y >= rowsTop && c.mouse.y < viewBottom)   // not the part the clip hides
                hoverRow = i;
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
        if (hoverRow >= 0 && c.clicked) click(c.mouse.x, lx, lw);
        else if (hoverItem >= 0 && c.clicked) intent(Intent::Accept);
    }

    std::vector<widgets::Hint> hints() const override {
        std::vector<widgets::Hint> h;
        if (onRail_ || rows_.empty()) {
            if (!rows_.empty()) h.push_back({host_.glyph(Intent::Accept), "Open"});
            h.push_back({host_.glyph(Intent::Back), embedded() ? "Menu" : "Home"});
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
        int count[kSettingGroups] = {};
        for (const SettingRow& r : all)
            if (r.group >= 0 && r.group < kSettingGroups) count[r.group]++;
        groups_.clear();
        for (int g = 0; g < kSettingGroups; g++) {
            groupCount_[g] = count[g];
            if (count[g] > 0) groups_.push_back(g);
        }
        if (!built_) {
            built_ = true;
            onRail_ = embedded() || shell_.memory().get("settings/list") == 0;   // a fresh visit starts on the rail (the menu: the groups)
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
        if (!embedded()) shell_.memory().put("settings/list", onRail_ ? 0 : 1);   // the menu's visits always start on the groups
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

    // A click on the focused row at pointer x.  The value is right-aligned, so its place is what drawRow recorded: right of
    // the row's middle, a click left of the value's centre steps back and right of it steps forward (the chevrons lie
    // either side).  Anywhere else (and on a row with no value) is Accept.
    void click(float mouseX, float lx, float lw) {
        const SettingRow& r = focused();
        if (r.choices.empty() || mouseX < lx + lw * 0.5f) accept();
        else step(r, mouseX < valueMid_[(size_t)list_.index] ? -1 : 1, false);
    }

    // The in-menu first level: one row per group with how many settings it holds.  Hovering one focuses it, a click opens it, a
    // notch is a group.
    void drawGroups(const DrawCtx& c, float lx, float lw, bool pick) {
        const Metrics& m = c.m;
        const float u = m.u, top = c.pos.y + kRowsMargin * u, pitch = (kRowH + kRowGap) * u;
        groupTweens_.update((int)groups_.size(), rail_.index, 0, c.now);
        int hover = -1;
        for (int i = 0; i < (int)groups_.size(); i++) {
            const int n = groupCount_[groups_[(size_t)i]];
            widgets::RowArgs a;
            a.label = settingGroupLabel(groups_[(size_t)i]);
            a.value = std::to_string(n) + (n == 1 ? " setting" : " settings");
            a.focus = i == rail_.index ? groupTweens_.at(i, c.now) : 0.0f;
            if (widgets::row(c.dl, m, ImVec2(lx, top + (float)i * pitch), ImVec2(lw, kRowH * u), a).hovered) hover = i;
        }
        if (c.hovered && c.mouse.x >= lx && c.mouse.x < lx + lw && c.mouse.y >= c.pos.y && c.mouse.y < c.pos.y + c.size.y) wheel_ += c.wheel;
        const int steps = (int)wheel_;
        wheel_ -= (float)steps;
        for (int s = 0; s < std::abs(steps); s++) railIntent(steps > 0 ? Intent::Up : Intent::Down);   // wheel up = toward the first group
        if (hover >= 0 && pick) selectGroup(hover);
        remember();
        if (hover >= 0 && c.clicked) intent(Intent::Accept);   // opens the group
    }

    // The label, the value between its chevrons, and a read-only row's note under the value.
    void drawRow(const DrawCtx& c, int index, ImVec2 pos, ImVec2 size, float f) {
        const SettingRow& r = rows_[(size_t)index];
        const Metrics& m = c.m;
        const theme::Palette& pal = theme::pal();
        const float u = m.u, k = 1.0f + (m.focusScale - 1.0f) * f;   // the row grows about its centre (widgets::row)
        const ImVec2 mid(pos.x + size.x * 0.5f, pos.y + size.y * 0.5f);
        const ImVec2 p0(mid.x - size.x * k * 0.5f, mid.y - size.y * k * 0.5f), p1(mid.x + size.x * k * 0.5f, mid.y + size.y * k * 0.5f);
        const float pad = 24 * u * (0.6f + 0.4f * k), half = (p1.x - p0.x - 2 * pad) * 0.5f;
        const float px = m.body * (1.0f + 0.08f * f);
        const ImU32 tc = widgets::col(r.readOnly ? pal.textMuted : pal.text), mc = widgets::col(pal.textMuted);
        const bool hasNote = !r.note.empty();
        valueMid_.resize(rows_.size(), p1.x);
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
        valueMid_[(size_t)index] = valueRight - vw * 0.5f;
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
    int groupCount_[kSettingGroups] = {};   // how many rows each group has, from the last refresh()
    widgets::FocusTweens groupTweens_;  // the menu's group rows
    std::vector<SettingRow> rows_;      // the focused group's rows, from the last refresh()
    LinearFocus rail_, list_;
    std::vector<float> valueMid_;       // per row of the last draw: x of the middle of its value (where a click turns from back to forward)
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
