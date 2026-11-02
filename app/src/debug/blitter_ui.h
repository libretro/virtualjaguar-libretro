// The Blitter inspector (Tools > Blitter log...): the last launches the core logged, decoded, plus
// the live blitter registers.  Logic in blitter_log.*.
//
//   blitterToolInit(session)   once: registers the Tools entry "blitter" and its draw hook
//   blitterToolDraw()          the window; the provider added by addDebugTools() calls it every UI frame (cheap when closed)
//
// The core's launch log is only switched on while the window is open (and the tools are enabled).  The list and the
// registers come from published view buffers (blitter_feed.*); the window never calls the core's interface.
#pragma once

namespace vj {

class DebugSession;

void blitterToolInit(DebugSession& session);
void blitterToolDraw();

// For the headless smoke test (app/tests/debug_blitter_ui_smoke.cpp).
namespace blitter_test {
unsigned launchesSeen();   // launches the window's list has seen (the tracker's total)
bool logOn();              // the window switched the core's log on
}  // namespace blitter_test

}  // namespace vj
