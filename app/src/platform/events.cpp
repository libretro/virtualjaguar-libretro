// Native-menu / tray clicks as SDL events (platform.h).  SDL only, no OS: the unit test links this.
#include <cstdint>

#include "platform/platform.h"

namespace vj {
namespace platform {

void postCommand(Cmd cmd, int arg) {
    SDL_Event ev;
    SDL_zero(ev);
    ev.type = SDL_EVENT_USER;
    ev.user.code = kCommandEventCode;
    ev.user.data1 = (void*)(intptr_t)(int)cmd;
    ev.user.data2 = (void*)(intptr_t)arg;
    SDL_PushEvent(&ev);
}

bool decodeCommandEvent(const SDL_Event& e, Cmd& cmd, int& arg) {
    if (e.type != SDL_EVENT_USER || e.user.code != kCommandEventCode) return false;
    int c = (int)(intptr_t)e.user.data1;
    if (c <= (int)Cmd::None || c >= (int)Cmd::Count_) return false;
    cmd = (Cmd)c;
    arg = (int)(intptr_t)e.user.data2;
    return true;
}

}  // namespace platform
}  // namespace vj
