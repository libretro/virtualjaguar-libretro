// TV mode (#991): the shell.  Owns what the TV UI is doing -- today (phase 1) only the in-game menu laid over the
// desktop UI -- and who the pads and keyboard belong to while it is up (spec 3.1).  Everything it needs of the app
// goes through tv::Host.
//
// The in-game menu is plain data here: pages of rows, a focus index, and what Accept / Back do on each.  Drawing it
// (tv_ingame.cpp) only reads that model.
//
// Input: while the menu is open handleEvent() feeds NavInput with the buttons / keys / axes that mean something to
// the TV UI and reports those events consumed; everything else (F4 / a menu button, which close the menu again,
// window and hot-plug events) is left for the app.  frame() turns the nav intents into menu moves.
#pragma once

#include <SDL3/SDL.h>

#include <cstdint>
#include <string>
#include <vector>

#include "imgui.h"
#include "tv/tv_focus.h"
#include "tv/tv_host.h"
#include "tv/tv_nav.h"

namespace vj::tv {

enum class Layer { Base, Menu };   // Base: TV screens, under desktop windows; Menu: the in-game menu, over them

class Shell {
public:
    // ---- the in-game menu model (read by tv_ingame.cpp) ---------------------------------------------------------
    enum class Page { Root, SaveSlots, LoadSlots, ConfirmReset, ConfirmClose };
    struct Row {
        enum Kind { Resume, Save, Load, Controls, Reset, Close, Slot, No, Yes };
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
    bool ownsInput() const;          // §3.1: menu open, or TV active with no game running (phase 2)
    bool handleEvent(const SDL_Event& e, uint64_t ms);   // nav input while ownsInput(); true = consumed
    void frame(uint64_t ms);         // NavInput::take -> the top screen
    void draw(Layer layer, ImVec2 pos, ImVec2 size);
    bool backAtRoot() const;         // Esc keeps its desktop meaning (leave fullscreen) only here

    Page menuPage() const { return page_; }
    const std::string& menuTitle() const { return title_; }
    const std::vector<Row>& menuRows() const { return rows_; }
    int menuFocus() const { return focus_.index; }
    LinearFocus& menuFocusState() { return focus_; }   // the drawer scrolls it (scrollToShow)
    bool menuNetplay() const { return host_.netplayActive(); }   // the banner "Netplay: the game keeps running"
    void setMenuFocus(int index);    // the pointer hovered a row
    void intent(Intent in);          // one nav intent (frame() calls it; a click on a row is Accept after setMenuFocus)

private:
    void enter(Page p, Row::Kind focusKind);   // build `p`'s rows and focus the first row of `focusKind` (else row 0)
    void buildRoot();
    void accept();
    void back();

    Host& host_;
    bool open_ = false;
    Page page_ = Page::Root;
    std::string title_;
    std::vector<Row> rows_;
    LinearFocus focus_;
    NavInput nav_;
    bool rootHasLoad_ = false;   // the Load-state row is in the root page (re-evaluated while it shows)
};

}  // namespace vj::tv
