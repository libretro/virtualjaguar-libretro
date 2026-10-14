// What the cheat scanner window does with a result: poke it, or keep it as a cheat.  Kept out of the
// ImGui file so the rules (hardcore, the 2 MB RAM limit, the exact code that reaches the core) are unit
// tested (app/tests/debug_scan_test.cpp) without a window.
#pragma once

#include <cstdint>
#include <string>

#include "cheats.h"
#include "debug_session.h"

namespace vj {

// The core-format code that holds `value` (as a `width`-byte big-endian number) at `addr`: what
// memoryCheatToCode makes of the bytes, e.g. (0x7D3D, 5, 1) -> "00007D3D:05".
std::string scanCheatCode(uint32_t addr, int64_t value, unsigned width);

// "Add as cheat": CheatManager::addUser, so the cheat shows up in the Cheats window, is saved with the
// game, and obeys the hardcore block (cm.blocked) like every other cheat.  Refused -- with `err` -- when the
// debug tools are off, hardcore is on, there is no game, or the value would not sit inside the 2 MB main RAM
// (cheats are writes into RAM only).  An empty `desc` becomes "Scanner 00xxxxxx".
bool scanAddCheat(CheatManager& cm, const DebugSession& session, uint32_t addr, int64_t value, unsigned width,
                  const std::string& desc, std::string& err);

// Double-click edit: writes the value through DebugSession::poke (the core's own poke policy).
bool scanPoke(DebugSession& session, uint32_t addr, int64_t value, unsigned width, std::string& err);

}  // namespace vj
