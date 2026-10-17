// TV mode (#991): when TV mode is on, and who owns the input.  Pure state, no SDL and no ImGui: the app feeds it
// the events (a fullscreen change, a pad press, mouse travel, a keyboard key that is not navigation) and reads back
// whether TV mode is active.
//
// Three stored settings: Off (never), Manual (only the toggle), Auto (also on fullscreen and on a controller press).
// TvMode itself acts on the setting it is given; mapping the stored setting to the effective one is
// effectiveSetting()'s job (Auto is Manual until the Auto triggers are wired up).
#pragma once

#include <string>

namespace vj::tv {

enum class TvSetting { Off, Manual, Auto };

const char* settingKey(TvSetting s);                       // "off" | "manual" | "auto"
bool settingFromKey(const std::string& k, TvSetting& out); // false (out untouched) on an unknown key
TvSetting effectiveSetting(TvSetting stored);              // Auto -> Manual until the Auto triggers exist

struct TvConfig {
    TvSetting setting = TvSetting::Manual;
    bool autoFullscreen = true;   // Auto: entering fullscreen turns TV mode on, leaving turns it off
    bool autoController = true;   // Auto: a pad press while the desktop owns the input turns it on
};

class TvMode {
public:
    static constexpr float kMouseOffPx = 32.0f;            // mouse travel that hands a controller-started TV mode back
    enum class Cause { None, Manual, Fullscreen, Controller };

    // Applies a (re)loaded config and starts from off; Auto + autoFullscreen + fullscreenNow turns it on.
    void configure(const TvConfig& c, bool fullscreenNow);
    bool active() const { return on_; }
    Cause cause() const { return cause_; }

    void toggle();                           // View > TV mode, the in-game menu, C_TVMENU with no game; no-op when Off
    void setOn(bool on);                     // --tv, the remembered Manual state; no-op when Off
    void onFullscreen(bool fs);              // Auto + autoFullscreen: enter -> on, leave -> off
    void onPadPress(bool desktopOwnsInput);  // Auto + autoController + desktopOwnsInput -> on (Cause::Controller)
    void onMouseMove(float dx, float dy);    // adds to the travel; > kMouseOffPx -> off (see offByDesktopInput)
    void onNonNavKey();                      // same rule as the mouse

private:
    void turnOn(Cause c);
    void turnOff();
    bool autoActing(bool trigger) const { return cfg_.setting == TvSetting::Auto && trigger; }
    bool offByDesktopInput() const { return on_ && cause_ == Cause::Controller && !heldByFullscreen_; }

    TvConfig cfg_;
    bool on_ = false;
    Cause cause_ = Cause::None;
    bool heldByFullscreen_ = false;  // a controller-started TV mode that fullscreen then kept alive
    float mouseTravel_ = 0.0f;       // summed path length of the mouse since TV mode went on (or the last pad press)
};

// Who gets the input (the §3.1 table): the TV in-game menu beats everything; otherwise a running game owns its
// input; otherwise TV mode, when it is on, owns it, and the desktop UI gets what is left.
enum class Owner { DesktopUi, Game, TvUi };
Owner inputOwner(bool tvActive, bool gameRunning, bool menuOpen);

}  // namespace vj::tv
