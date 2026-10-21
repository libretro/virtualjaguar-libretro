// The Memory viewer/editor and Palette (CLUT) windows (debug tools WP2).  ImGui only; every decision
// (formatting, the write policy, search, colour maths) is in memory_view.* and unit-tested.
//
// Wiring (the Debug UI / App owns the call sites):
//   once, after the DebugSession exists:        registerMemoryTools(session);
//   every UI frame, next to the other windows:  drawMemoryTools(session, &app.cheats);
// DebugSession::tick() must also run once per frame (it drives the 10 Hz refresh the windows follow).
#pragma once

#include <cstdint>

namespace vj {

class DebugSession;
class CheatManager;

// Registers the Tools-menu entries "memory" and "clut" (replacing the greyed placeholders) and closes
// both windows when the session switches the tools off (hardcore, content closed, no core).
void registerMemoryTools(DebugSession& s);

// Draws whichever of the two windows is open.  `cheats` may be null (then "Add as cheat" is not offered).
void drawMemoryTools(DebugSession& s, CheatManager* cheats);

// Individual windows (drawMemoryTools calls these).
void drawMemoryWindow(DebugSession& s, CheatManager* cheats);
void drawClutWindow(DebugSession& s);

// Open state, for menu code that wants it directly.
bool& memoryWindowOpen();
bool& clutWindowOpen();

// Opens the Memory viewer on `addr` (for other tools: "show this in memory", a register value, a pointer).
void showMemoryAt(DebugSession& s, unsigned addr);

// Test hook (debug_memory_ui_smoke): a byte of the window the grid last adopted from its published view.
namespace memory_test {
bool byteAt(unsigned addr, uint8_t& v);
}

}  // namespace vj
