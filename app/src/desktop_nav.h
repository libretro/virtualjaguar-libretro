// Controller navigation of the desktop library (#1021): the focus model.  Pure index arithmetic over the library's
// display lists, no ImGui and no SDL, so it is unit-testable.  The glue feeds it pad intents (tv::NavInput) and the
// lists the library is about to draw each frame, draws a ring on the focused item, scrolls to it, and executes the
// commands apply() returns.  See docs/app-pad-nav-design.md sections 3 and 6.
//
// Regions on a tab, top to bottom: the Favorites strip, the Recently played strip, the grid (empty ones are skipped);
// or, on the States tab, the shot list alone.  Focus never wraps.  Where focus was in each region of each tab is
// remembered (tv::FocusMemory, keyed `tab + "/" + region`) by item key as well as by position, so a list that
// reorders keeps the same game focused and coming back to a tab lands where it was left.
#pragma once

#include <string>
#include <vector>

#include "tv/tv_focus.h"
#include "tv/tv_nav.h"

namespace vj {

// key identifies the entry (its path; a state shot is `path + "#" + slot`); title feeds the A-Z jump.
struct DeskItem {
    std::string key, title;
};

enum class DeskRegion { FavStrip, RecentStrip, Grid, States };

// What the current tab shows, in display order.  A tab without a region leaves its list empty.  When `states` is
// not empty it is the only region (the States tab); the other lists are then ignored.
struct DeskLists {
    std::vector<DeskItem> fav, recent, grid, states;
    int gridCols = 1;   // the grid's column count this frame
};

// What the UI should do; `key` is the focused item's key where the command needs one.
struct DeskCmd {
    enum Kind { Activate, OpenMenu, ToggleFavorite, TabPrev, TabNext, FocusSearch, Back } kind;
    std::string key;
};

// How far to scroll (in the units given) so [itemMin, itemMax] sits fully inside [viewMin, viewMax]: 0 when it already
// does, else the least that brings it in plus `margin`; an item taller than the view lines its top up.  Pure.
float deskScrollToShow(float itemMin, float itemMax, float viewMin, float viewMax, float margin);

// The right stick scrolled the grid and the focused tile may have left the view: the index to focus instead.  Tiles are laid
// out `cols` to a row, rows `pitch` apart from `row0Top`, each `tileH` tall; [viewMin, viewMax] is the visible range (same
// units).  A fully visible focused tile keeps focus.  One that left above takes the first fully visible tile of its column,
// one that left below the last; a column the last row lacks steps back a row.  `index` when no row is fully visible.  Pure.
int deskNearestVisibleIndex(int index, int cols, int count, float row0Top, float pitch, float tileH, float viewMin, float viewMax);

class DesktopNav {
public:
    // Offer this frame's lists for `tab`.  The focused item is followed by key when the list changes; if it is gone
    // the index is clamped inside the same region; if the region is empty focus moves to the nearest non-empty one
    // (the grid on a tie).  A different `tab` than last time lands on that tab's remembered focus.
    void setLists(const std::string& tab, DeskLists lists);   // by value: the caller moves its freshly built lists in

    // Run the intents in order (moves change the focus, the rest become commands).  Back is always reported; the UI
    // decides whether it closes a popup, the window, or nothing.  Menu and None produce nothing.
    std::vector<DeskCmd> apply(const std::vector<tv::Intent>& intents);

    DeskRegion region() const { return region_; }
    int index() const { return idx_[(int)region_]; }
    std::string focusedKey() const;                       // "" when no region has an item
    bool focused(DeskRegion r, int index) const;          // is this item the focused one (for drawing the ring)
    bool takeScroll();                                    // true once after any focus change: scroll the item into view

    // Focus (r, index clamped) without asking for a scroll: the view is already where the player put it (right stick).
    // Nothing when r has no items.
    void focusIndex(DeskRegion r, int index);

    // A plain flag the glue sets (pad input shows the ring, mouse movement hides it).  The model ignores it.
    void setVisible(bool v) { visible_ = v; }
    bool visible() const { return visible_; }

    // Forget transient state (a pending scroll).  Focus memory is kept on purpose: returning to a tab, or to the
    // library after a popup or a game, restores where focus was.
    void reset() { scroll_ = false; }

private:
    const std::vector<DeskItem>& list(DeskRegion r) const;
    bool active(DeskRegion r) const;                      // has items and is shown (States alone when it has any)
    std::string memKey(DeskRegion r) const;
    int resolve(DeskRegion r) const;                      // the remembered position of r in the current list
    void remember();                                      // write the current focus of every active region to memory
    void settle();                                        // region_ must be active: else the nearest one that is
    bool go(DeskRegion to, int index);                    // focus (to, index clamped); true if the focus changed
    void move(tv::Intent in);                             // a direction
    void jump(int dir);                                   // PagePrev / PageNext in the grid

    std::string tab_;
    bool haveTab_ = false;
    DeskLists lists_;
    DeskRegion region_ = DeskRegion::Grid;
    int idx_[4] = {0, 0, 0, 0};
    tv::FocusMemory mem_;
    bool scroll_ = false;
    bool visible_ = false;
};

}  // namespace vj
