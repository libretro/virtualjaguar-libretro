#pragma once

namespace vj {
namespace sparkle {

// Main-thread calls. Start after Cocoa initialization, then report whether a game is loaded.
bool start();
void setGameLoaded(bool loaded);
void check();
bool backgroundChecks();
void setBackgroundChecks(bool enabled);

}
}  // namespace vj
