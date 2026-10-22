// TV mode (#991): the Library screen -- a rail of lists (All games, Favorites, Recently played, Jaguar CD) beside a
// 6-column cover grid, with A-Z jumps on LT / RT, a letter picker on Y and typing to filter.  Geometry is the spec's
// Library mockup in design units (u = viewport height / 1080).
//
// The list is fetched by value once per draw and per event (a GameRef is keyed by file path, so a rescan between two
// frames cannot leave the screen holding a dead entry); the filter is applied to that fetch.  Where focus was on each
// list lives in Shell::memory() ("library/<list>"), the selected list in "library/rail", so coming back lands there.
// While a filter is on, the focus belongs to the filtered list and is not written to memory: clearing the filter
// brings the player back to where they were.
#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cstdlib>
#include <cstring>

#include "theme.h"
#include "tv/tv_focus.h"
#include "tv/tv_screen.h"
#include "tv/tv_shell.h"

namespace vj::tv {

namespace {

constexpr int kLists = 4;
constexpr int kCols = 6;
constexpr float kRailW = 344, kRailGap = 64, kItemH = 54, kItemGap = 8;   // design units
constexpr float kGridTop = 196 - 150;       // from the top of the content area (y = 150)
constexpr float kRowExtra = 72;             // under each tile: room for the focused tile's caption
constexpr float kColGap = 32;
constexpr size_t kMaxFilter = 32;
const char* const kLetters = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";   // the picker's cells, row by row
constexpr int kLetterCount = 36;

struct ListKind {
    const char* id;
    const char* name;
    List list;
};
const ListKind kKinds[kLists] = {
    {"all", "All games", List::All},
    {"favorites", "Favorites", List::Favorites},
    {"recent", "Recently played", List::Recent},
    {"disc", "Jaguar CD", List::Disc},
};

std::string lower(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

class LettersScreen;

class LibraryScreen : public Screen {
public:
    LibraryScreen(Host& h, Shell& s) : host_(h), shell_(s) {
        list_ = std::clamp(s.memory().get("library/rail"), 0, kLists - 1);
        rail_.count = kLists;
        rail_.index = list_;
        grid_.cols = kCols;
        grid_.index = std::max(0, s.memory().get(std::string("library/") + kKinds[list_].id));
        focusKey_ = s.memory().getItem(std::string("library/") + kKinds[list_].id);
    }

    const char* id() const override { return "library"; }
    bool wide() const override { return embedded(); }   // in the in-game menu's slot the rail + grid take the whole panel

    void intent(Intent in) override {
        refresh();
        if (onRail_) railIntent(in);
        else gridIntent(in);
        remember();
    }

    // Typing filters by a substring of the title.  Backspace drops the last character; with none left it is Back (false).
    bool typed(char c) override {
        refresh();
        if (c == '\b') {
            if (filter_.empty()) return false;
            filter_.pop_back();
            if (filter_.empty()) {   // back to the place the filter did not lose
                grid_.index = std::max(0, shell_.memory().get(std::string("library/") + kKinds[list_].id));
                focusKey_ = shell_.memory().getItem(std::string("library/") + kKinds[list_].id);
            } else {
                grid_.index = 0;
                focusKey_.clear();
            }
        } else {
            if (filter_.size() >= kMaxFilter) return true;   // used, full
            if (filter_.empty()) remember();                 // the place the filter must not lose
            filter_ += c;
            grid_.index = 0;
            focusKey_.clear();   // the filtered list starts at its first game
        }
        snap_ = true;
        refresh();
        return true;
    }

    // The letter picker chose a title of the (filtered) list: focus it, in the grid.
    void focusTitle(int index) {
        refresh();
        if (games_.empty()) return;
        grid_.index = std::clamp(index, 0, (int)games_.size() - 1);
        onRail_ = false;
        remember();
    }

    const std::vector<std::string>& titles() const { return titles_; }
    char focusedInitial() const {
        return grid_.index >= 0 && grid_.index < (int)titles_.size() ? initialOf(titles_[(size_t)grid_.index]) : 'A';
    }

    void draw(const DrawCtx& c) override {
        refresh();
        const Metrics& m = c.m;
        const theme::Palette& pal = theme::pal();
        const float u = m.u;
        // In the in-game menu's slot the area is the menu's panel inside its inset: no safe margin, only the room the focused tile and its
        // ring grow into.
        const float edge = embedded() ? 12 * u : m.safeX;
        const float x0 = c.pos.x + edge, right = c.pos.x + c.size.x - edge;

        if (!mouseSeen_) {   // a pointer already resting on a tile when the screen appears has not chosen it
            lastMouse_ = c.mouse;
            mouseSeen_ = true;
        }
        const bool moved = c.mouse.x != lastMouse_.x || c.mouse.y != lastMouse_.y;
        lastMouse_ = c.mouse;
        // The letter picker is drawn over this screen with the same pointer: it is the one that gets it.
        const bool live = c.hovered && (embedded() || shell_.screenId() == id());   // (the slot's screen is not on the Base stack)
        const bool pick = live && (moved || c.clicked || c.rightClicked);
        const bool clicked = live && c.clicked, rightClicked = live && c.rightClicked;

        // ---- the rail ------------------------------------------------------------------------------------------------
        int hoverItem = -1;
        for (int i = 0; i < kLists; i++) {
            const ImVec2 p0(x0, c.pos.y + (float)i * (kItemH + kItemGap) * u), p1(x0 + kRailW * u, p0.y + kItemH * u);
            const bool sel = i == list_;
            if (sel) c.dl->AddRectFilled(p0, p1, widgets::col(pal.cardBgHover), 10 * u);
            if (sel && onRail_) c.dl->AddRect(p0, p1, widgets::col(pal.text), 10 * u, 0, m.ring);
            widgets::text(c.dl, m.body, ImVec2(p0.x + 20 * u, p0.y + 12 * u), widgets::col(sel ? pal.text : pal.textMuted),
                          kKinds[i].name, (kRailW - 40) * u);
            if (live && c.mouse.x >= p0.x && c.mouse.x < p1.x && c.mouse.y >= p0.y && c.mouse.y < p1.y) hoverItem = i;
        }
        if (hoverItem >= 0 && pick) {   // hovering an item selects it: the grid switches at once
            if (hoverItem != list_) switchList(hoverItem);
            onRail_ = true;
            rail_.index = hoverItem;
        }

        // ---- the grid ------------------------------------------------------------------------------------------------
        const float gx = x0 + (kRailW + kRailGap) * u, gridW = right - gx;
        const float tileW = std::max(1.0f, (gridW - (kCols - 1) * kColGap * u) / (float)kCols), tileH = tileW * 7 / 5;
        const float pitch = tileH + kRowExtra * u;
        const float top = c.pos.y + kGridTop * u, bottom = c.pos.y + c.size.y;
        const int visibleRows = std::max(1, (int)((bottom - top + kRowExtra * u) / pitch));
        const int n = (int)games_.size();

        const std::string count = std::to_string(n) + (n == 1 ? " game" : " games");
        const ImVec2 cap = widgets::text(c.dl, m.caption, ImVec2(gx, c.pos.y), widgets::col(pal.textMuted),
                                         std::string(kKinds[list_].name) + " \xC2\xB7 " + count);
        if (!filter_.empty())
            widgets::text(c.dl, m.caption, ImVec2(gx + cap.x + 24 * u, c.pos.y), widgets::col(pal.textMuted), "Filter: " + filter_,
                          std::max(0.0f, right - gx - cap.x - 24 * u));

        if (games_.empty()) {
            resumeOk_ = false;
            widgets::text(c.dl, m.body, ImVec2(gx, top + 8 * u), widgets::col(pal.textMuted),
                          filter_.empty() ? "Nothing here yet" : "No games match", right - gx);
            return;
        }

        const GameRef focusedGame = games_[(size_t)grid_.index];
        const std::vector<StateRef> st = host_.states(focusedGame);
        resumeOk_ = !st.empty() && host_.canLoadState();

        grid_.firstRow = std::min(grid_.firstRow, std::max(0, grid_.rows() - visibleRows));
        grid_.scrollToShow(visibleRows);
        const float target = (float)grid_.firstRow * pitch;
        if (snap_ || !scrollInit_ || lastU_ != u) {
            scroll_.from = scroll_.to = target;
            scroll_.start = c.now;
            scrollInit_ = true;
            lastU_ = u;
            snap_ = false;
        } else if (target != scroll_.to) {
            scroll_.retarget(target, c.now);
        }
        const float scroll = scroll_.at(c.now);
        tweens_.update(n, grid_.index, list_, c.now);

        const float clipTop = c.pos.y + 30 * u;   // room above the first row for the focused tile's growth and ring
        int hoverTile = -1;
        c.dl->PushClipRect(ImVec2(c.pos.x, clipTop), ImVec2(c.pos.x + c.size.x, bottom), true);
        for (int r = 0; r < grid_.rows(); r++) {
            const float y = top + (float)r * pitch - scroll;
            if (y + pitch <= clipTop || y >= bottom) continue;
            for (int col = 0; col < kCols; col++) {
                const int j = r * kCols + col;
                if (j >= n) break;
                const float tx = gx + (float)col * (tileW + kColGap * u);
                widgets::TileArgs a = tileArgs(games_[(size_t)j]);
                a.focus = !onRail_ && j == grid_.index ? tweens_.at(j, c.now) : 0.0f;
                const widgets::Hit hit = widgets::tile(c.dl, m, ImVec2(tx, y), ImVec2(tileW, tileH), a);
                if (live && hit.hovered && c.mouse.y >= clipTop && c.mouse.y < bottom) hoverTile = j;
                if (!onRail_ && j == grid_.index) {   // the focused tile's caption, in the room under the row
                    std::string line = focusedGame.region.empty() ? "" : focusedGame.region + " \xC2\xB7 ";
                    line += focusedGame.disc ? "Jaguar CD" : "Cartridge";
                    if (!st.empty()) line += " \xC2\xB7 " + std::to_string(st.size()) + (st.size() == 1 ? " state" : " states");
                    const float capW = 2 * tileW;
                    widgets::text(c.dl, m.minText, ImVec2(std::min(tx, right - capW), y + tileH + 18 * u),
                                  widgets::col(pal.textSecondary), line, capW);
                }
            }
        }
        c.dl->PopClipRect();

        // ---- the pointer: hovering a tile focuses it, a click plays it, a right-click is Menu, a notch is a row ----------
        if (hoverTile >= 0 && (pick)) {
            grid_.index = hoverTile;
            onRail_ = false;
        }
        const bool overGrid = live && c.mouse.x >= gx && c.mouse.x < right && c.mouse.y >= top && c.mouse.y < bottom;
        if (overGrid) wheel_ += c.wheel;
        const int steps = (int)wheel_;
        wheel_ -= (float)steps;
        for (int s = 0; s < std::abs(steps); s++) {   // wheel up = toward the first row
            grid_.move(0, steps > 0 ? -1 : 1);
            onRail_ = false;
        }
        remember();
        if (hoverTile >= 0 && clicked) intent(Intent::Accept);
        else if (hoverTile >= 0 && rightClicked) intent(Intent::Menu);
        else if (hoverItem >= 0 && clicked) intent(Intent::Accept);
    }

    std::vector<widgets::Hint> hints() const override {
        // In the menu's slot Back leaves for the menu, and Details / the letter picker (which push onto the Base stack, not shown under a
        // game) are not offered.
        const char* way = embedded() ? "Menu" : "Home";
        if (games_.empty()) return {{host_.glyph(Intent::Back), way}};   // an empty grid: no game to open, play or jump in
        if (onRail_) return {{host_.glyph(Intent::Accept), "Open"}, {host_.glyph(Intent::Back), way}};
        std::vector<widgets::Hint> h = {{host_.glyph(Intent::Accept), "Play"}};
        if (resumeOk_) h.push_back({host_.glyph(Intent::Alt), "Resume state"});
        if (!embedded()) h.push_back({host_.glyph(Intent::Menu), "Details"});
        if (!embedded()) h.push_back({host_.glyph(Intent::Aux), "Jump to letter"});
        const std::string lt = host_.glyph(Intent::PagePrev), rt = host_.glyph(Intent::PageNext);
        h.push_back({lt.empty() && rt.empty() ? std::string() : lt + " " + rt, "A\xE2\x80\x93Z"});
        h.push_back({host_.glyph(Intent::Back), way});
        return h;
    }

private:
    // Fetches the selected list, applies the filter and settles the focus inside the result.
    void refresh() {
        std::vector<GameRef> all = host_.games(kKinds[list_].list);
        games_.clear();
        titles_.clear();
        const std::string needle = lower(filter_);
        for (GameRef& g : all) {
            if (!needle.empty() && lower(g.title).find(needle) == std::string::npos) continue;
            titles_.push_back(g.title);
            games_.push_back(std::move(g));
        }
        grid_.count = (int)games_.size();
        // Follow the game, not the position: launching a game moves it to the front of Recently played, so the index the
        // player left on is a different game when they come back.  A game that is gone falls back to the clamped index.
        if (!focusKey_.empty())
            for (int j = 0; j < (int)games_.size(); j++)
                if (games_[(size_t)j].key == focusKey_) {
                    grid_.index = j;
                    break;
                }
        grid_.index = std::clamp(grid_.index, 0, std::max(0, grid_.count - 1));
        if (!built_) {
            built_ = true;
            onRail_ = games_.empty();   // nothing to play yet: start on the rail
        }
        remember();
    }

    void remember() {
        shell_.memory().put("library/rail", list_);
        if (grid_.index >= 0 && grid_.index < (int)games_.size()) focusKey_ = games_[(size_t)grid_.index].key;   // (an empty list keeps the last game)
        if (filter_.empty()) {
            shell_.memory().put(std::string("library/") + kKinds[list_].id, grid_.index);
            shell_.memory().putItem(std::string("library/") + kKinds[list_].id, focusKey_);
        }
    }

    // Moving on the rail shows that list at once, at the place it was left.
    void switchList(int k) {
        if (k == list_) return;
        remember();
        list_ = k;
        rail_.index = k;
        filter_.clear();
        grid_.index = std::max(0, shell_.memory().get(std::string("library/") + kKinds[list_].id));
        focusKey_ = shell_.memory().getItem(std::string("library/") + kKinds[list_].id);
        grid_.firstRow = 0;
        snap_ = true;
        refresh();
    }

    void railIntent(Intent in) {
        switch (in) {
        case Intent::Up:
            if (rail_.move(-1)) switchList(rail_.index);
            break;
        case Intent::Down:
            if (rail_.move(1)) switchList(rail_.index);
            break;
        case Intent::Right:
        case Intent::Accept:
            if (!games_.empty()) onRail_ = false;   // to the grid, at the remembered index
            break;
        default: break;
        }
    }

    void gridIntent(Intent in) {
        switch (in) {
        case Intent::Left:
            if (grid_.move(-1, 0) == GridFocus::Left) onRail_ = true;
            break;
        case Intent::Right: grid_.move(1, 0); break;
        case Intent::Up: grid_.move(0, -1); break;
        case Intent::Down: grid_.move(0, 1); break;
        case Intent::Accept: play(); break;
        case Intent::Alt: resume(); break;
        case Intent::Aux:
            if (!embedded()) openLetters();   // (a screen pushed on the Base stack does not show under a game)
            break;
        case Intent::PagePrev: grid_.index = jumpInitial(titles_, grid_.index, -1); break;
        case Intent::PageNext: grid_.index = jumpInitial(titles_, grid_.index, 1); break;
        case Intent::Menu:
            if (!embedded()) details();
            break;
        default: break;
        }
    }

    void openLetters();   // below, once LettersScreen exists

    const GameRef* focused() const {
        return grid_.index >= 0 && grid_.index < (int)games_.size() ? &games_[(size_t)grid_.index] : nullptr;
    }

    void play() {
        const GameRef* g = focused();
        if (!g) return;
        shell_.requestLaunch(*g, nullptr);   // asks first when a game is running (the menu's slot)
    }

    // Start / a right-click: Game details of the focused game.  Back from there lands on this tile again.
    void details() {
        const GameRef* g = focused();
        if (!g) return;
        shell_.push(std::make_unique<DetailsScreen>(host_, shell_, *g));
    }

    // X: the newest state of the focused game, unless the game has none or hardcore refuses state loads.
    void resume() {
        const GameRef* g = focused();
        if (!g || !host_.canLoadState()) return;
        const std::vector<StateRef> st = host_.states(*g);
        if (st.empty()) return;
        const StateRef newest = st[0];
        shell_.requestLaunch(*g, &newest);
    }

    widgets::TileArgs tileArgs(const GameRef& g) {
        widgets::TileArgs a;
        a.title = g.title;
        a.disc = g.disc;
        const Art art = host_.art(g);
        a.tex = art.tex;
        a.texW = art.w;
        a.texH = art.h;
        return a;
    }

    Host& host_;
    Shell& shell_;
    std::vector<GameRef> games_;        // the selected list, filtered, from the last refresh()
    std::vector<std::string> titles_;   // ... and their titles (jumps, the picker)
    LinearFocus rail_;
    GridFocus grid_;
    int list_ = 0;                      // the selected list: an index into kKinds
    bool onRail_ = false;
    bool built_ = false;
    std::string focusKey_;              // the key of the focused game: refresh() follows it when the list reorders under the player
    std::string filter_;
    bool resumeOk_ = false;             // the focused game has a state and loading one is allowed (hints() reads it)
    Tween scroll_;                      // vertical scroll of the grid, in pixels
    bool scrollInit_ = false, snap_ = false;
    float lastU_ = 0;
    widgets::FocusTweens tweens_;
    ImVec2 lastMouse_{-FLT_MAX, -FLT_MAX};
    bool mouseSeen_ = false;
    float wheel_ = 0;                   // wheel notches not yet spent (a trackpad sends fractions)
};

// The letter picker: a modal 6 x 6 grid of A-Z and 0-9 over the Library.  A letter no title starts with is muted and
// does nothing.
class LettersScreen : public Screen {
public:
    LettersScreen(Host& h, Shell& s, LibraryScreen& owner) : host_(h), shell_(s), owner_(owner) {
        grid_.count = kLetterCount;
        grid_.cols = 6;
        const char* at = std::strchr(kLetters, owner.focusedInitial());
        grid_.index = at ? (int)(at - kLetters) : 0;
    }

    const char* id() const override { return "letters"; }
    bool modal() const override { return true; }

    void intent(Intent in) override {
        switch (in) {
        case Intent::Left: grid_.move(-1, 0); break;
        case Intent::Right: grid_.move(1, 0); break;
        case Intent::Up: grid_.move(0, -1); break;
        case Intent::Down: grid_.move(0, 1); break;
        case Intent::Accept: choose(); break;
        default: break;
        }
    }

    void draw(const DrawCtx& c) override {
        const Metrics& m = c.m;
        const theme::Palette& pal = theme::pal();
        const float u = m.u, cell = 96 * u, gap = 16 * u, pad = 36 * u;
        const float gridW = 6 * cell + 5 * gap, titleH = m.body + 20 * u;
        const ImVec2 size(gridW + 2 * pad, pad + titleH + gridW + pad);
        const ImVec2 pos(c.pos.x + (c.size.x - size.x) * 0.5f, c.pos.y + (c.size.y - size.y) * 0.5f);
        widgets::scrim(c.dl, c.pos, c.size, 0.6f);
        widgets::panel(c.dl, m, pos, size);
        const ImVec2 tsz = widgets::textSize(m.body, "Jump to letter");
        widgets::text(c.dl, m.body, ImVec2(pos.x + (size.x - tsz.x) * 0.5f, pos.y + pad), widgets::col(pal.text), "Jump to letter");

        if (!mouseSeen_) {
            lastMouse_ = c.mouse;
            mouseSeen_ = true;
        }
        const bool moved = c.mouse.x != lastMouse_.x || c.mouse.y != lastMouse_.y;
        lastMouse_ = c.mouse;
        int hover = -1;
        const std::vector<std::string>& titles = owner_.titles();
        tweens_.update(kLetterCount, grid_.index, 0, c.now);
        for (int i = 0; i < kLetterCount; i++) {
            const ImVec2 p0(pos.x + pad + (float)(i % 6) * (cell + gap), pos.y + pad + titleH + (float)(i / 6) * (cell + gap));
            const ImVec2 p1(p0.x + cell, p0.y + cell);
            const bool has = firstWithInitial(titles, kLetters[i]) >= 0;
            const float f = i == grid_.index ? tweens_.at(i, c.now) : 0.0f;
            c.dl->AddRectFilled(p0, p1, widgets::col(i == grid_.index ? pal.cardBgHover : pal.cardBg), 12 * u);
            if (f > 0.01f) c.dl->AddRect(p0, p1, widgets::col(pal.text, f), 12 * u, 0, m.ring * f);
            const std::string s(1, kLetters[i]);
            const float px = 44 * u;
            const ImVec2 ts = widgets::textSize(px, s);
            widgets::text(c.dl, px, ImVec2(p0.x + (cell - ts.x) * 0.5f, p0.y + (cell - ts.y) * 0.5f),
                          widgets::col(has ? pal.text : pal.textMuted), s);
            if (c.hovered && c.mouse.x >= p0.x && c.mouse.x < p1.x && c.mouse.y >= p0.y && c.mouse.y < p1.y) hover = i;
        }
        if (hover >= 0 && (moved || c.clicked)) grid_.index = hover;
        if (hover >= 0 && c.clicked) choose();
    }

    std::vector<widgets::Hint> hints() const override {
        return {{host_.glyph(Intent::Accept), "Jump"}, {host_.glyph(Intent::Back), "Back"}};
    }

private:
    void choose() {
        const int t = firstWithInitial(owner_.titles(), kLetters[grid_.index]);
        if (t < 0) return;   // a muted letter
        owner_.focusTitle(t);
        shell_.pop();
    }

    Host& host_;
    Shell& shell_;
    LibraryScreen& owner_;
    GridFocus grid_;
    widgets::FocusTweens tweens_;
    ImVec2 lastMouse_{-FLT_MAX, -FLT_MAX};
    bool mouseSeen_ = false;
};

void LibraryScreen::openLetters() {
    if (games_.empty()) return;
    shell_.push(std::make_unique<LettersScreen>(host_, shell_, *this));
}

}  // namespace

std::unique_ptr<Screen> makeLibraryScreen(Host& h, Shell& s) { return std::make_unique<LibraryScreen>(h, s); }

}  // namespace vj::tv
