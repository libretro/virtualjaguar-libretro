// TV mode (#991): the shell.  Owns what the TV UI is doing -- the Base layer (a stack of screens under a shared
// chrome, shown while TV mode is on with no game running) and the in-game menu laid over the desktop UI -- and who the
// pads and keyboard belong to while either is up (spec 3.1).  Everything it needs of the app goes through tv::Host.
//
// The in-game menu is plain data here: pages of rows, a focus index, and what Accept / Back do on each.  Drawing it
// (tv_ingame.cpp) only reads that model.  Beside the pages sits one slot for a tv::Screen (Achievements, Overlays, Settings): while
// it is set the panel's list area is that screen's, it gets every intent but Back, and Back clears it onto the row it came from.  The Base layer is a stack of tv::Screen (tv_screen.h) with a section tab
// per root screen; the chrome around it is tv_chrome.cpp.
//
// Input: while the shell owns the input handleEvent() feeds NavInput with the buttons / keys / axes that mean
// something to the TV UI and reports those events consumed; everything else (F4 / a menu button, which close the menu
// again, window and hot-plug events) is left for the app.  frame() turns the nav intents into menu moves, or, on the
// Base layer, into section steps, Back and the top screen's intents.
#pragma once

#include <SDL3/SDL.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "imgui.h"
#include "tv/tv_focus.h"
#include "tv/tv_host.h"
#include "tv/tv_mode.h"
#include "tv/tv_nav.h"
#include "tv/tv_screen.h"
#include "tv/tv_widgets.h"

namespace vj::tv {

// The ImGui window the in-game menu is drawn in (tests look for it: with the menu closed it must not exist).
constexpr const char* kMenuWindowName = "TV menu##vjtv";
// ... and the Base layer's (with TV mode off, or a game running, it must not exist).
constexpr const char* kBaseWindowName = "TV##vjtvbase";

enum class Layer { Base, Menu };   // Base: TV screens, under desktop windows; Menu: the in-game menu, over them

class Shell {
public:
    // ---- the in-game menu model (read by tv_ingame.cpp) ---------------------------------------------------------
    enum class Page { Root, SaveSlots, LoadSlots, ConfirmReset, ConfirmClose };
    struct Row {
        enum Kind { Resume, Save, Load, Achievements, Overlays, Settings, Controls, TvMode, Reset, Close, Slot, No, Yes };
        Kind kind;
        std::string label;
        int slot = -1;         // Slot rows: the state slot
        bool enabled = true;   // Load-state slots: false when the slot is empty
    };

    explicit Shell(Host& h);

    bool menuOpen() const { return open_; }
    bool openMenu();                 // menuPause(); refused -> toast(why), false
    void closeMenu();                // menuResume(), armReleaseGuard(), clearHolds()
    void toggleMenu();
    bool ownsInput() const;          // §3.1: inputOwner() == TvUi: the menu is open, or TV is active with no game running
    bool handleEvent(const SDL_Event& e, uint64_t ms);   // nav input while ownsInput(); true = consumed
    void frame(uint64_t ms);         // NavInput::take -> the top screen
    void draw(Layer layer, ImVec2 pos, ImVec2 size);
    bool backAtRoot() const;         // Esc keeps its desktop meaning (leave fullscreen) only here

    // ---- the Base layer ----------------------------------------------------------------------------------------------
    bool baseActive() const;                    // host.tvActive() && !host.gameLoaded()
    Section section() const { return section_; }
    void setSection(Section s);                 // replaces the stack with that section's root screen (created fresh; focus via memory())
    void openSection(Section s, std::unique_ptr<Screen> root);   // ... with a root the caller prepared (Game details -> Achievements on a game)
    void push(std::unique_ptr<Screen> s);
    void pop();                                 // never pops the root
    std::string focusLabel();                   // the top screen's focusLabel(), "" when the stack is empty
    std::string screenId() const;               // the top screen's id(), "" when the stack is empty
    FocusMemory& memory() { return memory_; }
    void launched();                            // a game was started from a screen: armReleaseGuard() + clearHolds()
    Host& host() { return host_; }
    void noteCapture(bool active);              // a binding capture started / ended: its events bypass the shell, so reset the nav

    Page menuPage() const { return page_; }
    const std::string& menuTitle() const { return title_; }
    const std::vector<Row>& menuRows() const { return rows_; }
    int menuFocus() const { return focus_.index; }
    LinearFocus& menuFocusState() { return focus_; }   // the drawer scrolls it (scrollToShow)
    bool menuHasScreen() const { return menuScreen_ != nullptr; }   // the slot is set
    std::string menuScreenId() const { return menuScreen_ ? std::string(menuScreen_->id()) : std::string(); }
    // Puts `s` in the slot (the menu must be open); Back returns the focus to the first row of kind `from`.  Takes the screen's embedded mode.
    void menuPushScreen(std::unique_ptr<Screen> s, Row::Kind from);
    bool menuNetplay() const { return host_.netplayActive(); }   // the banner "Netplay: the game keeps running"
    void setMenuFocus(int index);    // the pointer hovered a row
    void intent(Intent in);          // one nav intent (frame() calls it; a click on a row is Accept after setMenuFocus)

private:
    void enter(Page p, Row::Kind focusKind);   // build `p`'s rows and focus the first row of `focusKind` (else row 0)
    void buildRoot();
    int tvRowState() const;   // rootTvRow_ as the host says it is now
    void accept();
    void back();
    void clearMenuScreen();   // the slot's Back: retire the screen and return to the root, focused on the row it came from
    void drawMenu(ImVec2 pos, ImVec2 size);   // tv_ingame.cpp: the menu model drawn over the dimmed game
    void drawBase(ImVec2 pos, ImVec2 size);   // tv_chrome.cpp: the Base window -- chrome, then the stack
    void baseIntent(Intent in);               // one nav intent on the Base layer: section step, Back, or the top screen's
    void stepSection(int delta);              // +-1, no wrap
    Screen* top() const { return stack_.empty() ? nullptr : stack_.back().get(); }
    void retire(std::unique_ptr<Screen> s);   // a removed screen may be mid-call (a Play pops its own screen): it dies at the next frame

    Host& host_;
    bool open_ = false;
    Page page_ = Page::Root;
    std::string title_;
    std::vector<Row> rows_;
    LinearFocus focus_;
    NavInput nav_;
    widgets::FocusTweens tweens_;   // the focus ring easing between rows (drawing only)
    float wheelAcc_ = 0;            // mouse-wheel notches not yet spent on a focus step (a trackpad sends fractions)
    bool focusWindow_ = false;      // the menu just opened: its window takes the focus once
    bool rootHasLoad_ = false;   // the Load-state row is in the root page (re-evaluated while it shows)
    std::unique_ptr<Screen> menuScreen_;   // the slot; set only while open_ (closeMenu() and enter() retire it)
    Row::Kind menuFrom_ = Row::Resume;     // the root row that opened it
    int rootTvRow_ = 0;          // the TV-mode row of the root page: 0 = none, 1 = "Enter TV mode", 2 = "Exit TV mode" (re-evaluated while it shows)

    // The Base layer.  The stack is kept while a game runs, so closing the game lands back where the player was.
    std::vector<std::unique_ptr<Screen>> stack_;   // [0] is the section's root
    std::vector<std::unique_ptr<Screen>> retired_;
    Section section_ = Section::Home;
    unsigned stackGen_ = 0;       // bumped on every push / pop / section change: drawBase stops when its list went stale
    FocusMemory memory_;
    bool capturing_ = false;
    bool baseWas_ = false;        // baseActive() with no desktop window open over it, at the last frame(): a change resets the nav
    bool padDriven_ = false;      // the last input was a pad: the pointer is hidden until the mouse moves
};

}  // namespace vj::tv
