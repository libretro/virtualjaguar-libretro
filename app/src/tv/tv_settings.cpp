// TV mode (#991): the Settings screen -- a rail of groups (the app's tabs, then the core's categories) beside the focused group's
// rows, each a label on the left and its value on the right between two chevrons.  Left / Right step a row, Accept steps it
// forward and wraps (so a one-handed player can still change everything), and the last row of Interface opens the desktop
// Settings window.  Geometry is the Library's rail and the plan's "Screen geometry", in design units (u = viewport
// height / 1080).
//
// The rows come from Host::settingRows() on every draw and every intent: a row the build or the session does not have
// is simply absent, and one that changed under the player is followed by its id.  Where focus was lives in
// Shell::memory(): the group under "settings/rail", each group's row under "settings/<group>" (index and row id), and
// whether the list had the focus under "settings/list", so coming back lands there.  A group is its key (SettingRow::group), so
// the memory survives groups that come and go.
//
// Both the rail and the list are long (the core's categories join the app's tabs; a category can hold a dozen rows), so each scrolls
// a whole item at a time (Scroll: LinearFocus::first eased by the Phase 3 tween), keeps the focus in view, and shows an arrow where
// there is more.  LT / RT (PagePrev / PageNext) jump a screenful, stopping at the ends.  A held direction repeats on the nav's own
// timer; the screen only has to tell a repeat from a press (Shell::repeatIntent) where a press means more than the repeat does.
//
// In the in-game menu's slot (Screen::setEmbedded) the screen has no rail of its own, so the groups are a first list level: Accept on
// a group opens its rows, Back returns to the groups, and Back on the groups is the menu's.  Every visit starts on the groups.  It takes
// the whole panel (wide(), as the Library does: the menu's own rail is not drawn) so the labels have the room, and names itself in the
// caption line above its list.
//
// A label that does not fit beside its value wraps at words to a second line (and a note under the value the same) instead of being cut;
// every row of a group then has the height of the tallest, so the window still scrolls a whole row at a time.
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
constexpr float kRailPitch = kItemH + kItemGap;
constexpr float kArrowRoom = 26;                                          // under a list that scrolls: the "more" arrow's place
constexpr float kArrowGap = 19;                                           // the arrow's centre, from a row list's first / last row (clear of its ring)
constexpr float kRailArrowGap = 16;                                       // ... from the rail's first / last item (its ring does not grow)
constexpr float kRowVPad = 7;                                             // above and below a row's text (72 u holds a label with a note line)
constexpr const char* kMoreId = "more";

std::string memoryKey(const std::string& group) { return "settings/" + group; }

// How many whole items of `pitch` fit under `viewH` (the last one has no gap after it).  A run that does not fit whole keeps
// `arrowRoom` free under its last item for the "more" arrow.
int itemsThatFit(float viewH, float pitch, float gap, int count, float arrowRoom) {
    const int all = std::max(1, (int)((viewH + gap) / pitch));
    if (all >= count) return all;
    return std::max(1, (int)((viewH - arrowRoom + gap) / pitch));
}

// A run of equal items scrolled a whole item at a time: LinearFocus::first is the first one showing and the pixel offset eases
// to it, so at rest no item is cut by the edge of its view.  `restart`: another run took the place (start from the top, no easing).
struct Scroll {
    Tween tween;
    bool init = false;
    float lastU = 0, lastPitch = 0;
    float update(LinearFocus& f, int visible, float pitch, float u, bool restart, double now) {
        if (restart) f.first = 0;
        f.first = std::clamp(f.first, 0, std::max(0, f.count - visible));
        f.scrollToShow(visible);
        const float target = (float)f.first * pitch;
        if (!init || lastU != u || lastPitch != pitch || restart) {
            tween.from = tween.to = target;
            tween.start = now;
            init = true;
            lastU = u;
            lastPitch = pitch;
        } else if (target != tween.to) {
            tween.retarget(target, now);
        }
        return tween.at(now);
    }
};

// LT / RT: the focus and the window both move a screenful, and stop at the ends.
void pageBy(LinearFocus& f, int visible, int dir) {
    const int step = std::max(1, visible);
    f.index = std::clamp(f.index + dir * step, 0, std::max(0, f.count - 1));
    f.first = std::clamp(f.first + dir * step, 0, std::max(0, f.count - visible));
}

class SettingsScreen : public Screen {
public:
    SettingsScreen(Host& h, Shell& s) : host_(h), shell_(s) {}

    const char* id() const override { return "settings"; }
    bool wide() const override { return embedded(); }   // in the in-game menu's slot the labels get the whole panel (the menu draws no rail)

    std::string focusLabel() override {
        refresh();
        if (group_.empty() || rows_.empty()) return {};
        return onRail_ ? groupLabel() : rows_[(size_t)list_.index].label;
    }

    std::string focusGroup() override {
        refresh();
        return group_;
    }

    void intent(Intent in) override {
        refresh();
        if (!shell_.repeatIntent()) enteredByRight_ = false;   // a fresh press: whatever the last hold started is over
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

        // ---- the rail: the groups, a window of whole items that follows the focus ---------------------------------------
        int hoverItem = -1;
        if (!emb) {
            const float pitch = kRailPitch * u, itemH = kItemH * u;
            railVisible_ = itemsThatFit(c.size.y, pitch, kItemGap * u, (int)groups_.size(), kArrowRoom * u);
            const float scroll = railScroll_.update(rail_, railVisible_, pitch, u, false, c.now);
            const float winBottom = c.pos.y + (float)railVisible_ * pitch - kItemGap * u;   // the window's lower edge
            const float ringRoom = m.ring;                                                  // the ring is drawn a little outside its item
            c.dl->PushClipRect(ImVec2(c.pos.x, c.pos.y - ringRoom), ImVec2(c.pos.x + c.size.x, winBottom + ringRoom), true);
            for (int i = 0; i < (int)groups_.size(); i++) {
                const float y = c.pos.y + (float)i * pitch - scroll;
                if (y + itemH <= c.pos.y - ringRoom || y >= winBottom) continue;   // wholly outside: not drawn at all
                const ImVec2 p0(x0, y), p1(x0 + kRailW * u, y + itemH);
                const bool sel = i == rail_.index;
                if (sel) c.dl->AddRectFilled(p0, p1, widgets::col(pal.cardBgHover), 10 * u);
                if (sel && onRail_) c.dl->AddRect(p0, p1, widgets::col(pal.text), 10 * u, 0, m.ring);
                widgets::text(c.dl, m.body, ImVec2(p0.x + 20 * u, p0.y + 12 * u), widgets::col(sel ? pal.text : pal.textMuted),
                              groups_[(size_t)i].label, (kRailW - 40) * u);
                if (c.hovered && c.mouse.x >= p0.x && c.mouse.x < p1.x && c.mouse.y >= std::max(p0.y, c.pos.y) &&
                    c.mouse.y < std::min(p1.y, winBottom))
                    hoverItem = i;
            }
            c.dl->PopClipRect();
            drawMoreArrows(c, x0 + kRailW * u * 0.5f, c.pos.y - kRailArrowGap * u, winBottom + kRailArrowGap * u, rail_.first > 0,
                           rail_.first + railVisible_ < (int)groups_.size());
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

        // ---- the rows: a window of whole rows that follows the focus ----------------------------------------------------
        const float lx = emb ? x0 : x0 + (kRailW + kRailGap) * u, lw = std::max(1.0f, right - lx);
        const std::string count = std::to_string(rows_.size()) + (rows_.size() == 1 ? " setting" : " settings");
        widgets::text(c.dl, m.caption, ImVec2(lx, c.pos.y), widgets::col(pal.textMuted),
                      groupLabel() + " \xC2\xB7 " + count, lw);

        layoutRows(m, lw);
        const float rowsTop = c.pos.y + kListTop * u, viewBottom = c.pos.y + c.size.y, viewH = std::max(1.0f, viewBottom - rowsTop);
        const float pitch = rowH_ + kRowGap * u, rowH = rowH_;
        listVisible_ = itemsThatFit(viewH, pitch, kRowGap * u, (int)rows_.size(), kArrowRoom * u);
        const float scroll = listScroll_.update(list_, listVisible_, pitch, u, group_ != lastGroup_, c.now);
        lastGroup_ = group_;
        tweens_.update((int)rows_.size(), list_.index, groupSerial_, c.now);
        const float winBottom = rowsTop + (float)listVisible_ * pitch - kRowGap * u;   // the window's lower edge

        int hoverRow = -1;
        c.dl->PushClipRect(ImVec2(c.pos.x, rowsTop - kRowsMargin * u), ImVec2(c.pos.x + c.size.x, winBottom + 8 * u), true);
        for (int i = 0; i < (int)rows_.size(); i++) {
            const float y = rowsTop + (float)i * pitch - scroll;
            if (y + rowH <= rowsTop - kRowsMargin * u || y >= winBottom) continue;   // wholly outside: not drawn at all
            const float f = !onRail_ && i == list_.index ? tweens_.at(i, c.now) : 0.0f;
            widgets::RowArgs a;   // the card and the focus ring; the label and the value are drawn here
            a.focus = f;
            widgets::row(c.dl, m, ImVec2(lx, y), ImVec2(lw, rowH), a);
            drawRow(c, i, ImVec2(lx, y), ImVec2(lw, rowH), f);
            // The unscaled card, widened by what a focused row grows: its right chevron lies beyond the unscaled edge.
            const float slack = (m.focusScale - 1.0f) * 0.5f * lw;
            if (c.hovered && c.mouse.x >= lx - slack && c.mouse.x < lx + lw + slack && c.mouse.y >= y && c.mouse.y < y + rowH &&
                c.mouse.y >= rowsTop && c.mouse.y < winBottom)   // not the part the clip hides
                hoverRow = i;
        }
        c.dl->PopClipRect();
        drawMoreArrows(c, lx + lw * 0.5f, rowsTop - kArrowGap * u, winBottom + kArrowGap * u, list_.first > 0,
                       list_.first + listVisible_ < (int)rows_.size());

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
            pageHint(h, (int)groups_.size() > railVisible_);   // a rail (or groups level) that scrolls
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
        pageHint(h, (int)rows_.size() > listVisible_);   // a list that scrolls
        h.push_back({host_.glyph(Intent::Back), "Groups"});
        return h;
    }

private:
    // "LT RT  Page" on a list that scrolls: one glyph if the other has none (or both are the same key), nothing if neither has.
    void pageHint(std::vector<widgets::Hint>& h, bool scrolls) const {
        if (!scrolls) return;
        const std::string lt = host_.glyph(Intent::PagePrev), rt = host_.glyph(Intent::PageNext);
        const std::string glyph = lt.empty() ? rt : rt.empty() || lt == rt ? lt : lt + " " + rt;
        if (!glyph.empty()) h.push_back({glyph, "Page"});
    }

    // How each row of the focused group is laid out at rest (the label and the note broken into lines) and how tall its rows are: the
    // tallest row's, never under kRowH.  The label gets the row's width less the value column, which is sized by the widest choice
    // (not the one showing) so that stepping a value never rewraps the label; a note under the value is right-aligned in half a row.
    void layoutRows(const Metrics& m, float lw) {
        const float u = m.u, pad = 24 * u, inner = std::max(1.0f, lw - 2 * pad), half = inner * 0.5f;
        const float chevW = 9 * u, gap = 24 * u;
        lay_.assign(rows_.size(), RowLay());
        rowH_ = kRowH * u;
        for (size_t i = 0; i < rows_.size(); i++) {
            const SettingRow& r = rows_[i];
            RowLay& L = lay_[i];
            float avail = inner;   // a note row ("Available once a game has run"): the whole row is its text
            if (!r.choices.empty()) {
                float widest = 0;
                for (const std::string& ch : r.choices) widest = std::max(widest, widgets::textWidth(m.body, ch));
                avail = inner - (std::min(widest, half - chevW - gap) + 2 * (chevW + gap)) - gap;
            } else if (!r.readOnly) {
                avail = inner - chevW - gap;   // "more": a chevron
            }
            L.label = widgets::wrapLines(m.body, r.label, std::max(avail, 0.4f * inner), 2);
            if (!r.choices.empty() && !r.note.empty()) L.note = widgets::wrapLines(m.minText, r.note, half, 2);
            const float n = (float)std::max<size_t>(1, L.label.size());
            const float labelH = n * m.body + (n - 1) * 4 * u;
            const float noteH = L.note.empty() ? 0.0f : 4 * u + (float)L.note.size() * m.minText + (float)(L.note.size() - 1) * 2 * u;
            rowH_ = std::max(rowH_, std::max(labelH, m.body + noteH) + 2 * kRowVPad * u);
        }
    }

    // Fetches the rows, takes their groups in first-seen order (the host lists them in the rail's order), and settles the focus:
    // the rail follows the group's key (not its position: groups come and go with the session) and the list follows the row's id.
    void refresh() {
        const std::vector<SettingRow> all = host_.settingRows();
        groups_ = groupsOf(all);
        groupCount_.assign(groups_.size(), 0);
        for (const SettingRow& r : all)
            for (size_t g = 0; g < groups_.size(); g++)
                if (groups_[g].key == r.group) groupCount_[g]++;
        if (!built_) {
            built_ = true;
            onRail_ = embedded() || shell_.memory().get("settings/list") == 0;   // the Base layer resumes the level it was left on (list or rail); the menu's slot always starts on the groups
        }
        rail_.count = (int)groups_.size();
        rail_.index = 0;
        const std::string wantGroup = shell_.memory().getItem("settings/rail");
        for (int i = 0; i < (int)groups_.size(); i++)
            if (groups_[(size_t)i].key == wantGroup) rail_.index = i;
        const std::string now = groups_.empty() ? std::string() : groups_[(size_t)rail_.index].key;
        if (now != group_) groupSerial_++;
        group_ = now;
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
        if (group_.empty() || rows_.empty()) return;
        shell_.memory().putItem("settings/rail", group_);
        shell_.memory().put(memoryKey(group_), list_.index);
        shell_.memory().putItem(memoryKey(group_), rows_[(size_t)list_.index].id);
    }

    // Moving on the rail shows that group's rows at once, at the place they were left.
    void selectGroup(int k) {
        if (k < 0 || k >= (int)groups_.size() || k == rail_.index) return;
        remember();
        shell_.memory().putItem("settings/rail", groups_[(size_t)k].key);
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
        case Intent::PagePrev:
        case Intent::PageNext: {
            pageBy(f, railVisible_, in == Intent::PageNext ? 1 : -1);
            rail_.first = f.first;
            selectGroup(f.index);
            break;
        }
        case Intent::Right:
        case Intent::Accept:
            if (!rows_.empty()) {   // to the list, at the remembered row
                onRail_ = false;
                enteredByRight_ = in == Intent::Right;   // a Right still held repeats into the list: those repeats are not steps
            }
            break;
        default: break;
        }
    }

    void listIntent(Intent in) {
        if (rows_.empty()) return;
        switch (in) {
        case Intent::Up: list_.move(-1); break;
        case Intent::Down: list_.move(1); break;
        case Intent::PagePrev: pageBy(list_, listVisible_, -1); break;
        case Intent::PageNext: pageBy(list_, listVisible_, 1); break;
        case Intent::Left:
            if (pressOnly(focused())) break;
            if (canStep(focused(), -1)) step(focused(), -1, false);
            else if (!shell_.repeatIntent()) onRail_ = true;   // the left edge of the row: the groups (a held Left stops there)
            break;
        case Intent::Right:
            if (pressOnly(focused())) break;
            if (!(shell_.repeatIntent() && enteredByRight_)) step(focused(), 1, false);
            break;
        case Intent::Accept: accept(); break;
        default: break;
        }
    }

    // A row whose step is costly (the Shader row parses and compiles a preset and saves the config, and a broken one toasts for seconds)
    // moves on a press only: a held Left / Right (the 80 ms repeat) is not another step.
    bool pressOnly(const SettingRow& r) const { return shell_.repeatIntent() && r.id == "shader"; }

    const SettingRow& focused() const { return rows_[(size_t)list_.index]; }
    std::string groupLabel() const { return rail_.index >= 0 && rail_.index < (int)groups_.size() ? groups_[(size_t)rail_.index].label : std::string(); }

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

    // The in-menu first level: one row per group with how many settings it holds, a window of whole rows that follows the focus.
    // Hovering one focuses it, a click opens it, a notch is a group.
    void drawGroups(const DrawCtx& c, float lx, float lw, bool pick) {
        const Metrics& m = c.m;
        const float u = m.u, top = c.pos.y + kListTop * u, pitch = (kRowH + kRowGap) * u, rowH = kRowH * u;
        widgets::text(c.dl, m.caption, ImVec2(lx, c.pos.y), widgets::col(theme::pal().textMuted),   // this level's name: the menu draws no rail
                      "Settings \xC2\xB7 " + std::to_string(groups_.size()) + (groups_.size() == 1 ? " group" : " groups"), lw);
        railVisible_ = itemsThatFit(std::max(1.0f, c.pos.y + c.size.y - top), pitch, kRowGap * u, (int)groups_.size(), kArrowRoom * u);
        const float scroll = railScroll_.update(rail_, railVisible_, pitch, u, false, c.now);
        const float winBottom = top + (float)railVisible_ * pitch - kRowGap * u;
        groupTweens_.update((int)groups_.size(), rail_.index, 0, c.now);
        int hover = -1;
        c.dl->PushClipRect(ImVec2(c.pos.x, top - kRowsMargin * u), ImVec2(c.pos.x + c.size.x, winBottom + 8 * u), true);
        for (int i = 0; i < (int)groups_.size(); i++) {
            const float y = top + (float)i * pitch - scroll;
            if (y + rowH <= top - kRowsMargin * u || y >= winBottom) continue;   // wholly outside: not drawn at all
            const int n = groupCount_[(size_t)i];
            widgets::RowArgs a;
            a.label = groups_[(size_t)i].label;
            a.value = std::to_string(n) + (n == 1 ? " setting" : " settings");
            a.focus = i == rail_.index ? groupTweens_.at(i, c.now) : 0.0f;
            const bool h = widgets::row(c.dl, m, ImVec2(lx, y), ImVec2(lw, rowH), a).hovered;
            if (h && c.mouse.y >= top && c.mouse.y < winBottom) hover = i;   // not the part the clip hides
        }
        c.dl->PopClipRect();
        drawMoreArrows(c, lx + lw * 0.5f, top - kArrowGap * u, winBottom + kArrowGap * u, rail_.first > 0,
                       rail_.first + railVisible_ < (int)groups_.size());
        if (c.hovered && c.mouse.x >= lx && c.mouse.x < lx + lw && c.mouse.y >= c.pos.y && c.mouse.y < c.pos.y + c.size.y) wheel_ += c.wheel;
        const int steps = (int)wheel_;
        wheel_ -= (float)steps;
        for (int s = 0; s < std::abs(steps); s++) railIntent(steps > 0 ? Intent::Up : Intent::Down);   // wheel up = toward the first group
        if (hover >= 0 && pick) selectGroup(hover);
        remember();
        if (hover >= 0 && c.clicked) intent(Intent::Accept);   // opens the group
    }

    // The two arrows of a scrolling run, centred on x: above its first item (y = `upY`) and below its last (`downY`).
    static void drawMoreArrows(const DrawCtx& c, float x, float upY, float downY, bool up, bool down) {
        if (up) widgets::moreArrow(c.dl, c.m, ImVec2(x, upY), true);
        if (down) widgets::moreArrow(c.dl, c.m, ImVec2(x, downY), false);
    }

    // The label (its lines from layoutRows), the value between its chevrons, and a note under the value.
    void drawRow(const DrawCtx& c, int index, ImVec2 pos, ImVec2 size, float f) {
        const SettingRow& r = rows_[(size_t)index];
        const RowLay& L = lay_[(size_t)index];
        const Metrics& m = c.m;
        const theme::Palette& pal = theme::pal();
        const float u = m.u, k = 1.0f + (m.focusScale - 1.0f) * f;   // the row grows about its centre (widgets::row)
        const ImVec2 mid(pos.x + size.x * 0.5f, pos.y + size.y * 0.5f);
        const ImVec2 p0(mid.x - size.x * k * 0.5f, mid.y - size.y * k * 0.5f), p1(mid.x + size.x * k * 0.5f, mid.y + size.y * k * 0.5f);
        const float pad = 24 * u * (0.6f + 0.4f * k), half = (p1.x - p0.x - 2 * pad) * 0.5f;
        const float px = m.body * (1.0f + 0.08f * f), notePx = m.minText * (1.0f + 0.08f * f);
        const ImU32 tc = widgets::col(r.readOnly ? pal.textMuted : pal.text), mc = widgets::col(pal.textMuted);
        const float hgt = p1.y - p0.y;
        valueMid_.resize(rows_.size(), p1.x);
        const float nL = (float)std::max<size_t>(1, L.label.size());
        const float labelH = nL * px + (nL - 1) * 4 * u;
        widgets::textLines(c.dl, px, ImVec2(p0.x + pad, p0.y + (hgt - labelH) * 0.5f), tc, L.label, 4 * u);

        // The value's side: the value (and, under it, the note's lines), as one block centred in the row.
        const float noteH = L.note.empty() ? 0.0f : 4 * u + (float)L.note.size() * notePx + (float)(L.note.size() - 1) * 2 * u;
        const float y = p0.y + (hgt - (px + noteH)) * 0.5f;
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
        if (!L.note.empty()) widgets::textLines(c.dl, notePx, ImVec2(inner, y + px + 4 * u), mc, L.note, 2 * u, true);
    }

    Host& host_;
    Shell& shell_;
    std::vector<SettingGroup> groups_;  // the groups that have rows, in order; the rail's items
    std::vector<int> groupCount_;       // how many rows each of them has, from the last refresh()
    widgets::FocusTweens groupTweens_;  // the menu's group rows
    std::vector<SettingRow> rows_;      // the focused group's rows, from the last refresh()
    struct RowLay {
        std::vector<std::string> label, note;   // the lines each is broken into (layoutRows)
    };
    std::vector<RowLay> lay_;           // per row of rows_, from the last draw
    float rowH_ = 0;                    // the rows' height at the last draw, pixels
    LinearFocus rail_, list_;
    std::vector<float> valueMid_;       // per row of the last draw: x of the middle of its value (where a click turns from back to forward)
    std::string group_;                 // the focused group's key; "" when there are no rows
    std::string lastGroup_;
    int groupSerial_ = 0;               // moves with group_: what the row tweens restart on
    bool built_ = false;
    bool onRail_ = true;
    Scroll railScroll_, listScroll_;    // the rail's window and the rows' (whole items; first is LinearFocus::first)
    int railVisible_ = 8, listVisible_ = 8;   // how many items each window showed at the last draw (LT / RT page by them)
    bool enteredByRight_ = false;       // the last move from the rail to the list was a Right: its repeats are not steps
    widgets::FocusTweens tweens_;
    ImVec2 lastMouse_{-FLT_MAX, -FLT_MAX};
    bool mouseSeen_ = false;
    float wheel_ = 0;                   // wheel notches not yet spent (a trackpad sends fractions)
};

}  // namespace

std::unique_ptr<Screen> makeSettingsScreen(Host& h, Shell& s) { return std::make_unique<SettingsScreen>(h, s); }

}  // namespace vj::tv
