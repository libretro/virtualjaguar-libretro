// TV mode (#991): a Base-layer screen.  The shell keeps a stack of them (tv_shell.h), draws the chrome around the
// top one (tv_chrome.cpp) and hands it the nav intents; a screen is drawn on an ImDrawList and tests the pointer with
// plain rectangles (see tv_widgets.h), so it never owns an ImGui item.
//
// A screen never sees Intent::Back, SectionPrev or SectionNext: those are the shell's (a pop, a step to the
// neighbouring section).  What a screen needs of the app it asks of tv::Host.
#pragma once

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "imgui.h"
#include "tv/tv_focus.h"
#include "tv/tv_host.h"
#include "tv/tv_nav.h"
#include "tv/tv_style.h"
#include "tv/tv_widgets.h"

namespace vj::tv {

class Shell;

enum class Section { Home, Library };   // Phase 3 appends Achievements and Settings

// What a screen draws with, once per frame.  The pointer fields are false / zero unless the Base window itself is
// hovered, so a click on a desktop window drawn above it never reaches a tile.
struct DrawCtx {
    ImDrawList* dl;
    Metrics m;
    ImVec2 pos, size;   // the content area (between the top bar and the hint bar), pixels
    bool hovered;       // the Base window is hovered (not a desktop window above it)
    ImVec2 mouse;
    bool clicked, rightClicked;
    float wheel;
    double now;         // Host::now(), seconds: the unit the tweens run in
};

class Screen {
public:
    virtual ~Screen() = default;
    virtual const char* id() const = 0;   // "home" | "library" | "letters" | "details": FocusMemory keys, tests
    virtual void intent(Intent in) = 0;
    virtual bool typed(char c) { return false; }   // 'a'..'z', '0'..'9', '\b'; true = used
    virtual void draw(const DrawCtx& c) = 0;       // draws, and does hover-focus / click / wheel from c
    virtual std::vector<widgets::Hint> hints() const = 0;   // last = the way out
    virtual bool modal() const { return false; }   // drawn over the screen below it, which is drawn first
};

// The two root screens (tv_home.cpp, tv_library.cpp).
std::unique_ptr<Screen> makeHomeScreen(Host& h, Shell& s);
std::unique_ptr<Screen> makeLibraryScreen(Host& h, Shell& s);

// Game details (tv_details.cpp): the art, the title and a list -- Play, one Resume row per save state, Favorite.  Pushed by
// Start or a right-click on a tile of Home or the Library.  The list is fetched again every draw, keyed by `g.key`.
class DetailsScreen : public Screen {
public:
    DetailsScreen(Host& h, Shell& s, GameRef g) : host_(h), shell_(s), g_(std::move(g)) {}
    const char* id() const override { return "details"; }
    void intent(Intent in) override;
    void draw(const DrawCtx& c) override;
    std::vector<widgets::Hint> hints() const override;

private:
    struct Item {
        enum Kind { Play, Resume, Favorite } kind;
        std::string label, sub;
        int slot = -1;
        int64_t time = 0;
    };
    void refresh();
    void accept();

    Host& host_;
    Shell& shell_;
    GameRef g_;
    std::vector<Item> items_;   // from the last refresh(); empty when the game is gone
    LinearFocus focus_;
    Tween scroll_;              // vertical scroll of the list, in pixels
    float scrollTo_ = 0;        // ... and where it is heading
    bool scrollInit_ = false;
    float lastU_ = 0;
    widgets::FocusTweens tweens_;
    ImVec2 lastMouse_{-1e30f, -1e30f};
    bool mouseSeen_ = false;
    float wheel_ = 0;           // wheel notches not yet spent (a trackpad sends fractions)
};

}  // namespace vj::tv
