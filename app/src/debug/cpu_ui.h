// The CPU debugger window (debug tools WP4): one window with three tabs -- 68000, GPU, DSP.  ImGui only;
// every decision (disassembly, flag decoding, step over, the temporary breakpoint) is in cpu_view.* and
// unit-tested.
//
// One window rather than three: the run controls (Break, Continue, Step, Step over, Run to cursor), the
// halted banner and the breakpoint list are shared by the three processors -- a halt stops the whole
// machine, and "step the GPU while the 68000 is parked" is a normal thing to do -- so splitting them would
// triplicate that chrome and the breakpoint table.  Each tab keeps its own view position, cursor and
// follow-PC setting.
//
// Wiring (App owns the call site):  debug.addProvider({registerCpuTools, drawCpuTools});
#pragma once

namespace vj {

class DebugSession;

// Registers the Tools-menu entry "cpu" (replacing the greyed placeholder), the halt hooks that open the
// window and select the halted processor's tab, and closes the window when the session switches the tools off.
void registerCpuTools(DebugSession& s);

// Draws the window if it is open.  Also runs while the machine is halted (inside App::haltedFrame()).
void drawCpuTools(DebugSession& s);

// Open state, for menu code that wants it directly.
bool& cpuWindowOpen();

// Opens the window on `target`'s tab with the disassembly at `addr` (for other tools: "disassemble here").
void showCpuAt(DebugSession& s, int target, unsigned addr);

}  // namespace vj
