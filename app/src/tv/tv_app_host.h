// TV mode (#991): App's implementation of tv::Host.  The only file pair of the TV module that touches App.
#pragma once

#include <SDL3/SDL.h>

#include "tv/tv_host.h"

namespace vj {

class App;
namespace lib { struct Entry; }

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
    void showControlsOverlay() override;
    void armReleaseGuard() override;
    void clearHolds() override;
    void toast(const std::string& text) override;
    std::string glyph(Intent i) const override;
    Intent padIntent(uint32_t pad, int sdlButton) const override;
    Intent padTriggerIntent(uint32_t pad, int sdlAxis) const override;
    bool tvActive() const override;
    bool tvAvailable() const override;
    void setTvActive(bool on) override;
    std::string clockText() const override;
    std::string raStatus() const override;
    bool desktopWindowOpen() const override;
    bool libraryScanning() const override;
    std::vector<GameRef> games(List l) override;
    Art art(const GameRef& g) override;
    std::vector<StateRef> states(const GameRef& g) override;
    Art stateThumb(const GameRef& g, const StateRef& state) override;
    void launch(const GameRef& g) override;
    void launchState(const GameRef& g, const StateRef& state) override;
    bool isFavorite(const GameRef& g) override;
    void setFavorite(const GameRef& g, bool on) override;

    // Every SDL event, before anything can consume it (App::handleEvent): remembers which device was used last, so
    // glyph() follows what the player is actually pressing -- including the buttons the shell itself consumes, which
    // never reach InputManager (its lastInputWasPad would go stale).
    void noteInput(const SDL_Event& e);

private:
    const lib::Entry* entryFor(const GameRef& g) const;

    App& app_;
    bool lastWasPad_ = false;        // the last real input was a pad button / a pushed stick, not a key
    SDL_JoystickID lastPad_ = 0;     // ... on this pad
};

}  // namespace tv
}  // namespace vj
