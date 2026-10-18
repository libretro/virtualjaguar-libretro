// Controller navigation of the desktop library (#1021): the DesktopNav focus model.
//
//   vj_desktop_nav_test
//
// Pure logic: no window, no core, no clock.
#include "desktop_nav.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace vj;
using tv::Intent;

static int g_fail = 0, g_checks = 0;

#define CHECK(c)                                                              \
    do {                                                                      \
        g_checks++;                                                           \
        if (!(c)) {                                                           \
            g_fail++;                                                         \
            std::fprintf(stderr, "FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); \
        }                                                                     \
    } while (0)

static std::vector<DeskItem> items(const char* names) {   // "a b c" -> keys and titles "a", "b", "c"
    std::vector<DeskItem> v;
    std::string cur;
    for (const char* p = names;; p++) {
        if (*p == ' ' || *p == 0) {
            if (!cur.empty()) v.push_back({cur, cur});
            cur.clear();
            if (!*p) break;
        } else {
            cur += *p;
        }
    }
    return v;
}

static DeskLists gridOnly(const char* names, int cols) {
    DeskLists l;
    l.grid = items(names);
    l.gridCols = cols;
    return l;
}

static std::vector<DeskCmd> press(DesktopNav& n, std::initializer_list<Intent> in) {
    return n.apply(std::vector<Intent>(in));
}

static void grid_moves_and_edges() {
    DesktopNav n;
    n.setLists("all", gridOnly("a b c d e f g", 3));   // rows: abc / def / g
    CHECK(n.region() == DeskRegion::Grid && n.index() == 0 && n.focusedKey() == "a");
    press(n, {Intent::Left, Intent::Up});              // edges: nothing moves
    CHECK(n.index() == 0);
    press(n, {Intent::Right, Intent::Right});
    CHECK(n.index() == 2);
    press(n, {Intent::Right});                         // right edge: no wrap to the next row
    CHECK(n.index() == 2);
    press(n, {Intent::Down});
    CHECK(n.index() == 5 && n.focusedKey() == "f");
    press(n, {Intent::Down});                          // short last row: lands on its last item
    CHECK(n.index() == 6 && n.focusedKey() == "g");
    press(n, {Intent::Down});                          // bottom edge
    CHECK(n.index() == 6);
    press(n, {Intent::Up, Intent::Left});              // g is in column 0: Up lands on d; Left is the edge
    CHECK(n.index() == 3 && n.focusedKey() == "d");
}

static void strip_to_grid_and_back() {
    DesktopNav n;
    DeskLists l;
    l.fav = items("f0 f1 f2 f3 f4");
    l.recent = items("r0 r1");
    l.grid = items("g0 g1 g2 g3 g4 g5 g6 g7");
    l.gridCols = 4;
    n.setLists("all", l);
    CHECK(n.region() == DeskRegion::Grid);             // a fresh tab lands on the grid
    press(n, {Intent::Right, Intent::Right});          // g2
    press(n, {Intent::Up});                            // row 0: up to the recent strip at its remembered index (0)
    CHECK(n.region() == DeskRegion::RecentStrip && n.index() == 0);
    press(n, {Intent::Right});
    CHECK(n.index() == 1 && n.focusedKey() == "r1");
    press(n, {Intent::Right});                         // strip end: stays
    CHECK(n.index() == 1);
    press(n, {Intent::Up});                            // up to the fav strip at its remembered index (0)
    CHECK(n.region() == DeskRegion::FavStrip && n.index() == 0);
    press(n, {Intent::Right, Intent::Right, Intent::Right, Intent::Right});
    CHECK(n.index() == 4);
    press(n, {Intent::Up});                            // the top region: nothing
    CHECK(n.region() == DeskRegion::FavStrip && n.index() == 4);
    press(n, {Intent::Down});                          // next strip, same column clamped to its length
    CHECK(n.region() == DeskRegion::RecentStrip && n.index() == 1);
    press(n, {Intent::Down});                          // grid index = min(strip index, cols - 1)
    CHECK(n.region() == DeskRegion::Grid && n.index() == 1 && n.focusedKey() == "g1");
    press(n, {Intent::Up, Intent::Up, Intent::Right});  // grid -> recent(1) -> fav(4)... right at the end stays
    CHECK(n.region() == DeskRegion::FavStrip && n.index() == 4);
    // From the fav strip straight to the grid: min(4, cols - 1) = 3.
    DeskLists l2;
    l2.fav = items("f0 f1 f2 f3 f4");
    l2.grid = items("g0 g1 g2 g3 g4 g5 g6 g7");
    l2.gridCols = 4;
    DesktopNav m;
    m.setLists("t", l2);
    press(m, {Intent::Up});
    CHECK(m.region() == DeskRegion::FavStrip);         // recent is empty: skipped
    press(m, {Intent::Right, Intent::Right, Intent::Right, Intent::Right, Intent::Down});
    CHECK(m.region() == DeskRegion::Grid && m.index() == 3);
    // A grid that is shorter than the column index clamps to its last item.
    DeskLists l3;
    l3.fav = items("f0 f1 f2");
    l3.grid = items("g0 g1");
    l3.gridCols = 6;
    DesktopNav k;
    k.setLists("t", l3);
    press(k, {Intent::Up, Intent::Right, Intent::Right, Intent::Down});
    CHECK(k.region() == DeskRegion::Grid && k.index() == 1);
}

static void states_linear() {
    DesktopNav n;
    DeskLists l;
    l.states = items("s0 s1 s2");
    l.gridCols = 3;
    n.setLists("states", l);
    CHECK(n.region() == DeskRegion::States && n.index() == 0);
    press(n, {Intent::Down});
    CHECK(n.index() == 1);
    press(n, {Intent::Right});
    CHECK(n.index() == 2);
    press(n, {Intent::Down, Intent::Right});           // end: stays
    CHECK(n.index() == 2);
    press(n, {Intent::Up});
    CHECK(n.index() == 1);
    press(n, {Intent::Left, Intent::Left, Intent::Up});
    CHECK(n.index() == 0);
    // Favorite is not offered on a state shot.
    press(n, {Intent::Down});
    std::vector<DeskCmd> c = press(n, {Intent::Aux, Intent::Accept});
    CHECK(c.size() == 1 && c[0].kind == DeskCmd::Activate && c[0].key == "s1");
}

static void page_initials() {
    DesktopNav n;
    DeskLists l;
    l.grid = {{"1", "Alpha"}, {"2", "Aztec"}, {"3", "Bravo"}, {"4", "Blitz"}, {"5", "Cat"}, {"6", "Dog"}};
    l.gridCols = 2;
    n.setLists("all", l);
    press(n, {Intent::PageNext});
    CHECK(n.index() == 2 && n.focusedKey() == "3");    // Bravo
    press(n, {Intent::PageNext});
    CHECK(n.index() == 4);
    press(n, {Intent::PageNext, Intent::PageNext});
    CHECK(n.index() == 5);                             // Dog is the last initial: stays
    press(n, {Intent::PagePrev});
    CHECK(n.index() == 4);
    press(n, {Intent::PagePrev, Intent::PagePrev});
    CHECK(n.index() == 0);
    press(n, {Intent::PagePrev});
    CHECK(n.index() == 0);
    // Page jumps do nothing in a strip.
    DesktopNav s;
    DeskLists l2;
    l2.fav = items("a b c");
    s.setLists("all", l2);
    CHECK(s.region() == DeskRegion::FavStrip);         // the only non-empty region
    press(s, {Intent::PageNext});
    CHECK(s.index() == 0);
}

static void commands_from_intents() {
    DesktopNav n;
    n.setLists("all", gridOnly("a b", 2));
    press(n, {Intent::Right});
    struct Row { Intent in; bool any; DeskCmd::Kind kind; bool keyed; };
    const Row rows[] = {
        {Intent::Accept, true, DeskCmd::Activate, true},
        {Intent::AcceptLong, true, DeskCmd::OpenMenu, true},
        {Intent::Alt, true, DeskCmd::OpenMenu, true},
        {Intent::Aux, true, DeskCmd::ToggleFavorite, true},
        {Intent::SectionPrev, true, DeskCmd::TabPrev, false},
        {Intent::SectionNext, true, DeskCmd::TabNext, false},
        {Intent::Search, true, DeskCmd::FocusSearch, false},
        {Intent::Back, true, DeskCmd::Back, false},
        {Intent::Menu, false, DeskCmd::Back, false},
        {Intent::None, false, DeskCmd::Back, false},
        {Intent::Up, false, DeskCmd::Back, false},
    };
    for (const Row& r : rows) {
        std::vector<DeskCmd> c = press(n, {r.in});
        if (!r.any) {
            CHECK(c.empty());
            continue;
        }
        CHECK(c.size() == 1);
        if (c.size() != 1) continue;
        CHECK(c[0].kind == r.kind);
        CHECK(r.keyed ? c[0].key == "b" : c[0].key.empty());
    }
    // Several intents run in order, moves between them.
    std::vector<DeskCmd> c = press(n, {Intent::Accept, Intent::Left, Intent::Accept, Intent::Back});
    CHECK(c.size() == 3 && c[0].key == "b" && c[1].key == "a" && c[2].kind == DeskCmd::Back);
}

static void list_changes_follow_key() {
    DesktopNav n;
    n.setLists("all", gridOnly("a b c d", 4));
    press(n, {Intent::Right});                         // b
    n.takeScroll();
    n.setLists("all", gridOnly("x a b c d", 4));       // a new item ahead of it
    CHECK(n.focusedKey() == "b" && n.index() == 2);
    CHECK(n.takeScroll());                             // it moved on screen: scroll to it
    n.setLists("all", gridOnly("x a b c d", 4));       // same lists again
    CHECK(n.focusedKey() == "b" && n.index() == 2);
    CHECK(!n.takeScroll());
    n.setLists("all", gridOnly("x a c d", 4));         // b removed: clamps to the same position
    CHECK(n.index() == 2 && n.focusedKey() == "c");
    press(n, {Intent::Right, Intent::Right});          // d: last
    CHECK(n.focusedKey() == "d");
    n.setLists("all", gridOnly("x a", 4));             // shorter than the index: clamps to the end
    CHECK(n.index() == 1 && n.focusedKey() == "a");
    // A strip that empties moves focus to the nearest non-empty region, the grid on a tie.
    DeskLists l;
    l.fav = items("f0 f1");
    l.recent = items("r0");
    l.grid = items("g0 g1");
    l.gridCols = 2;
    DesktopNav m;
    m.setLists("all", l);
    press(m, {Intent::Up});
    CHECK(m.region() == DeskRegion::RecentStrip);
    l.recent.clear();
    m.setLists("all", l);
    CHECK(m.region() == DeskRegion::Grid && m.focusedKey() == "g0");
    DesktopNav o;
    o.setLists("all", l);
    press(o, {Intent::Up});                            // fav (recent is empty)
    CHECK(o.region() == DeskRegion::FavStrip);
    l.fav.clear();
    o.setLists("all", l);
    CHECK(o.region() == DeskRegion::Grid);
    // Everything empty: no region has a key.
    o.setLists("all", DeskLists());
    CHECK(o.focusedKey().empty());
    // The focused region empties and only the other strip is left.
    DeskLists f;
    f.fav = items("f0 f1");
    f.grid = items("g0");
    DesktopNav p;
    p.setLists("all", f);
    f.grid.clear();
    p.setLists("all", f);
    CHECK(p.region() == DeskRegion::FavStrip && p.focusedKey() == "f0");
    // Memory of an emptied region survives: the same game is focused when the list comes back.
    DeskLists g;
    g.grid = items("a b c");
    g.gridCols = 3;
    DesktopNav q;
    q.setLists("all", g);
    press(q, {Intent::Right, Intent::Right});          // c
    q.setLists("all", DeskLists());
    q.setLists("all", g);
    CHECK(q.focusedKey() == "c");
}

static void per_tab_memory() {
    DesktopNav n;
    n.setLists("carts", gridOnly("a b c d", 2));
    press(n, {Intent::Right, Intent::Down});           // d
    CHECK(n.focusedKey() == "d");
    n.takeScroll();
    n.setLists("cd", gridOnly("p q r", 2));
    CHECK(n.focusedKey() == "p");                      // a tab never visited lands on its first item
    CHECK(n.takeScroll());                             // a tab switch scrolls to the landing
    press(n, {Intent::Right});
    n.setLists("carts", gridOnly("a b c d", 2));
    CHECK(n.focusedKey() == "d");                      // back where it was
    n.setLists("cd", gridOnly("p q r", 2));
    CHECK(n.focusedKey() == "q");
    // The remembered region too.
    DeskLists l;
    l.fav = items("f0 f1");
    l.grid = items("g0 g1");
    l.gridCols = 2;
    n.setLists("fav", l);
    press(n, {Intent::Up, Intent::Right});             // fav strip, f1
    n.setLists("cd", gridOnly("p q r", 2));
    n.setLists("fav", l);
    CHECK(n.region() == DeskRegion::FavStrip && n.focusedKey() == "f1");
    // The tab switch intent itself only reports the command.
    std::vector<DeskCmd> c = press(n, {Intent::SectionNext});
    CHECK(c.size() == 1 && c[0].kind == DeskCmd::TabNext);
    CHECK(n.region() == DeskRegion::FavStrip && n.focusedKey() == "f1");
    // reset() keeps memory.
    n.reset();
    n.setLists("fav", l);
    CHECK(n.focusedKey() == "f1");
}

static void empty_lists() {
    const Intent all[] = {Intent::None, Intent::Up, Intent::Down, Intent::Left, Intent::Right, Intent::Accept,
                          Intent::Back, Intent::Alt, Intent::Aux, Intent::Menu, Intent::SectionPrev,
                          Intent::SectionNext, Intent::PagePrev, Intent::PageNext, Intent::Search,
                          Intent::AcceptLong};
    for (int pass = 0; pass < 2; pass++) {
        DesktopNav n;                                  // pass 0: never given lists; pass 1: given empty ones
        if (pass) n.setLists("all", DeskLists());
        for (Intent in : all) {
            std::vector<DeskCmd> c = press(n, {in});
            for (const DeskCmd& d : c) {
                CHECK(d.kind != DeskCmd::Activate && d.kind != DeskCmd::OpenMenu && d.kind != DeskCmd::ToggleFavorite);
                CHECK(d.key.empty());
            }
        }
        CHECK(n.focusedKey().empty());
        CHECK(!n.focused(DeskRegion::Grid, 0));
        std::vector<DeskCmd> c = press(n, {Intent::SectionNext, Intent::Search, Intent::Back});
        CHECK(c.size() == 3 && c[0].kind == DeskCmd::TabNext && c[1].kind == DeskCmd::FocusSearch &&
              c[2].kind == DeskCmd::Back);
    }
}

static void scroll_flag_once() {
    DesktopNav n;
    n.setLists("all", gridOnly("a b c", 3));
    CHECK(n.takeScroll());                             // the first landing
    CHECK(!n.takeScroll());
    press(n, {Intent::Left});                          // at the edge: nothing moved
    CHECK(!n.takeScroll());
    press(n, {Intent::Right});
    CHECK(n.takeScroll());
    CHECK(!n.takeScroll());
    press(n, {Intent::Right, Intent::Right});          // several moves, one flag
    CHECK(n.takeScroll());
    CHECK(!n.takeScroll());
    press(n, {Intent::PagePrev});                      // a jump to the previous initial is a move too
    CHECK(n.takeScroll());
    press(n, {Intent::PagePrev});                      // on the first initial now
    n.takeScroll();
    press(n, {Intent::PagePrev});                      // nothing before it: nothing moved
    CHECK(!n.takeScroll());
    press(n, {Intent::Right});
    n.reset();                                         // reset forgets a pending scroll
    CHECK(!n.takeScroll());
}

static void visible_and_focused() {
    DesktopNav n;
    CHECK(!n.visible());
    n.setVisible(true);
    CHECK(n.visible());
    n.setLists("all", gridOnly("a b", 2));
    CHECK(n.focused(DeskRegion::Grid, 0) && !n.focused(DeskRegion::Grid, 1) && !n.focused(DeskRegion::States, 0));
    n.reset();
    CHECK(n.visible());                                // reset is not a visibility change
}

int main() {
    grid_moves_and_edges();
    strip_to_grid_and_back();
    states_linear();
    page_initials();
    commands_from_intents();
    list_changes_follow_key();
    per_tab_memory();
    empty_lists();
    scroll_flag_once();
    visible_and_focused();
    std::printf("desktop_nav_test: %d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
