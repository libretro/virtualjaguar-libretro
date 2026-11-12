#pragma once

namespace vj::windows_updates {

// All entry points run on the UI thread. The shutdown predicate is shared with
// WinSparkle's worker. Pass false while any game is loaded or being opened.
bool start(bool backgroundChecks);
void setBackgroundChecks(bool enabled);
void check();
bool setCanInstall(bool allowed);
void stop();

} // namespace vj::windows_updates
