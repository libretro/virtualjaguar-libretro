// TV mode (#991): the Achievements screen.  Signed in with a game being tracked it is that game's list -- a header (icon,
// title, "N of M achievements", the hardcore chip) over the active leaderboard trackers (read-only, muted) and one row per
// achievement with its badge, points and the desktop's status line.  With no game it is the RetroAchievements Jaguar list,
// one row per game with a set ("<title> - <unlocked>/<total>" once the player's progress is known); A on a game the library has
// launches it.  Signed out, or in a build without rc_client, it is one line saying so.  Geometry is the Library's rail and
// the plan's "Screen geometry", in design units (u = viewport height / 1080).
//
// Everything comes from Host::achHeader() / achItems() / achGames() on every draw and every intent (the games call also
// keeps the list loaded and its progress fetched, so it must run every frame the list shows).  Where the focus was lives in
// Shell::memory(): the index and the row's own key under "achievements/rows" or "achievements/games", so a list that
// changed while the player was away is followed by item, not by place.  The rail holds no focus (there is one view at a
// time): it only names the view.
#include <algorithm>
#include <cfloat>
#include <cstdlib>

#include "theme.h"
#include "tv/tv_screen.h"
#include "tv/tv_shell.h"

namespace vj::tv {

namespace {

constexpr float kRailW = 344, kRailGap = 64, kItemH = 54;   // the Library's rail, design units
constexpr float kIcon = 96, kIconGap = 28;                  // the header's icon and the gap to its text
constexpr float kHeaderH = 56 + 4 + 28 + 8 + 32;            // title, count line, chip (24 text in a 32 pill)
constexpr float kRowsTopWithHeader = kHeaderH + 28;         // from the top of the content area (y = 150)
constexpr float kRowsTopWithCaption = 196 - 150;            // the games list: a caption line above it
constexpr float kRowH = 72, kBadgeRowH = 112, kRowGap = 14;
constexpr float kRowsMargin = 12;                           // room above the first row for a focused row's ring and shadow
constexpr const char* kNotAvailable = "RetroAchievements is not available in this build";
constexpr const char* kSignIn = "Sign in from Settings > Achievements in desktop mode";
constexpr const char* kRowsKey = "achievements/rows";
constexpr const char* kGamesKey = "achievements/games";

class AchievementsScreen : public Screen {
public:
    AchievementsScreen(Host& h, Shell& s, uint32_t focusGameId) : host_(h), shell_(s), focusGame_(focusGameId) {}

    const char* id() const override { return "achievements"; }

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
        const Metrics& m = c.m;
        const theme::Palette& pal = theme::pal();
        const float u = m.u;
        const float x0 = c.pos.x + m.safeX, right = c.pos.x + c.size.x - m.safeX;
        if (mode_ == Mode::Unavailable || mode_ == Mode::SignedOut) {
            drawMessage(c, mode_ == Mode::Unavailable ? kNotAvailable : kSignIn);
            return;
        }

        // ---- the rail: which view this is (it holds no focus) ----------------------------------------------------------
        {
            const ImVec2 p0(x0, c.pos.y), p1(x0 + kRailW * u, p0.y + kItemH * u);
            c.dl->AddRectFilled(p0, p1, widgets::col(pal.cardBgHover), 10 * u);
            widgets::text(c.dl, m.body, ImVec2(p0.x + 20 * u, p0.y + 12 * u), widgets::col(pal.text),
                          mode_ == Mode::Rows ? "This game" : "Jaguar games", (kRailW - 40) * u);
        }

        // ---- the header (a tracked game) or the caption (the games list) ------------------------------------------------
        const float lx = x0 + (kRailW + kRailGap) * u, lw = std::max(1.0f, right - lx);
        float rowsTop = c.pos.y;
        if (mode_ == Mode::Rows) {
            drawHeader(c, lx, lw);
            rowsTop += kRowsTopWithHeader * u;
        } else {
            std::string caption = std::to_string(rows_.size()) + (rows_.size() == 1 ? " game" : " games") + " with achievements";
            caption += " \xC2\xB7 " + std::to_string(inLibrary_) + " in your library";
            widgets::text(c.dl, m.caption, ImVec2(lx, c.pos.y), widgets::col(pal.textMuted), caption, lw);
            rowsTop += kRowsTopWithCaption * u;
        }
        if (rows_.empty()) {
            widgets::text(c.dl, m.body, ImVec2(lx, rowsTop), widgets::col(pal.textMuted),
                          mode_ == Mode::Rows ? "This game has no achievements yet." : "No Jaguar games to show yet.", lw);
            return;
        }

        // ---- the rows: scrolled so the focused one shows ---------------------------------------------------------------
        const float viewBottom = c.pos.y + c.size.y, viewH = viewBottom - rowsTop;
        std::vector<float> tops(rows_.size()), heights(rows_.size());
        float total = 0;
        for (size_t i = 0; i < rows_.size(); i++) {
            heights[i] = (rows_[i].tall ? kBadgeRowH : kRowH) * u;
            tops[i] = total;
            total += heights[i] + kRowGap * u;
        }
        total -= kRowGap * u;
        float target = scrollTo_;
        if (mode_ != lastMode_) target = 0;
        if (index_ >= 0) {
            const size_t f = (size_t)index_;
            if (tops[f] < target) target = tops[f];
            else if (tops[f] + heights[f] > target + viewH) target = tops[f] + heights[f] - viewH;
        }
        target = std::clamp(target, 0.0f, std::max(0.0f, total - viewH));
        scrollTo_ = target;
        if (!scrollInit_ || lastU_ != u || mode_ != lastMode_) {
            scroll_.from = scroll_.to = target;
            scroll_.start = c.now;
            scrollInit_ = true;
            lastU_ = u;
            lastMode_ = mode_;
        } else if (target != scroll_.to) {
            scroll_.retarget(target, c.now);
        }
        const float scroll = scroll_.at(c.now);
        tweens_.update((int)rows_.size(), index_, (int)mode_, c.now);

        if (!mouseSeen_) {   // a pointer already resting on a row when the screen appears has not chosen it
            lastMouse_ = c.mouse;
            mouseSeen_ = true;
        }
        const bool moved = c.mouse.x != lastMouse_.x || c.mouse.y != lastMouse_.y;
        lastMouse_ = c.mouse;

        int hover = -1;
        c.dl->PushClipRect(ImVec2(c.pos.x, rowsTop - kRowsMargin * u), ImVec2(c.pos.x + c.size.x, viewBottom), true);
        for (size_t i = 0; i < rows_.size(); i++) {
            const float y = rowsTop + tops[i] - scroll;
            if (y + heights[i] < rowsTop - kRowsMargin * u || y > viewBottom) continue;   // off screen: its picture is not asked for
            const Row& r = rows_[i];
            widgets::RowArgs a;
            a.label = r.label;
            a.sub = r.sub;
            a.value = r.value;
            a.enabled = !r.muted;
            a.focus = (int)i == index_ ? tweens_.at((int)i, c.now) : 0.0f;
            if (r.thumb) {
                a.thumbBox = a.thumbSquare = true;
                const Art t = r.badgeUrl.empty() ? Art() : host_.achImage(r.badgeUrl);
                a.thumb = t.tex;
                a.thumbSrcW = t.w;
                a.thumbSrcH = t.h;
            }
            const widgets::Hit hit = widgets::row(c.dl, m, ImVec2(lx, y), ImVec2(lw, heights[i]), a);
            if (hit.hovered && r.focusable && c.mouse.y >= rowsTop && c.mouse.y < viewBottom) hover = (int)i;   // not the part the clip hides
        }
        c.dl->PopClipRect();
        if (scroll > 0.5f) widgets::moreArrow(c.dl, m, ImVec2(lx + lw * 0.5f, rowsTop - 4 * u), true);
        if (scroll + viewH < total - 0.5f) widgets::moreArrow(c.dl, m, ImVec2(lx + lw * 0.5f, viewBottom - 10 * u), false);

        // ---- the pointer: hovering a row focuses it, a click on a game in the library plays it, a notch is a row --------
        if (hover >= 0 && (moved || c.clicked)) focusRow(hover);
        const bool overList = c.hovered && c.mouse.x >= lx && c.mouse.x < right && c.mouse.y >= rowsTop && c.mouse.y < viewBottom;
        if (overList) wheel_ += c.wheel;
        const int steps = (int)wheel_;
        wheel_ -= (float)steps;
        for (int s = 0; s < std::abs(steps); s++) move(steps > 0 ? -1 : 1);   // wheel up = toward the first row
        if (hover >= 0 && c.clicked) accept();
    }

    std::vector<widgets::Hint> hints() const override {
        std::vector<widgets::Hint> h;
        if (mode_ == Mode::Games && index_ >= 0 && rows_[(size_t)index_].inLibrary) h.push_back({host_.glyph(Intent::Accept), "Play"});
        h.push_back({host_.glyph(Intent::Back), "Home"});
        return h;
    }

private:
    enum class Mode { Unavailable, SignedOut, Rows, Games };

    // One row of either list, as drawn.
    struct Row {
        std::string key;             // what the focus memory follows: "a<id>" / "t<id>" for rows, the game's id for games
        uint32_t id = 0;
        std::string label, sub, value, badgeUrl;
        bool focusable = true, muted = false, thumb = true, tall = true;
        bool inLibrary = false;      // games: A plays it
    };

    const char* memoryKey() const { return mode_ == Mode::Rows ? kRowsKey : kGamesKey; }

    // Fetches the header and the list, and settles the focus: the row the memory names, else the remembered place, on a
    // row that can have it (trackers cannot).
    void refresh() {
        hdr_ = host_.achHeader();
        rows_.clear();
        index_ = -1;
        inLibrary_ = 0;
        mode_ = !hdr_.available ? Mode::Unavailable : !hdr_.signedIn ? Mode::SignedOut : hdr_.hasGame ? Mode::Rows : Mode::Games;
        if (mode_ == Mode::Rows) {
            for (const AchItem& a : host_.achItems()) {
                const bool tracker = a.kind == AchItem::Tracker;
                Row r;
                r.id = a.id;
                r.key = (tracker ? "t" : "a") + std::to_string(a.id);
                r.label = a.title;
                r.sub = a.status;
                r.badgeUrl = a.badgeUrl;
                r.focusable = !tracker;
                r.muted = tracker;
                r.thumb = r.tall = !tracker;
                if (!tracker) r.value = std::to_string(a.points) + " pts";
                rows_.push_back(std::move(r));
            }
        } else if (mode_ == Mode::Games) {
            for (const AchGame& g : host_.achGames()) {
                Row r;
                r.id = g.id;
                r.key = std::to_string(g.id);
                r.label = g.title + " \xC2\xB7 " +
                          (g.haveProgress ? std::to_string(g.unlocked) + "/" + std::to_string(g.achievements)
                                          : std::to_string(g.achievements) + " achievements");
                r.badgeUrl = g.imageUrl;
                r.muted = !g.inLibrary;
                r.inLibrary = g.inLibrary;
                r.tall = false;
                if (g.inLibrary) inLibrary_++;
                rows_.push_back(std::move(r));
            }
        }
        if (rows_.empty()) return;
        const int n = (int)rows_.size();
        int idx = shell_.memory().get(memoryKey());
        const std::string item = shell_.memory().getItem(memoryKey());
        if (!item.empty())
            for (int i = 0; i < n; i++)
                if (rows_[(size_t)i].key == item) idx = i;
        if (mode_ == Mode::Games && focusGame_) {   // opened from a game's details: its row, once the list has it
            const std::string want = std::to_string(focusGame_);
            for (int i = 0; i < n; i++)
                if (rows_[(size_t)i].key == want) idx = i;
            focusGame_ = 0;
        }
        idx = std::clamp(idx, 0, n - 1);
        int f = idx;
        while (f < n && !rows_[(size_t)f].focusable) f++;
        if (f == n) {
            f = idx;
            while (f >= 0 && !rows_[(size_t)f].focusable) f--;
        }
        if (f >= 0) focusRow(f);
    }

    void focusRow(int i) {
        index_ = i;
        shell_.memory().put(memoryKey(), i);
        shell_.memory().putItem(memoryKey(), rows_[(size_t)i].key);
    }

    // One step up or down, over the rows that cannot have the focus; at an end nothing moves.
    void move(int delta) {
        if (index_ < 0) return;
        int j = index_ + delta;
        while (j >= 0 && j < (int)rows_.size() && !rows_[(size_t)j].focusable) j += delta;
        if (j >= 0 && j < (int)rows_.size()) focusRow(j);
    }

    // A on a game the library has: open it.  Nothing else on this screen acts.
    void accept() {
        if (mode_ != Mode::Games || index_ < 0 || !rows_[(size_t)index_].inLibrary) return;
        host_.achPlay(rows_[(size_t)index_].id);
        shell_.launched();
    }

    // The game's icon, title, "N of M achievements" and the mode chip, right of the rail.
    void drawHeader(const DrawCtx& c, float lx, float lw) {
        const Metrics& m = c.m;
        const theme::Palette& pal = theme::pal();
        const float u = m.u;
        drawPicture(c.dl, m, ImVec2(lx, c.pos.y), kIcon * u, hdr_.iconUrl.empty() ? Art() : host_.achImage(hdr_.iconUrl));
        const float tx = lx + (kIcon + kIconGap) * u, tw = std::max(1.0f, lx + lw - tx);
        widgets::text(c.dl, m.titleSmall, ImVec2(tx, c.pos.y), widgets::col(pal.text), hdr_.title, tw);
        float y = c.pos.y + m.titleSmall + 4 * u;
        widgets::text(c.dl, 28 * u, ImVec2(tx, y), widgets::col(pal.textSecondary),
                      std::to_string(hdr_.unlocked) + " of " + std::to_string(hdr_.total) + " achievements", tw);
        y += 28 * u + 8 * u;
        // The chip: gold when hardcore is on, amber when it is wanted but paused, quiet for plain softcore.
        const bool hard = hdr_.mode == "Hardcore", paused = hdr_.mode.rfind("Softcore (", 0) == 0;
        const theme::Color edge = hard ? pal.gold : paused ? pal.warning : pal.textMuted;
        const float padX = 14 * u, cw = std::min(widgets::textWidth(m.minText, hdr_.mode) + 2 * padX, tw);
        c.dl->AddRect(ImVec2(tx, y), ImVec2(tx + cw, y + 32 * u), widgets::col(edge), 16 * u, 0, 2 * u);
        widgets::text(c.dl, m.minText, ImVec2(tx + padX, y + 4 * u), widgets::col(hard ? pal.gold : pal.textSecondary), hdr_.mode, cw - 2 * padX);
    }

    // A picture box with the picture fitted inside; the box alone while it loads or has none.
    static void drawPicture(ImDrawList* dl, const Metrics& m, ImVec2 p, float size, const Art& a) {
        const theme::Palette& pal = theme::pal();
        const ImVec2 p1(p.x + size, p.y + size);
        dl->AddRectFilled(p, p1, widgets::col(pal.stateBg), 8 * m.u);
        if (a.tex && a.w > 0 && a.h > 0) {
            const float k = std::min(size / a.w, size / a.h), iw = a.w * k, ih = a.h * k;
            const ImVec2 i0(p.x + (size - iw) * 0.5f, p.y + (size - ih) * 0.5f);
            dl->AddImage((ImTextureID)(intptr_t)a.tex, i0, ImVec2(i0.x + iw, i0.y + ih));
        } else {
            dl->AddRect(p, p1, widgets::col(pal.emptyEdge), 8 * m.u, 0, 2 * m.u);
        }
    }

    // A line on a card in the middle of the content area (Home's empty-library card).
    static void drawMessage(const DrawCtx& c, const char* msg) {
        const Metrics& m = c.m;
        const float u = m.u, w = std::min(1100 * u, c.size.x - 2 * m.safeX), pad = 48 * u;
        const ImVec2 ts = widgets::textSize(m.body, msg, w - 2 * pad);
        const ImVec2 size(w, ts.y + 2 * pad);
        const ImVec2 pos(c.pos.x + (c.size.x - size.x) * 0.5f, c.pos.y + (c.size.y - size.y) * 0.5f);
        widgets::panel(c.dl, m, pos, size);
        widgets::text(c.dl, m.body, ImVec2(pos.x + pad, pos.y + pad), widgets::col(theme::pal().text), msg, 0, w - 2 * pad);
    }

    Host& host_;
    Shell& shell_;
    uint32_t focusGame_;           // the game whose row the first list with it focuses (0 = none)
    AchHeader hdr_;
    Mode mode_ = Mode::Games;
    Mode lastMode_ = Mode::Unavailable;
    std::vector<Row> rows_;        // from the last refresh()
    int index_ = -1;               // the focused row; -1 when no row can have the focus
    unsigned inLibrary_ = 0;       // games of the list the library has
    Tween scroll_;                 // vertical scroll of the rows, in pixels
    float scrollTo_ = 0;           // ... and where it is heading
    bool scrollInit_ = false;
    float lastU_ = 0;
    widgets::FocusTweens tweens_;
    ImVec2 lastMouse_{-FLT_MAX, -FLT_MAX};
    bool mouseSeen_ = false;
    float wheel_ = 0;              // wheel notches not yet spent (a trackpad sends fractions)
};

}  // namespace

std::unique_ptr<Screen> makeAchievementsScreen(Host& h, Shell& s, uint32_t focusGameId) {
    return std::make_unique<AchievementsScreen>(h, s, focusGameId);
}

}  // namespace vj::tv
