#include "tv/tv_mode.h"

#include <cmath>

namespace vj::tv {

const char* settingKey(TvSetting s) {
    switch (s) {
        case TvSetting::Off: return "off";
        case TvSetting::Manual: return "manual";
        case TvSetting::Auto: return "auto";
    }
    return "manual";
}

bool settingFromKey(const std::string& k, TvSetting& out) {
    if (k == "off") out = TvSetting::Off;
    else if (k == "manual") out = TvSetting::Manual;
    else if (k == "auto") out = TvSetting::Auto;
    else return false;
    return true;
}

TvSetting effectiveSetting(TvSetting stored) {
    return stored == TvSetting::Auto ? TvSetting::Manual : stored;
}

void TvMode::turnOn(Cause c) {
    on_ = true;
    cause_ = c;
    heldByFullscreen_ = false;
    mouseTravel_ = 0.0f;
}

void TvMode::turnOff() {
    on_ = false;
    cause_ = Cause::None;
    heldByFullscreen_ = false;
    mouseTravel_ = 0.0f;
}

void TvMode::configure(const TvConfig& c, bool fullscreenNow) {
    cfg_ = c;
    turnOff();
    if (autoActing(cfg_.autoFullscreen) && fullscreenNow) turnOn(Cause::Fullscreen);
}

void TvMode::toggle() {
    if (cfg_.setting == TvSetting::Off) return;
    if (on_) turnOff();              // a manual off wins until the next fullscreen or pad edge
    else turnOn(Cause::Manual);
}

void TvMode::setOn(bool on) {
    if (cfg_.setting == TvSetting::Off) return;
    if (on == on_ && (!on || cause_ == Cause::Manual)) return;
    if (on) turnOn(Cause::Manual);   // an explicit request is Manual even when a trigger had it on
    else turnOff();
}

void TvMode::onFullscreen(bool fs) {
    if (!autoActing(cfg_.autoFullscreen)) return;
    if (fs) {
        if (!on_) turnOn(Cause::Fullscreen);
        else if (cause_ == Cause::Controller) heldByFullscreen_ = true;   // controller-on, then fullscreen holds it
    } else if (on_) {
        if (cause_ == Cause::Fullscreen) turnOff();
        else if (cause_ == Cause::Controller) heldByFullscreen_ = false;  // the pad still owns it: only the hold goes
    }
}

void TvMode::onPadPress(bool desktopOwnsInput) {
    if (!autoActing(cfg_.autoController)) return;
    if (on_) {
        if (cause_ == Cause::Controller) mouseTravel_ = 0.0f;             // the pad is in use again: fresh budget
        return;
    }
    if (desktopOwnsInput) turnOn(Cause::Controller);
}

void TvMode::onMouseMove(float dx, float dy) {
    if (!offByDesktopInput()) return;
    mouseTravel_ += std::sqrt(dx * dx + dy * dy);
    if (mouseTravel_ > kMouseOffPx) turnOff();
}

void TvMode::onNonNavKey() {
    if (offByDesktopInput()) turnOff();
}

Owner inputOwner(bool tvActive, bool gameRunning, bool menuOpen) {
    if (menuOpen) return Owner::TvUi;
    if (gameRunning) return Owner::Game;
    return tvActive ? Owner::TvUi : Owner::DesktopUi;
}

}  // namespace vj::tv
