#include "tv/tv_shell.h"

namespace vj::tv {

namespace {
constexpr int kSlots = 10;   // state slots 0..9 (App::stateSlot cycles through the same ten)
}

Shell::Shell(Host& h) : host_(h) {}

bool Shell::ownsInput() const { return open_; }
bool Shell::backAtRoot() const { return !open_; }   // Back on the in-game menu is always something (a page up, or Resume)

bool Shell::openMenu() {
    if (open_) return true;
    if (!host_.gameLoaded()) return false;   // phase 1: the menu belongs to a running game
    std::string why;
    if (!host_.menuPause(why)) {             // hardcore rations pausing
        host_.toast(why);
        return false;
    }
    open_ = true;
    nav_.reset();
    enter(Page::Root, Row::Resume);
    return true;
}

void Shell::closeMenu() {
    if (!open_) return;
    open_ = false;
    nav_.reset();
    host_.menuResume();
    host_.armReleaseGuard();   // whatever is down now (the A that picked Resume) stays masked from the game
    host_.clearHolds();        // ... and no hold or tap pulse in flight survives the hand-off
}

void Shell::toggleMenu() {
    if (open_) closeMenu();
    else openMenu();
}

// ---- input --------------------------------------------------------------------------------------------------------

bool Shell::handleEvent(const SDL_Event& e, uint64_t ms) {
    if (!ownsInput()) return false;
    switch (e.type) {
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
    case SDL_EVENT_GAMEPAD_BUTTON_UP:
        if (intentForButton(e.gbutton.button) == Intent::None) return false;   // a menu button / paddle is the app's
        nav_.padButton(e.gbutton.button, e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN, ms);
        return true;
    case SDL_EVENT_GAMEPAD_AXIS_MOTION:
        // The left stick and the two triggers only; the right stick is the game's (and the pointer's).
        if (e.gaxis.axis != SDL_GAMEPAD_AXIS_LEFTX && e.gaxis.axis != SDL_GAMEPAD_AXIS_LEFTY &&
            e.gaxis.axis != SDL_GAMEPAD_AXIS_LEFT_TRIGGER && e.gaxis.axis != SDL_GAMEPAD_AXIS_RIGHT_TRIGGER)
            return false;
        nav_.padAxis(e.gaxis.axis, e.gaxis.value, ms);
        return true;
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP: {
        if (e.key.mod & (SDL_KMOD_CTRL | SDL_KMOD_ALT | SDL_KMOD_GUI)) return false;   // Alt+Enter and the like: hotkeys
        const bool shift = (e.key.mod & SDL_KMOD_SHIFT) != 0;
        if (intentForKey(e.key.scancode, shift) == Intent::None) return false;   // F4 and friends are the app's
        nav_.key(e.key.scancode, e.type == SDL_EVENT_KEY_DOWN, shift, ms);
        return true;
    }
    case SDL_EVENT_WINDOW_FOCUS_LOST:
    case SDL_EVENT_GAMEPAD_REMOVED:
        nav_.reset();   // the matching "up" will never come: no direction may keep repeating
        return false;
    default: return false;
    }
}

void Shell::frame(uint64_t ms) {
    if (!open_) return;
    if (!host_.gameLoaded()) {   // the game went away under the menu
        closeMenu();
        return;
    }
    if (page_ == Page::Root && host_.canLoadState() != rootHasLoad_) enter(Page::Root, rows_[(size_t)focus_.index].kind);
    for (Intent in : nav_.take(ms)) {
        intent(in);
        if (!open_) break;
    }
}

void Shell::draw(Layer, ImVec2, ImVec2) {}   // phase 1 part B (tv_ingame.cpp)

// ---- the in-game menu ---------------------------------------------------------------------------------------------

void Shell::buildRoot() {
    title_ = host_.gameTitle();
    rows_.clear();
    rows_.push_back({Row::Resume, "Resume"});
    rows_.push_back({Row::Save, "Save state"});
    rootHasLoad_ = host_.canLoadState();
    if (rootHasLoad_) rows_.push_back({Row::Load, "Load state"});   // hardcore refuses loads: no row at all
    rows_.push_back({Row::Controls, "Controls"});
    rows_.push_back({Row::Reset, "Reset"});
    rows_.push_back({Row::Close, "Close game"});
}

void Shell::enter(Page p, Row::Kind focusKind) {
    page_ = p;
    switch (p) {
    case Page::Root: buildRoot(); break;
    case Page::SaveSlots:
    case Page::LoadSlots: {
        const bool load = p == Page::LoadSlots;
        title_ = load ? "Load state" : "Save state";
        bool exists[kSlots] = {};
        for (const SlotInfo& s : host_.slots())
            if (s.slot >= 0 && s.slot < kSlots) exists[s.slot] = s.exists;
        rows_.clear();
        for (int i = 0; i < kSlots; i++) {
            Row r{Row::Slot, "Slot " + std::to_string(i), i};
            r.enabled = !load || exists[i];   // nothing to load from an empty slot
            rows_.push_back(r);
        }
        break;
    }
    case Page::ConfirmReset:
    case Page::ConfirmClose:
        title_ = p == Page::ConfirmReset ? "Reset the game?" : "Close the game?";
        rows_.clear();
        rows_.push_back({Row::No, "No"});   // the safe answer first: focus starts there
        rows_.push_back({Row::Yes, "Yes"});
        break;
    }
    focus_ = LinearFocus();
    focus_.count = (int)rows_.size();
    for (int i = 0; i < focus_.count; i++)
        if (rows_[(size_t)i].kind == focusKind && rows_[(size_t)i].enabled) {
            focus_.index = i;
            break;
        }
}

void Shell::setMenuFocus(int index) {
    if (open_ && index >= 0 && index < focus_.count) focus_.index = index;
}

void Shell::intent(Intent in) {
    if (!open_) return;
    switch (in) {
    case Intent::Up: focus_.move(-1); break;
    case Intent::Down: focus_.move(1); break;
    case Intent::Accept: accept(); break;
    case Intent::Back: back(); break;
    default: break;
    }
}

void Shell::accept() {
    if (focus_.index < 0 || focus_.index >= (int)rows_.size()) return;
    const Row r = rows_[(size_t)focus_.index];   // a copy: the action below rebuilds rows_
    if (!r.enabled) return;
    switch (r.kind) {
    case Row::Resume: closeMenu(); break;
    case Row::Save: enter(Page::SaveSlots, Row::Slot); break;
    case Row::Load: enter(Page::LoadSlots, Row::Slot); break;
    case Row::Controls:
        closeMenu();   // the sheet is drawn over the game, so the game has to be showing
        host_.toggleControlsOverlay();
        break;
    case Row::Reset: enter(Page::ConfirmReset, Row::No); break;
    case Row::Close: enter(Page::ConfirmClose, Row::No); break;
    case Row::Slot:
        if (page_ == Page::SaveSlots) {
            if (host_.saveState(r.slot)) enter(Page::Root, Row::Save);   // the app's toast says it is saved; stay in the menu
        } else if (page_ == Page::LoadSlots) {
            if (host_.loadState(r.slot)) closeMenu();
        }
        break;
    case Row::No: enter(Page::Root, page_ == Page::ConfirmReset ? Row::Reset : Row::Close); break;
    case Row::Yes:
        if (page_ == Page::ConfirmReset) {
            host_.reset();
            closeMenu();
        } else {
            host_.closeGame();   // the app closes the menu itself on the way (it must not outlive the game)
            if (open_) {
                if (host_.gameLoaded()) enter(Page::Root, Row::Close);   // refused (debugger halt): the app toasted why
                else closeMenu();
            }
        }
        break;
    }
}

void Shell::back() {
    switch (page_) {
    case Page::Root: closeMenu(); break;
    case Page::SaveSlots: enter(Page::Root, Row::Save); break;
    case Page::LoadSlots: enter(Page::Root, Row::Load); break;
    case Page::ConfirmReset: enter(Page::Root, Row::Reset); break;
    case Page::ConfirmClose: enter(Page::Root, Row::Close); break;
    }
}

}  // namespace vj::tv
