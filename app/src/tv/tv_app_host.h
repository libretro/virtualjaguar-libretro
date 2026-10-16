// TV mode (#991): App's implementation of tv::Host.  The only file pair of the TV module that touches App.
#pragma once

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

private:
    App& app_;
};

}  // namespace tv
}  // namespace vj
