// TV mode (#991): Game details -- the art and title of one game over a short list: Play, a Resume row for each save
// state (newest first, with the state's picture), the Favorite toggle, and -- when RetroAchievements has a set the library
// can run -- Achievements, which opens that section with the game's row focused.  Pushed by Start or a right-click on a tile of
// Home or the Library; geometry is the spec's Game details sketch in design units (u = viewport height / 1080).
//
// The rows are fetched again on every draw and every intent, keyed by the GameRef's `key` (a file path), so a rescan or
// a new save state shows at once and the screen never holds anything that can die.  A game a rescan dropped leaves only
// the title and Back, and A does nothing.  Launching leaves this screen on the stack, so closing the game lands here.
#include <algorithm>
#include <cstdlib>

#include "theme.h"
#include "paths.h"
#include "tv/tv_screen.h"
#include "tv/tv_shell.h"

namespace vj::tv {

namespace {

constexpr float kArtW = 266, kArtH = 372, kArtGap = 56;   // design units
constexpr float kRowsTop = 56 + 28 + 40;                  // from the top of the content area: title, kind line, a gap
constexpr float kRowsW = 900, kRowH = 72, kStateRowH = 112, kRowGap = 12;
constexpr float kRowsMargin = 12;                         // room above the first row for a focused row's ring and shadow

bool inList(const std::vector<GameRef>& v, const std::string& key) {
    for (const GameRef& g : v)
        if (g.key == key) return true;
    return false;
}

}  // namespace

// Still in the library?  Every list is a subset of All, but the smaller ones are asked first (a long All is a long fetch).
static bool gameExists(Host& h, const std::string& key) {
    return inList(h.games(List::Recent), key) || inList(h.games(List::Favorites), key) || inList(h.games(List::All), key) ||
           inList(h.games(List::Disc), key);
}

void DetailsScreen::refresh() {
    items_.clear();
    achId_ = 0;
    if (gameExists(host_, g_.key)) {   // a game a rescan dropped has no rows
        items_.push_back({Item::Play, "Play", "", -1, 0});
        if (host_.canLoadState())   // hardcore refuses state loads: no Resume rows at all
            for (const StateRef& s : host_.states(g_))
                items_.push_back({Item::Resume, s.slot < 0 ? std::string("Resume: Auto-save") : "Resume: Slot " + std::to_string(s.slot),
                                  (s.disc.empty() ? "" : pathStem(s.disc) + " - ") + widgets::formatDate(s.time), s.slot, s.time, s.disc});
        items_.push_back({Item::Favorite, host_.isFavorite(g_) ? "Favorite: On" : "Favorite: Off", "", -1, 0});
        achId_ = host_.achGameId(g_);
        if (achId_ != 0) items_.push_back({Item::Achievements, "Achievements", "", -1, 0});
    }
    focus_.clampTo((int)items_.size());
}

void DetailsScreen::intent(Intent in) {
    refresh();
    switch (in) {
    case Intent::Up: focus_.move(-1); break;
    case Intent::Down: focus_.move(1); break;
    case Intent::Accept: accept(); break;
    default: break;
    }
}

void DetailsScreen::accept() {
    if (focus_.index < 0 || focus_.index >= (int)items_.size()) return;
    const Item it = items_[(size_t)focus_.index];
    switch (it.kind) {
    case Item::Play:
        host_.launch(g_);
        shell_.launched();
        break;
    case Item::Resume:
        host_.launchState(g_, {it.slot, it.time, it.disc});
        shell_.launched();
        break;
    case Item::Favorite: host_.setFavorite(g_, !host_.isFavorite(g_)); break;
    case Item::Achievements:   // replaces the stack, this screen with it: nothing of ours is touched after the call
        shell_.openSection(Section::Achievements, makeAchievementsScreen(host_, shell_, achId_));
        break;
    }
}

std::vector<widgets::Hint> DetailsScreen::hints() const {
    std::vector<widgets::Hint> h;
    if (!items_.empty()) h.push_back({host_.glyph(Intent::Accept), "Select"});
    h.push_back({host_.glyph(Intent::Back), "Back"});
    return h;
}

void DetailsScreen::draw(const DrawCtx& c) {
    refresh();
    const Metrics& m = c.m;
    const theme::Palette& pal = theme::pal();
    const float u = m.u;
    const float x0 = c.pos.x + m.safeX, right = c.pos.x + c.size.x - m.safeX;

    // ---- the game: art, title, kind --------------------------------------------------------------------------------
    {
        widgets::TileArgs a;
        a.title = g_.title;
        a.disc = g_.disc;
        const Art art = host_.art(g_);
        a.tex = art.tex;
        a.texW = art.w;
        a.texH = art.h;
        widgets::tile(c.dl, m, ImVec2(x0, c.pos.y), ImVec2(kArtW * u, kArtH * u), a);
    }
    const float tx = x0 + (kArtW + kArtGap) * u, colW = right - tx;
    widgets::text(c.dl, m.titleSmall, ImVec2(tx, c.pos.y), widgets::col(pal.text), g_.title, colW);
    const std::string kind = g_.disc ? "Jaguar CD" : "Cartridge";
    widgets::text(c.dl, 28 * u, ImVec2(tx, c.pos.y + m.titleSmall), widgets::col(pal.textSecondary),
                  g_.region.empty() ? kind : g_.region + " \xC2\xB7 " + kind, colW);
    if (items_.empty()) return;

    // ---- the rows: scrolled so the focused one shows ----------------------------------------------------------------
    const float rowsTop = c.pos.y + kRowsTop * u, viewBottom = c.pos.y + c.size.y, viewH = viewBottom - rowsTop;
    const float w = std::min(kRowsW * u, colW);
    std::vector<float> tops(items_.size()), heights(items_.size());
    float total = 0;
    for (size_t i = 0; i < items_.size(); i++) {
        heights[i] = (items_[i].kind == Item::Resume ? kStateRowH : kRowH) * u;
        tops[i] = total;
        total += heights[i] + kRowGap * u;
    }
    total -= kRowGap * u;
    float target = scrollTo_;
    const size_t f = (size_t)focus_.index;
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
    tweens_.update((int)items_.size(), focus_.index, 0, c.now);

    if (!mouseSeen_) {   // a pointer already resting on a row when the screen appears has not chosen it
        lastMouse_ = c.mouse;
        mouseSeen_ = true;
    }
    const bool moved = c.mouse.x != lastMouse_.x || c.mouse.y != lastMouse_.y;
    lastMouse_ = c.mouse;

    int hover = -1;
    c.dl->PushClipRect(ImVec2(c.pos.x, rowsTop - kRowsMargin * u), ImVec2(c.pos.x + c.size.x, viewBottom), true);
    for (size_t i = 0; i < items_.size(); i++) {
        const float y = rowsTop + tops[i] - scroll;
        if (y + heights[i] < rowsTop - kRowsMargin * u || y > viewBottom) continue;
        const Item& it = items_[i];
        widgets::RowArgs a;
        a.label = it.label;
        a.sub = it.sub;
        a.focus = (int)i == focus_.index ? tweens_.at((int)i, c.now) : 0.0f;
        if (it.kind == Item::Resume) {
            a.thumbBox = true;
            const Art t = host_.stateThumb(g_, {it.slot, it.time, it.disc});
            a.thumb = t.tex;
            a.thumbSrcW = t.w;
            a.thumbSrcH = t.h;
        }
        const widgets::Hit hit = widgets::row(c.dl, m, ImVec2(tx, y), ImVec2(w, heights[i]), a);
        if (hit.hovered && c.mouse.y >= rowsTop && c.mouse.y < viewBottom) hover = (int)i;   // not the part the clip hides
    }
    c.dl->PopClipRect();
    if (scroll > 0.5f) widgets::moreArrow(c.dl, m, ImVec2(tx + w * 0.5f, rowsTop - 4 * u), true);
    if (scroll + viewH < total - 0.5f) widgets::moreArrow(c.dl, m, ImVec2(tx + w * 0.5f, viewBottom - 10 * u), false);

    // ---- the pointer: hovering a row focuses it, a click selects it, a notch is a row ------------------------------
    if (hover >= 0 && (moved || c.clicked)) focus_.index = hover;
    wheel_ += c.wheel;
    const int steps = (int)wheel_;
    wheel_ -= (float)steps;
    for (int s = 0; s < std::abs(steps); s++) focus_.move(steps > 0 ? -1 : 1);   // wheel up = toward the first row
    if (hover >= 0 && c.clicked) accept();
}

}  // namespace vj::tv
