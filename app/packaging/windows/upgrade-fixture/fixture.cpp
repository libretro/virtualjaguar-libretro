#include "SDL3/SDL.h"
#include "winsparkle_config.h"
#include "winsparkle_updates.h"

#include <windows.h>
#include <filesystem>
#include <iostream>
#include <string_view>

bool SDL_PushEvent(SDL_Event*) { return true; }
Uint64 SDL_GetTicks() { return GetTickCount64(); }

int main(int argc, char** argv)
{
    using namespace vj::windows_updates;
    if (argc != 2)
        return 2;
    if (std::string_view(argv[1]) == "--version") {
        std::wcout << VJ_WINSPARKLE_BUILD_VERSION << '\n';
        return 0;
    }
    if (std::string_view(argv[1]) != "--check" || !start(false))
        return 3;
    const auto block = std::filesystem::current_path() / "fixture-playing";
    setCanInstall(!std::filesystem::exists(block));
    check();
    for (unsigned tick = 0; tick < 2400 && !shutdownRequested(); ++tick) {
        setCanInstall(!std::filesystem::exists(block));
        Sleep(100);
    }
    const bool updated = shutdownRequested();
    stop();
    return updated ? 0 : 4;
}
