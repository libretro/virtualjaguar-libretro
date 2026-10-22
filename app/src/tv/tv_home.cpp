// TV mode (#991): the Home screen -- a hero panel for the focused game over Steam-style shelves (Continue playing,
// Favorites, Jaguar CD).  Geometry is the spec's Home mockup in design units (u = viewport height / 1080).
//
// The lists are fetched by value once per draw (a GameRef is keyed by file path, so a rescan between two frames cannot
// leave the screen holding a dead entry) and focus is clamped to them; what each shelf remembered lives in
// Shell::memory() so coming back to Home lands where the player was.
#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cstdlib>

#include "theme.h"
#include "tv/tv_screen.h"
#include "tv/tv_shell.h"

namespace vj::tv {

namespace {

constexpr int kShelfKinds = 3;                       // recent, favorites, disc: also the order they are shown in
constexpr float kShelfPitch = 386, kTileW = 210, kTileH = 294, kTileGap = 32;   // design units
constexpr float kHeroArtW = 266, kHeroArtH = 372;
constexpr float kShelvesTop = 570 - 150;             // from the top of the content area (y = 150)

struct ShelfKind {
    const char* id;
    const char* name;
    List list;
};
const ShelfKind kKinds[kShelfKinds] = {
    {"recent", "Continue playing", List::Recent},
    {"favorites", "Favorites", List::Favorites},
    {"disc", "Jaguar CD", List::Disc},
};

std::string upper(std::string s) {
    for (char& c : s) c = (char)std::toupper((unsigned char)c);
    return s;
}

class HomeScreen : public Screen {
public:
    HomeScreen(Host& h, Shell& s) : host_(h), shell_(s) {
        want_ = std::clamp(s.memory().get("home/shelf"), 0, kShelfKinds - 1);
        for (int k = 0; k < kShelfKinds; k++) tile_[k].index = std::max(0, s.memory().get(std::string("home/") + kKinds[k].id));
    }

    const char* id() const override { return "home"; }

    void intent(Intent in) override {
        if (in == Intent::Aux) {   // Y: the Achievements section, tile or no tile
            shell_.setSection(Section::Achievements);
            return;
        }
        if (!built_) refresh();
        if (shelves_.empty()) return;
        LinearFocus& f = tile_[shelves_[(size_t)cur_].kind];
        switch (in) {
        case Intent::Left: f.move(-1); break;
        case Intent::Right: f.move(1); break;
        case Intent::Up: moveShelf(-1); break;
        case Intent::Down: moveShelf(1); break;
        case Intent::Accept: play(); break;
        case Intent::Alt: resume(); break;
        case Intent::Menu: details(); break;
        default: break;
        }
        remember();
    }

    void draw(const DrawCtx& c) override {
        refresh();
        const Metrics& m = c.m;
        const theme::Palette& pal = theme::pal();
        const float u = m.u;
        const float x0 = c.pos.x + m.safeX, right = c.pos.x + c.size.x;
        if (shelves_.empty()) {
            drawEmpty(c);
            return;
        }

        // ---- hero: the focused game ----------------------------------------------------------------------------------
        const Shelf& cur = shelves_[(size_t)cur_];
        const GameRef g = cur.games[(size_t)tile_[cur.kind].index];
        const std::vector<StateRef> st = host_.states(g);
        resumeOk_ = !st.empty() && host_.canLoadState();
        {
            widgets::TileArgs a = tileArgs(g);
            widgets::tile(c.dl, m, ImVec2(x0, c.pos.y), ImVec2(kHeroArtW * u, kHeroArtH * u), a);
            const float tx = x0 + (kHeroArtW + 56) * u, colW = right - m.safeX - tx;
            const float bottom = c.pos.y + kHeroArtH * u;
            const float yButtons = bottom - 36 * u, yStates = yButtons - 20 * u - 28 * u, yKind = yStates - 8 * u - 28 * u;
            const float yTitle = yKind - 8 * u - m.titleLarge, yName = yTitle - 6 * u - m.caption;
            widgets::text(c.dl, m.caption, ImVec2(tx, yName), widgets::col(pal.textMuted), upper(kKinds[cur.kind].name), colW);
            widgets::text(c.dl, m.titleLarge, ImVec2(tx, yTitle), widgets::col(pal.text), g.title, colW);
            const std::string kind = g.disc ? "Jaguar CD" : "Cartridge";
            widgets::text(c.dl, 28 * u, ImVec2(tx, yKind), widgets::col(pal.textSecondary),
                          g.region.empty() ? kind : g.region + " \xC2\xB7 " + kind, colW);
            std::string states = "No save states";
            if (!st.empty())
                states = std::to_string(st.size()) + (st.size() == 1 ? " save state" : " save states") + " \xC2\xB7 newest " +
                         widgets::formatDate(st[0].time);
            widgets::text(c.dl, 28 * u, ImVec2(tx, yStates), widgets::col(pal.textSecondary), states, colW);
            std::vector<widgets::Hint> buttons = {{host_.glyph(Intent::Accept), "Play"}};
            if (resumeOk_) buttons.push_back({host_.glyph(Intent::Alt), "Resume last state"});
            buttons.push_back({host_.glyph(Intent::Menu), "Details"});
            widgets::dropUnbound(buttons);
            widgets::hintBar(c.dl, m, ImVec2(tx, yButtons), colW, buttons, 28 * u);
        }

        // ---- the shelves: the focused one in the first slot, the next one showing in part --------------------------
        const float top = c.pos.y + kShelvesTop * u, bottom = c.pos.y + c.size.y;
        const float target = (float)cur_ * kShelfPitch * u;
        if (!scrollInit_ || lastU_ != u) {
            scroll_.from = scroll_.to = target;
            scroll_.start = c.now;
            scrollInit_ = true;
            lastU_ = u;
        } else if (target != scroll_.to) {
            scroll_.retarget(target, c.now);
        }
        const float scroll = scroll_.at(c.now);
        const float tile0 = x0 + 12 * u, pitch = (kTileW + kTileGap) * u;
        const int fit = std::max(1, (int)((right - tile0 + kTileGap * u) / pitch));   // tiles that fit before the screen's edge
        tweens_.update((int)cur.games.size(), tile_[cur.kind].index, cur.kind, c.now);

        if (!mouseSeen_) {   // a pointer already resting on a tile when the screen appears has not chosen it
            lastMouse_ = c.mouse;
            mouseSeen_ = true;
        }
        const bool moved = c.mouse.x != lastMouse_.x || c.mouse.y != lastMouse_.y;
        lastMouse_ = c.mouse;
        int hoverShelf = -1, hoverTile = -1;
        c.dl->PushClipRect(ImVec2(c.pos.x, top), ImVec2(right, bottom), true);
        for (int i = 0; i < (int)shelves_.size(); i++) {
            const Shelf& sh = shelves_[(size_t)i];
            const float y = top + (float)i * kShelfPitch * u - scroll;
            if (y + kShelfPitch * u <= top || y >= bottom) continue;
            // A shelf is shown only when its whole title fits above the hint bar: a title half under the bar is noise.
            if (y + widgets::textSize(m.body, kKinds[sh.kind].name).y > bottom) continue;
            LinearFocus& f = tile_[sh.kind];
            f.first = std::min(f.first, std::max(0, f.count - fit));
            f.scrollToShow(fit);
            widgets::text(c.dl, m.body, ImVec2(x0, y), widgets::col(i == cur_ ? pal.text : pal.textSecondary), kKinds[sh.kind].name);
            const float ty = y + (30 + 22) * u;
            for (int j = f.first; j < (int)sh.games.size() && j < f.first + fit; j++) {
                widgets::TileArgs a = tileArgs(sh.games[(size_t)j]);
                a.focus = i == cur_ && j == f.index ? tweens_.at(j, c.now) : 0.0f;
                const widgets::Hit hit = widgets::tile(c.dl, m, ImVec2(tile0 + (float)(j - f.first) * pitch, ty),
                                                      ImVec2(kTileW * u, kTileH * u), a);
                if (hit.hovered && c.mouse.y >= top && c.mouse.y < bottom) {   // not the part of a tile the clip hides
                    hoverShelf = i;
                    hoverTile = j;
                }
            }
        }
        c.dl->PopClipRect();

        // ---- the pointer: hovering a tile focuses it, a click plays it, a right-click is Menu, the wheel steps shelves
        if (hoverShelf >= 0 && (moved || c.clicked || c.rightClicked)) {
            cur_ = hoverShelf;
            want_ = shelves_[(size_t)cur_].kind;
            tile_[want_].index = hoverTile;
        }
        wheel_ += c.wheel;
        const int steps = (int)wheel_;
        wheel_ -= (float)steps;
        for (int s = 0; s < std::abs(steps); s++) moveShelf(steps > 0 ? -1 : 1);   // wheel up = toward the first shelf
        remember();
        if (hoverShelf >= 0 && c.clicked) intent(Intent::Accept);
        else if (hoverShelf >= 0 && c.rightClicked) intent(Intent::Menu);
    }

    std::vector<widgets::Hint> hints() const override {
        std::vector<widgets::Hint> h;   // Back on Home does nothing: no way out to hint
        if (!shelves_.empty()) {
            h.push_back({host_.glyph(Intent::Accept), "Play"});
            if (resumeOk_) h.push_back({host_.glyph(Intent::Alt), "Resume state"});
            h.push_back({host_.glyph(Intent::Menu), "Details"});
        }
        h.push_back({host_.glyph(Intent::Aux), "Achievements"});
        return h;
    }

private:
    struct Shelf {
        int kind;
        std::vector<GameRef> games;   // never empty: an empty shelf is not here
    };

    // Fetches the three lists and settles the focus inside them.
    void refresh() {
        built_ = true;
        shelves_.clear();
        for (int k = 0; k < kShelfKinds; k++) {
            Shelf s{k, host_.games(kKinds[k].list)};
            if (s.games.empty()) continue;   // its remembered index waits for it to come back
            // Follow the game, not the position: launching a game moves it to the front of Recent, so the index the
            // player left on is a different game when they come back.  A game that is gone falls back to the index.
            const std::string want = shell_.memory().getItem(std::string("home/") + kKinds[k].id);
            if (!want.empty())
                for (int j = 0; j < (int)s.games.size(); j++)
                    if (s.games[(size_t)j].key == want) {
                        tile_[k].index = j;
                        break;
                    }
            tile_[k].clampTo((int)s.games.size());
            shelves_.push_back(std::move(s));
        }
        cur_ = -1;
        for (int i = 0; i < (int)shelves_.size() && cur_ < 0; i++)
            if (shelves_[(size_t)i].kind >= want_) cur_ = i;
        if (cur_ < 0) cur_ = (int)shelves_.size() - 1;   // -1 when there are none
        if (cur_ >= 0) want_ = shelves_[(size_t)cur_].kind;
        remember();
    }

    void remember() {
        shell_.memory().put("home/shelf", want_);
        for (int k = 0; k < kShelfKinds; k++) shell_.memory().put(std::string("home/") + kKinds[k].id, tile_[k].index);
        for (const Shelf& s : shelves_) {   // (a shelf that is empty right now keeps the game it last had)
            const int i = tile_[s.kind].index;
            if (i >= 0 && i < (int)s.games.size())
                shell_.memory().putItem(std::string("home/") + kKinds[s.kind].id, s.games[(size_t)i].key);
        }
    }

    void moveShelf(int d) {
        const int n = cur_ + d;
        if (n < 0 || n >= (int)shelves_.size()) return;
        cur_ = n;
        want_ = shelves_[(size_t)cur_].kind;
    }

    const GameRef* focused() const {
        if (cur_ < 0 || cur_ >= (int)shelves_.size()) return nullptr;
        const Shelf& s = shelves_[(size_t)cur_];
        const int i = tile_[s.kind].index;
        return i >= 0 && i < (int)s.games.size() ? &s.games[(size_t)i] : nullptr;
    }

    void play() {
        const GameRef* g = focused();
        if (!g) return;
        host_.launch(*g);
        shell_.launched();
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
        host_.launchState(*g, st[0]);
        shell_.launched();
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

    void drawEmpty(const DrawCtx& c) {
        const Metrics& m = c.m;
        const float u = m.u, w = std::min(1100 * u, c.size.x - 2 * m.safeX), pad = 48 * u;
        // Games but no shelves (a new player: nothing played, no favorites, no discs) is not an empty library: the Library
        // section lists them.  Otherwise the first scan of a new install (or of an empty cache) may still be running.
        std::string msg;
        if (host_.hasGames()) {
            std::string next = host_.glyph(Intent::SectionNext);   // the button the section bar shows
            if (next.empty()) next = "RB";
            msg = "Nothing to continue yet. Press " + next + " for your Library.";
        } else {
            msg = host_.libraryScanning()
                      ? "Scanning your library..."
                      : "Your library is empty. Add game folders in desktop mode: View > TV mode turns TV mode off.";
        }
        const ImVec2 ts = widgets::textSize(m.body, msg, w - 2 * pad);
        const ImVec2 size(w, ts.y + 2 * pad);
        const ImVec2 pos(c.pos.x + (c.size.x - size.x) * 0.5f, c.pos.y + (c.size.y - size.y) * 0.5f);
        widgets::panel(c.dl, m, pos, size);
        widgets::text(c.dl, m.body, ImVec2(pos.x + pad, pos.y + pad), widgets::col(theme::pal().text), msg, 0, w - 2 * pad);
    }

    Host& host_;
    Shell& shell_;
    std::vector<Shelf> shelves_;   // the visible shelves, in order, from the last refresh()
    LinearFocus tile_[kShelfKinds];   // by kind: the focus (and scroll) each shelf keeps
    int want_ = 0;                 // the kind of the focused shelf; cur_ is its place among shelves_
    int cur_ = -1;
    bool built_ = false;
    bool resumeOk_ = false;        // the focused game has a state and loading one is allowed (hints() reads it)
    Tween scroll_;                 // vertical scroll of the shelves, in pixels
    bool scrollInit_ = false;
    float lastU_ = 0;
    widgets::FocusTweens tweens_;
    ImVec2 lastMouse_{-FLT_MAX, -FLT_MAX};
    bool mouseSeen_ = false;
    float wheel_ = 0;              // wheel notches not yet spent (a trackpad sends fractions)
};

}  // namespace

std::unique_ptr<Screen> makeHomeScreen(Host& h, Shell& s) { return std::make_unique<HomeScreen>(h, s); }

}  // namespace vj::tv
