// TV mode (#991): the focus model (LinearFocus, GridFocus, FocusMemory, the A-Z jump helpers) and the style
// metrics / tween.
//
//   vj_tv_focus_test
//
// Pure logic: no window, no core, no clock.  Timestamps are passed in.
#include "tv/tv_focus.h"
#include "tv/tv_style.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace vj::tv;

static int g_fail = 0, g_checks = 0;

#define CHECK(c)                                                              \
    do {                                                                      \
        g_checks++;                                                           \
        if (!(c)) {                                                           \
            g_fail++;                                                         \
            std::fprintf(stderr, "FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); \
        }                                                                     \
    } while (0)

static void testGrid() {
    GridFocus g{14, 6, 0};
    CHECK(g.move(-1, 0) == GridFocus::Left && g.index == 0);
    CHECK(g.move(0, -1) == GridFocus::Top);
    g.index = 10;
    CHECK(g.move(0, 1) == GridFocus::None && g.index == 13);   // short last row: last item
    g.index = 13;
    CHECK(g.move(0, 1) == GridFocus::Bottom);
    g.index = 5;
    CHECK(g.move(1, 0) == GridFocus::Right && g.index == 5);
    // Plain moves inside the grid.
    g.index = 0;
    CHECK(g.move(1, 0) == GridFocus::None && g.index == 1);
    CHECK(g.move(0, 1) == GridFocus::None && g.index == 7);
    CHECK(g.move(0, -1) == GridFocus::None && g.index == 1);
    // The last row is short: the right edge is its last item, not the column count.
    g.index = 13;
    CHECK(g.move(1, 0) == GridFocus::Right && g.index == 13);
    CHECK(g.move(-1, 0) == GridFocus::None && g.index == 12);
    CHECK(g.row() == 2 && g.col() == 0 && g.rows() == 3);
    // Scrolling by rows.
    g.firstRow = 0;
    g.index = 13;
    g.scrollToShow(2);
    CHECK(g.firstRow == 1);
    g.index = 0;
    g.scrollToShow(2);
    CHECK(g.firstRow == 0);
    // Empty grid: nothing moves, nothing throws.
    GridFocus e{0, 6, 0};
    CHECK(e.rows() == 0);
    CHECK(e.move(0, 1) != GridFocus::None && e.index == 0);
}

static void testLinear() {
    LinearFocus l{5, 4};
    CHECK(!l.move(1) && l.index == 4);   // no wrap
    l.index = 0;
    CHECK(!l.move(-1));
    CHECK(l.move(1) && l.index == 1);
    LinearFocus sc{20, 12};
    sc.scrollToShow(6);
    CHECK(sc.first == 7);
    sc.index = 3;
    sc.scrollToShow(6);
    CHECK(sc.first == 3);
    sc.index = 8;
    sc.scrollToShow(6);
    CHECK(sc.first == 3);   // already inside the window
    LinearFocus c{10, 9};
    c.clampTo(4);
    CHECK(c.count == 4 && c.index == 3);
    c.clampTo(0);
    CHECK(c.count == 0 && c.index == 0 && !c.move(1));
}

static void testMemory() {
    FocusMemory m;
    m.put("home/favorites", 3);
    CHECK(m.get("home/favorites") == 3 && m.get("home/recents", 0) == 0);
    CHECK(m.get("nope", 9) == 9);
    m.put("home/favorites", 5);
    CHECK(m.get("home/favorites") == 5);
    // The item that had the focus is remembered beside the index, per key; none = "".
    CHECK(m.getItem("home/favorites").empty());
    m.putItem("home/favorites", "/games/a.j64");
    CHECK(m.getItem("home/favorites") == "/games/a.j64" && m.getItem("home/recent").empty());
    CHECK(m.get("home/favorites") == 5);   // the index is untouched
    m.putItem("home/favorites", "/games/b.j64");
    CHECK(m.getItem("home/favorites") == "/games/b.j64");
}

static void testInitials() {
    std::vector<std::string> t = {"1942", "Alien vs Predator", "Atari Karts", "Cybermorph", "Doom"};
    CHECK(initialOf("alien") == 'A' && initialOf("1942") == '1' && initialOf("[x]") == '#');
    CHECK(initialOf("") == '#');
    CHECK(jumpInitial(t, 1, +1) == 3 && jumpInitial(t, 3, -1) == 1 && jumpInitial(t, 4, +1) == 4);
    CHECK(jumpInitial(t, 2, -1) == 0);   // from inside the A group back to the digits
    CHECK(jumpInitial(t, 0, -1) == 0);
    CHECK(firstWithInitial(t, 'D') == 4 && firstWithInitial(t, 'Z') == -1);
    CHECK(firstWithInitial(t, 'a') == 1);
}

static void testStyle() {
    Metrics m1 = metrics(1080), m2 = metrics(2160);
    CHECK(m1.u == 1 && m1.safeX == 96 && m1.safeY == 54 && m1.body == 30 && m1.minText == 24 && m1.ring == 5);
    CHECK(m2.body == 60 && m1.focusScale > 1.05f && m1.focusScale < 1.10f);
    CHECK(m1.caption == 26 && m1.titleSmall == 56 && m1.titleLarge == 84);
    Tween tw{0, 1, 0};
    CHECK(tw.at(0) == 0 && tw.at(0.120) == 1 && tw.at(0.060) > 0.5f);   // ease-out: past half at half time
    CHECK(tw.at(5.0) == 1 && tw.at(-1.0) == 0);                          // clamped outside the window
    CHECK(easeOut(0) == 0 && easeOut(1) == 1);
    tw.retarget(0, 0.060);                                               // starts from where it is now
    float mid = tw.from;
    CHECK(mid > 0.5f && mid < 1 && tw.to == 0 && tw.at(0.060) == mid && tw.at(0.060 + kTweenSeconds) == 0);
    // "More settings" in TV mode: the desktop window's text is scaled to the TV body size (30u), never down, 1 with no font.
    CHECK(bodyFontScale(15, 1080) == 2 && bodyFontScale(15, 2160) == 4 && bodyFontScale(30, 720) == 1);   // (20 px wanted at 720p: already bigger)
    CHECK(bodyFontScale(15, 720) > 1.33f && bodyFontScale(15, 720) < 1.34f && bodyFontScale(60, 1080) == 1);
    CHECK(bodyFontScale(0, 1080) == 1 && bodyFontScale(-3, 1080) == 1);
}

int main() {
    testGrid();
    testLinear();
    testMemory();
    testInitials();
    testStyle();
    std::printf("tv_focus_test: %d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
