#include "platform/viewports.h"

#include <cstring>

namespace vj {
namespace viewports {

bool defaultEnabled() {
#if defined(__APPLE__) || defined(_WIN32)
    return true;
#else
    return false;
#endif
}

bool driverIsWayland(const char* d) { return d && std::strcmp(d, "wayland") == 0; }

bool driverIsDummy(const char* d) {
    return !d || !*d || std::strcmp(d, "dummy") == 0 || std::strcmp(d, "offscreen") == 0 || std::strcmp(d, "evdev") == 0;
}

Decision decide(bool userWants, const char* videoDriver, bool fullscreen) {
    Decision r;
    if (!userWants) {
        r.why = Why::UserOff;
    } else if (driverIsDummy(videoDriver)) {
        r.why = Why::NoWindowSystem;
    } else if (driverIsWayland(videoDriver)) {
        r.why = Why::Wayland;
    } else if (fullscreen) {
        r.why = Why::Fullscreen;
    } else {
        r.active = true;
        r.why = Why::Active;
    }
    return r;
}

const char* explain(Why why) {
    switch (why) {
    case Why::Wayland:
        return "Not available on Wayland: a program cannot position its own extra windows there, so they stay inside "
               "the main window. Starting with SDL_VIDEODRIVER=x11 (XWayland) turns this on.";
    case Why::Fullscreen: return "Windows stay inside the main window while it is fullscreen.";
    case Why::NoWindowSystem: return "There is no window system to make extra windows with.";
    default: return "";
    }
}

}  // namespace viewports
}  // namespace vj
