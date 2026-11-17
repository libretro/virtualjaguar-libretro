#pragma once

namespace vj::windows_updates {

// All entry points run on the UI thread. The shutdown predicate is shared with
// WinSparkle's worker. Pass false while any game is loaded or being opened.
inline constexpr char kBackgroundPreference[] = "updates.windows.background";
bool start(bool backgroundChecks);
bool available();
bool shutdownRequested();
void setBackgroundChecks(bool enabled);
void tick();
void check();
bool setCanInstall(bool allowed);
void stop();

} // namespace vj::windows_updates
