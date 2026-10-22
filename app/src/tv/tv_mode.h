// TV mode (#991): when TV mode is on, and who owns the input.  Pure state, no SDL and no ImGui: the app feeds it
// the events (a fullscreen change, a pad press, mouse travel, a keyboard key that is not navigation) and reads back
// whether TV mode is active.
//
// Three stored settings: Off (never), Manual (only the toggle), Auto (also on fullscreen and on a controller press).
// TvMode itself acts on the setting it is given; defaultSetting() is what a config with no stored setting gets.
#pragma once

#include <string>

namespace vj::tv {

enum class TvSetting { Off, Manual, Auto };

const char* settingKey(TvSetting s);                       // "off" | "manual" | "auto"
bool settingFromKey(const std::string& k, TvSetting& out); // false (out untouched) on an unknown key
// The setting a config with no ui.tv_mode starts from: the build's (VJ_TV_DEFAULT_SETTING: "off" | "manual" | "auto").
// Null or anything else is Auto.  Never written back, so a build can move its default under people who never chose.
TvSetting defaultSetting(const char* buildDefault);

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
    // onFullscreen and onPadPress are fed edges (a fullscreen change, a pad press), but a repeated onFullscreen(true)
    // still re-applies: it turns TV mode back on after a manual off (the tests rely on it).  The current fullscreen
    // state is recorded whatever the setting.
    void onFullscreen(bool fs);              // Auto + autoFullscreen: enter -> on, leave -> off whatever the cause
    void onPadPress(bool desktopOwnsInput);  // Auto + autoController + desktopOwnsInput -> on (Cause::Controller)
    // The mouse and the keyboard end a Controller-caused TV mode: the mouse once its summed path length passes
    // kMouseOffPx, a non-nav key at once.  Never while Auto + autoFullscreen + fullscreen holds it, and never for a
    // Manual or Fullscreen cause.  Every turn-on resets the travel (so does a pad press while a controller has it on).
    void onMouseMove(float dx, float dy);
    void onNonNavKey();

private:
    void turnOn(Cause c);
    void turnOff();
    bool autoActing(bool trigger) const { return cfg_.setting == TvSetting::Auto && trigger; }
    bool fullscreenHolds() const { return cfg_.setting == TvSetting::Auto && cfg_.autoFullscreen && fs_; }
    bool offByDesktopInput() const { return on_ && cause_ == Cause::Controller && !fullscreenHolds(); }

    TvConfig cfg_;
    bool on_ = false;
    Cause cause_ = Cause::None;
    bool fs_ = false;                // the window is fullscreen right now (configure, then every onFullscreen)
    float mouseTravel_ = 0.0f;       // summed path length of the mouse since TV mode went on (or the last pad press)
};

// Who gets the input (the §3.1 table): the TV in-game menu beats everything; otherwise a running game owns its
// input; otherwise TV mode, when it is on, owns it, and the desktop UI gets what is left.
enum class Owner { DesktopUi, Game, TvUi };
Owner inputOwner(bool tvActive, bool gameRunning, bool menuOpen);

}  // namespace vj::tv
