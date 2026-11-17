#pragma once
// Only the SDL clock and event wake used by the production updater are stubbed.
// WinSparkle, Win32, the packaging script, and Inno Setup run unchanged.
#include <cstdint>
using Uint64 = std::uint64_t;
struct SDL_Event { unsigned type; };
inline constexpr unsigned SDL_EVENT_QUIT = 0x100;
bool SDL_PushEvent(SDL_Event* event);
Uint64 SDL_GetTicks();
