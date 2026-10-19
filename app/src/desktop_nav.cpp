#include "desktop_nav.h"

#include <algorithm>
#include <utility>

namespace vj {

namespace {

const DeskRegion kRegions[4] = {DeskRegion::FavStrip, DeskRegion::RecentStrip, DeskRegion::Grid, DeskRegion::States};

const char* regionName(DeskRegion r) {
    switch (r) {
        case DeskRegion::FavStrip: return "fav";
        case DeskRegion::RecentStrip: return "recent";
        case DeskRegion::Grid: return "grid";
        case DeskRegion::States: return "states";
    }
    return "grid";
}

}  // namespace

float deskScrollToShow(float itemMin, float itemMax, float viewMin, float viewMax, float margin) {
    if (itemMin >= viewMin && itemMax <= viewMax) return 0.0f;
    if (itemMin < viewMin || itemMax - itemMin >= viewMax - viewMin) return itemMin - margin - viewMin;
    return itemMax + margin - viewMax;
}

int deskNearestVisibleIndex(int index, int cols, int count, float row0Top, float pitch, float tileH, float viewMin, float viewMax) {
    if (count <= 0 || cols < 1 || pitch <= 0.0f || index < 0 || index >= count) return index;
    const int rows = (count + cols - 1) / cols, col = index % cols;
    auto top = [&](int r) { return row0Top + (float)r * pitch; };
    auto full = [&](int r) { return top(r) >= viewMin && top(r) + tileH <= viewMax; };
    const int row = index / cols;
    if (full(row)) return index;
    if (top(row) < viewMin) {                       // left above: the first fully visible row
        for (int r = row + 1; r < rows; r++) {
            if (top(r) >= viewMax) break;
            if (full(r)) return std::min(r * cols + col, count - 1);
        }
    } else {                                        // left below: the last fully visible row
        for (int r = row - 1; r >= 0; r--) {
            if (top(r) + tileH <= viewMin) break;
            if (full(r)) return r * cols + col;     // rows above the last are full, so the column exists
        }
    }
    return index;
}

const std::vector<DeskItem>& DesktopNav::list(DeskRegion r) const {
    switch (r) {
        case DeskRegion::FavStrip: return lists_.fav;
        case DeskRegion::RecentStrip: return lists_.recent;
        case DeskRegion::Grid: return lists_.grid;
        case DeskRegion::States: return lists_.states;
    }
    return lists_.grid;
}

// The States tab shows its shots alone; on any other tab the shot list is not used.
bool DesktopNav::active(DeskRegion r) const {
    if (list(r).empty()) return false;
    return lists_.states.empty() ? r != DeskRegion::States : r == DeskRegion::States;
}

std::string DesktopNav::memKey(DeskRegion r) const { return tab_ + "/" + regionName(r); }

// Follow the remembered item by key; if it is gone, keep the remembered position clamped inside the list.
int DesktopNav::resolve(DeskRegion r) const {
    const std::vector<DeskItem>& v = list(r);
    int n = (int)v.size();
    if (n == 0) return 0;
    std::string k = memKey(r);
    std::string item = mem_.getItem(k);
    if (!item.empty()) {
        for (int i = 0; i < n; i++)
            if (v[i].key == item) return i;
    }
    return std::min(std::max(mem_.get(k, 0), 0), n - 1);
}

// An empty region is left out so its memory survives (a search that filters a list to nothing and back).
void DesktopNav::remember() {
    for (DeskRegion r : kRegions) {
        if (!active(r)) continue;
        int i = idx_[(int)r];
        mem_.put(memKey(r), i);
        mem_.putItem(memKey(r), list(r)[i].key);
    }
    mem_.put(tab_ + "/region", (int)region_);
}

// The focused region must have items: else the nearest region that does, the grid when two are equally near.
void DesktopNav::settle() {
    if (active(region_)) return;
    int cur = (int)region_;
    for (int d = 1; d < 4; d++) {
        int below = cur + d, above = cur - d;               // below = towards the grid
        if (below < 4 && active(kRegions[below])) { region_ = kRegions[below]; return; }
        if (above >= 0 && active(kRegions[above])) { region_ = kRegions[above]; return; }
    }
}

void DesktopNav::setLists(const std::string& tab, DeskLists lists) {
    bool tabChanged = !haveTab_ || tab != tab_;
    DeskRegion oldRegion = region_;
    int oldIndex = index();
    std::string oldKey = focusedKey();

    tab_ = tab;
    haveTab_ = true;
    lists_ = std::move(lists);
    if (lists_.gridCols < 1) lists_.gridCols = 1;
    for (DeskRegion r : kRegions) idx_[(int)r] = resolve(r);

    if (tabChanged) {
        int rem = mem_.get(tab_ + "/region", -1);
        region_ = rem >= 0 && rem < 4 ? kRegions[rem] : DeskRegion::Grid;   // a fresh tab starts on the grid
        if (!active(region_)) region_ = lists_.states.empty() ? DeskRegion::Grid : DeskRegion::States;
    }
    settle();
    remember();

    if (tabChanged || region_ != oldRegion || index() != oldIndex || focusedKey() != oldKey) scroll_ = true;
}

void DesktopNav::focusIndex(DeskRegion r, int index) {
    if (!active(r)) return;
    go(r, index);
    remember();
}

std::string DesktopNav::focusedKey() const {
    if (!active(region_)) return std::string();
    return list(region_)[idx_[(int)region_]].key;
}

bool DesktopNav::focused(DeskRegion r, int index) const {
    return r == region_ && active(r) && index == idx_[(int)r];
}

bool DesktopNav::takeScroll() {
    bool s = scroll_;
    scroll_ = false;
    return s;
}

bool DesktopNav::go(DeskRegion to, int index) {
    int n = (int)list(to).size();
    if (n == 0) return false;
    index = std::min(std::max(index, 0), n - 1);
    bool changed = to != region_ || index != idx_[(int)to];
    region_ = to;
    idx_[(int)to] = index;
    return changed;
}

void DesktopNav::move(tv::Intent in) {
    int dx = in == tv::Intent::Left ? -1 : in == tv::Intent::Right ? 1 : 0;
    int dy = in == tv::Intent::Up ? -1 : in == tv::Intent::Down ? 1 : 0;
    if (!active(region_)) return;
    int cur = (int)region_;
    bool changed = false;

    if (region_ == DeskRegion::States) {
        tv::LinearFocus f;
        f.count = (int)lists_.states.size();
        f.index = idx_[cur];
        changed = f.move(dx + dy);                          // Up/Left -1, Down/Right +1
        idx_[cur] = f.index;
    } else if (region_ == DeskRegion::Grid) {
        tv::GridFocus g;
        g.count = (int)lists_.grid.size();
        g.cols = lists_.gridCols;
        g.index = idx_[cur];
        tv::GridFocus::Edge e = g.move(dx, dy);
        if (e == tv::GridFocus::None) {
            changed = true;
            idx_[cur] = g.index;
        } else if (e == tv::GridFocus::Top) {               // off the first row: the strip above, where it was
            for (int r = cur - 1; r >= 0; r--)
                if (active(kRegions[r])) {
                    changed = go(kRegions[r], idx_[r]);
                    break;
                }
        }
    } else {                                                // a strip
        if (dx) {
            tv::LinearFocus f;
            f.count = (int)list(region_).size();
            f.index = idx_[cur];
            changed = f.move(dx);
            idx_[cur] = f.index;
        } else if (dy < 0) {
            for (int r = cur - 1; r >= 0; r--)
                if (active(kRegions[r])) {
                    changed = go(kRegions[r], idx_[r]);
                    break;
                }
        } else if (dy > 0) {
            for (int r = cur + 1; r < 3; r++)
                if (active(kRegions[r])) {
                    // Straight down: the grid column is min(strip index, cols - 1); a strip below takes the same
                    // position clamped to its length.
                    int i = idx_[cur];
                    if (kRegions[r] == DeskRegion::Grid) i = std::min(i, lists_.gridCols - 1);
                    changed = go(kRegions[r], i);
                    break;
                }
        }
    }
    if (changed) scroll_ = true;
}

void DesktopNav::jump(int dir) {
    if (region_ != DeskRegion::Grid || !active(region_)) return;
    std::vector<std::string> titles;
    titles.reserve(lists_.grid.size());
    for (const DeskItem& it : lists_.grid) titles.push_back(it.title);
    int from = idx_[(int)region_];
    int to = tv::jumpInitial(titles, from, dir);
    if (to != from) {
        idx_[(int)region_] = to;
        scroll_ = true;
    }
}

std::vector<DeskCmd> DesktopNav::apply(const std::vector<tv::Intent>& intents) {
    std::vector<DeskCmd> out;
    for (tv::Intent in : intents) {
        switch (in) {
            case tv::Intent::Up:
            case tv::Intent::Down:
            case tv::Intent::Left:
            case tv::Intent::Right:
                move(in);
                break;
            case tv::Intent::PagePrev: jump(-1); break;
            case tv::Intent::PageNext: jump(1); break;
            case tv::Intent::Accept:
                if (active(region_)) out.push_back({DeskCmd::Activate, focusedKey()});
                break;
            case tv::Intent::AcceptLong:
            case tv::Intent::Alt:
                if (active(region_)) out.push_back({DeskCmd::OpenMenu, focusedKey()});
                break;
            case tv::Intent::Aux:                           // a state shot cannot be a favorite
                if (active(region_) && region_ != DeskRegion::States)
                    out.push_back({DeskCmd::ToggleFavorite, focusedKey()});
                break;
            case tv::Intent::SectionPrev: out.push_back({DeskCmd::TabPrev, std::string()}); break;
            case tv::Intent::SectionNext: out.push_back({DeskCmd::TabNext, std::string()}); break;
            case tv::Intent::Search: out.push_back({DeskCmd::FocusSearch, std::string()}); break;
            case tv::Intent::Back: out.push_back({DeskCmd::Back, std::string()}); break;
            case tv::Intent::Menu:                          // unbound in the desktop library for now
            case tv::Intent::None:
                break;
        }
    }
    remember();
    return out;
}

}  // namespace vj
