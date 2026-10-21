#include "tv/tv_shell.h"

#include <cstdlib>

namespace vj::tv {

namespace {
constexpr int kSlots = 10;   // state slots 0..9 (App::stateSlot cycles through the same ten)

// A key that goes to the top screen as typing: a-z and 0-9 by the layout-aware keycode (not the scancode: AZERTY
// types its own letters), Backspace as '\b'.  0 = not typing.
char typedChar(const SDL_KeyboardEvent& k) {
    if (k.key >= SDLK_A && k.key <= SDLK_Z) return (char)('a' + (k.key - SDLK_A));
    if (k.key >= 'A' && k.key <= 'Z') return (char)('a' + (k.key - 'A'));   // (SDL gives the unshifted keycode; just in case)
    if (k.key >= SDLK_0 && k.key <= SDLK_9) return (char)('0' + (k.key - SDLK_0));
    if (k.scancode == SDL_SCANCODE_BACKSPACE) return '\b';
    return 0;
}

// An intent the TV screens act on.  Search and AcceptLong are profile intents of the desktop library (#1021): a button
// mapped to one is not TV's, as an unmapped button is not.
bool isTvIntent(Intent in) { return in != Intent::None && in != Intent::Search && in != Intent::AcceptLong; }
}  // namespace

Shell::Shell(Host& h) : host_(h) {}

// The Base layer also steps aside while a desktop window (Settings, States, the library window ...) or a popup is open over
// it: a pad A must not launch the game hidden behind Settings.  The in-game menu keeps the input regardless.
bool Shell::ownsInput() const {
    if (inputOwner(host_.tvActive(), host_.gameLoaded(), open_) != Owner::TvUi) return false;
    return open_ || !host_.desktopWindowOpen();
}
bool Shell::baseActive() const { return host_.tvActive() && !host_.gameLoaded(); }

// Esc keeps its desktop meaning (leave fullscreen) only where Back would do nothing: on the Home root.  Back on the
// in-game menu is always something (a page up, or Resume).
bool Shell::backAtRoot() const {
    if (open_) return false;
    if (baseActive()) return stack_.size() <= 1 && section_ == Section::Home;
    return true;
}

bool Shell::openMenu() {
    if (open_) return true;
    std::string why;
    if (!host_.menuPause(why)) {             // hardcore rations pausing
        host_.toast(why);
        return false;
    }
    open_ = true;
    focusWindow_ = true;
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
    const bool base = !open_;   // ownsInput() with the menu closed = the Base layer (TV on, no game)
    switch (e.type) {
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
    case SDL_EVENT_GAMEPAD_BUTTON_UP: {
        // The pad's own menu-button profile (#1021), asked of the host: D-pad, face, shoulder buttons and whatever else the
        // profile maps.  Not TV's: Search and AcceptLong (the desktop library's), so those buttons stay the app's.
        // Start (Menu) too, except on the Base layer, where it is the screens' Menu (Game details); with the menu open
        // it stays the app's.
        const Intent in = host_.padIntent(e.gbutton.which, e.gbutton.button);
        if (!isTvIntent(in) || (in == Intent::Menu && !base)) return false;
        const bool down = e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN;
        if (down) padDriven_ = true;
        nav_.padIntent(e.gbutton.which, e.gbutton.button, in, down, ms);
        return down;   // a release is fed to the nav (it stops a repeat) but still reaches the app
    }
    case SDL_EVENT_GAMEPAD_AXIS_MOTION:
        // The left stick and the two triggers only; the right stick is the game's (and the pointer's).
        if (e.gaxis.axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER || e.gaxis.axis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) {
            const Intent in = host_.padTriggerIntent(e.gaxis.which, e.gaxis.axis);
            if (!isTvIntent(in)) return false;   // an unbound trigger stays the app's
            if (std::abs((int)e.gaxis.value) >= NavInput::kTriggerOn) padDriven_ = true;
            nav_.padTrigger(e.gaxis.which, e.gaxis.axis, in, e.gaxis.value, ms);
            return true;
        }
        if (e.gaxis.axis != SDL_GAMEPAD_AXIS_LEFTX && e.gaxis.axis != SDL_GAMEPAD_AXIS_LEFTY) return false;
        if (std::abs((int)e.gaxis.value) >= NavInput::kTriggerOn) padDriven_ = true;   // a resting stick drifts: only a push counts
        nav_.padAxis(e.gaxis.axis, e.gaxis.value, ms);
        return true;
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP: {
        const bool down = e.type == SDL_EVENT_KEY_DOWN;
        // Alt+Enter and the like are hotkeys (a press only: a release always reaches the nav, or a repeat could stick).
        if (down && (e.key.mod & (SDL_KMOD_CTRL | SDL_KMOD_ALT | SDL_KMOD_GUI))) return false;
        const bool shift = (e.key.mod & SDL_KMOD_SHIFT) != 0;
        if (base && down) {
            // A text field of a desktop window has the keyboard: every key is the field's (Enter, Tab, Backspace, the
            // arrows, Esc too).  Key-ups still reach the nav below, so a repeat that began earlier can stop.
            if (ImGui::GetCurrentContext() && ImGui::GetIO().WantTextInput) return false;
            // Esc where Back would do nothing keeps its desktop meaning: not fed, not consumed.
            if (e.key.scancode == SDL_SCANCODE_ESCAPE && backAtRoot()) return false;
            // Typing goes to the top screen.
            const char c = typedChar(e.key);
            if (c && top() && top()->typed(c)) return true;   // used: not Back, not the app's
            // A held Backspace repeats: the repeats the screen does not use (the filter ran out) are dropped, not turned into
            // Back -- only a fresh press may leave the screen.
            if (c == '\b' && e.key.repeat) return true;
        }
        // An OS key repeat is never a fresh press.  NavInput repeats the directions on its own timer, and a repeat that
        // reaches a nav which was just reset (a game closed, a window closed, with Enter still held) would be taken for a
        // new Accept and launch a game.  So every repeat of a nav key is dropped here (consumed: not the app's either);
        // typing and the Backspace rule above came first.
        if (down && e.key.repeat) return intentForKey(e.key.scancode, shift) != Intent::None;
        if (intentForKey(e.key.scancode, shift) == Intent::None) return false;   // F4 and friends are the app's
        nav_.key(e.key.scancode, down, shift, ms);
        return down;   // a key-up still reaches the app (a Tab held for fast-forward must be let go of)
    }
    case SDL_EVENT_MOUSE_MOTION:
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        padDriven_ = false;   // the pointer is the player's again
        return false;
    case SDL_EVENT_GAMEPAD_REMOVED:
        // Only that pad's holds go (the matching "up" will never come); the keyboard's and the other pads' stay.  The
        // left stick is shared (padAxis): centre it, or a direction held on the unplugged pad would repeat forever.
        nav_.forgetPad(e.gdevice.which);
        nav_.padAxis(SDL_GAMEPAD_AXIS_LEFTX, 0, ms);
        nav_.padAxis(SDL_GAMEPAD_AXIS_LEFTY, 0, ms);
        return false;
    case SDL_EVENT_WINDOW_FOCUS_LOST:
        nav_.reset();   // the matching "up" will never come: no direction may keep repeating
        return false;
    default: return false;
    }
}

void Shell::frame(uint64_t ms) {
    retired_.clear();   // screens removed since the last frame: nothing can be inside them now
    // The Base layer got or lost the input (a game started or closed, TV mode toggled): the releases in between never
    // reached the nav, so forget what it thinks is held, or a direction would repeat and a button would stay "down".
    // Same when a desktop window opens or closes over the Base layer: it stops taking input (ownsInput()).
    const bool live = baseActive() && !host_.desktopWindowOpen();
    if (live != baseWas_) {
        baseWas_ = live;
        nav_.reset();
    }
    if (!open_) {
        if (!baseActive()) return;
        if (stack_.empty()) setSection(Section::Home);
        if (!live) return;   // a desktop window has the pads and keys: the screens wait
        for (Intent in : nav_.take(ms)) baseIntent(in);
        return;
    }
    if (page_ == Page::Root && (host_.canLoadState() != rootHasLoad_ || tvRowState() != rootTvRow_))
        enter(Page::Root, rows_[(size_t)focus_.index].kind);
    for (Intent in : nav_.take(ms)) {
        intent(in);
        if (!open_) break;
    }
}

void Shell::draw(Layer layer, ImVec2 pos, ImVec2 size) {
    // Layer::Base exists only while TV mode is on with no game running; Layer::Menu only while the menu is open.  With a
    // game running and the menu closed there is no shell window at all, so the pointer stays the game's.
    if (layer == Layer::Base && baseActive()) drawBase(pos, size);
    else if (layer == Layer::Menu && open_) drawMenu(pos, size);
}

// ---- the Base layer -----------------------------------------------------------------------------------------------

void Shell::setSection(Section s) {
    section_ = s;
    for (std::unique_ptr<Screen>& old : stack_) retire(std::move(old));
    stack_.clear();
    stack_.push_back(s == Section::Home ? makeHomeScreen(host_, *this) : makeLibraryScreen(host_, *this));
    stackGen_++;
}

void Shell::push(std::unique_ptr<Screen> s) {
    if (!s) return;
    stack_.push_back(std::move(s));
    stackGen_++;
}

void Shell::pop() {
    if (stack_.size() <= 1) return;
    retire(std::move(stack_.back()));
    stack_.pop_back();
    stackGen_++;
}

void Shell::retire(std::unique_ptr<Screen> s) {
    if (s) retired_.push_back(std::move(s));
}

std::string Shell::screenId() const { return stack_.empty() ? std::string() : std::string(stack_.back()->id()); }

// While a binding is being captured App::handleEvent keeps every event from the shell: a direction that was down when the
// capture began would never see its release and repeat on, so the nav starts over at both edges.
void Shell::noteCapture(bool active) {
    if (active == capturing_) return;
    capturing_ = active;
    nav_.reset();
}

void Shell::launched() {
    host_.armReleaseGuard();   // the A that started the game must not also reach it
    host_.clearHolds();
}

void Shell::stepSection(int delta) {
    const int n = (int)section_ + delta;
    if (n < (int)Section::Home || n > (int)Section::Library) return;   // no wrap
    setSection((Section)n);
}

// Back and the section steps are the shell's; everything else is the top screen's.
void Shell::baseIntent(Intent in) {
    switch (in) {
    case Intent::SectionPrev: stepSection(-1); break;
    case Intent::SectionNext: stepSection(1); break;
    case Intent::Back:
        if (stack_.size() > 1) pop();
        else if (section_ != Section::Home) setSection(Section::Home);
        break;
    case Intent::None: break;
    default:
        if (Screen* t = top()) t->intent(in);
        break;
    }
}

// ---- the in-game menu ---------------------------------------------------------------------------------------------

int Shell::tvRowState() const { return !host_.tvAvailable() ? 0 : host_.tvActive() ? 2 : 1; }

void Shell::buildRoot() {
    title_ = host_.gameTitle();
    rows_.clear();
    rows_.push_back({Row::Resume, "Resume"});
    rows_.push_back({Row::Save, "Save state"});
    rootHasLoad_ = host_.canLoadState();
    if (rootHasLoad_) rows_.push_back({Row::Load, "Load state"});   // hardcore refuses loads: no row at all
    rows_.push_back({Row::Controls, "Controls"});
    rootTvRow_ = tvRowState();
    if (rootTvRow_) rows_.push_back({Row::TvMode, rootTvRow_ == 2 ? "Exit TV mode" : "Enter TV mode"});   // none when the setting is Off
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
        rows_.push_back({Row::Yes, "Yes"});
        rows_.push_back({Row::No, "No"});   // focus starts on the safe answer (enter() is given Row::No)
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
        host_.showControlsOverlay();
        break;
    case Row::TvMode:
        host_.setTvActive(!host_.tvActive());
        enter(Page::Root, Row::TvMode);   // the label flipped; the menu stays open and the focus stays on the row
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
