// TV mode (#991): the Library screen -- a stub for now (one line of text); the rail and the grid come with Task 9.
#include "theme.h"
#include "tv/tv_screen.h"
#include "tv/tv_shell.h"

namespace vj::tv {

namespace {

class LibraryScreen : public Screen {
public:
    explicit LibraryScreen(Host& h) : host_(h) {}
    const char* id() const override { return "library"; }
    void intent(Intent) override {}
    void draw(const DrawCtx& c) override {
        widgets::text(c.dl, c.m.titleSmall, ImVec2(c.pos.x + c.m.safeX, c.pos.y), widgets::col(theme::pal().text), "Library");
    }
    std::vector<widgets::Hint> hints() const override { return {{host_.glyph(Intent::Back), "Back"}}; }

private:
    Host& host_;
};

}  // namespace

std::unique_ptr<Screen> makeLibraryScreen(Host& h, Shell&) { return std::make_unique<LibraryScreen>(h); }

}  // namespace vj::tv
