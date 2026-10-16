// TV mode (#991): App's implementation of tv::Host.  The only file pair of the TV module that touches App.
#pragma once

#include <SDL3/SDL.h>

#include "tv/tv_host.h"

namespace vj {

class App;

namespace tv {

class AppHost : public Host {
public:
    explicit AppHost(App& a) : app_(a) {}

    double now() const override;
    bool gameLoaded() const override;
    std::string gameTitle() const override;
    bool menuPause(std::string& why) override;
    void menuResume() override;
    bool netplayActive() const override;
    bool canLoadState() const override;
    std::vector<SlotInfo> slots() override;
    bool saveState(int slot) override;
    bool loadState(int slot) override;
    void reset() override;
    void closeGame() override;
    void toggleControlsOverlay() override;
    void armReleaseGuard() override;
    void clearHolds() override;
    void toast(const std::string& text) override;
    std::string glyph(Intent i) const override;

    // Every SDL event, before anything can consume it (App::handleEvent): remembers which device was used last, so
    // glyph() follows what the player is actually pressing -- including the buttons the shell itself consumes, which
    // never reach InputManager (its lastInputWasPad would go stale).
    void noteInput(const SDL_Event& e);

private:
    App& app_;
    bool lastWasPad_ = false;        // the last real input was a pad button / a pushed stick, not a key
    SDL_JoystickID lastPad_ = 0;     // ... on this pad
};

}  // namespace tv
}  // namespace vj
