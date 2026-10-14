// The audio scope (Tools > Audio scope...): left / right waveforms and peak meters of what the core is
// producing.  App-only: it listens to the audio callback; the core is not involved.  Logic in audio_ring.*.
//
//   scopeToolInit(session, host)   once, AFTER the host's audio callback is wired (it chains onto it)
//   scopeToolDraw()                the window; the provider added by addDebugTools() calls it every UI frame
//
// While the window is closed the tap does nothing (one flag test per audio block).
#pragma once

namespace vj {

class CoreHost;
class DebugSession;

void scopeToolInit(DebugSession& session, CoreHost& host);
void scopeToolDraw();

}  // namespace vj
