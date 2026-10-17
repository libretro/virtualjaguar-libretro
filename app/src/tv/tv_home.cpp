// TV mode (#991): the Home screen -- a stub for now (one line of text); the hero and the shelves come with Task 8.
#include "theme.h"
#include "tv/tv_screen.h"
#include "tv/tv_shell.h"

namespace vj::tv {

namespace {

class HomeScreen : public Screen {
public:
    const char* id() const override { return "home"; }
    void intent(Intent) override {}
    void draw(const DrawCtx& c) override {
        widgets::text(c.dl, c.m.titleSmall, ImVec2(c.pos.x + c.m.safeX, c.pos.y), widgets::col(theme::pal().text), "Home");
    }
    std::vector<widgets::Hint> hints() const override { return {}; }   // Back on Home does nothing: no way out to hint
};

}  // namespace

std::unique_ptr<Screen> makeHomeScreen(Host&, Shell&) { return std::make_unique<HomeScreen>(); }

}  // namespace vj::tv
