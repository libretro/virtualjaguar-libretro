#pragma once

#include <string>

namespace vj::windows_portable_updates {

// UI-thread entry points. A check reads release metadata only.
bool start();
std::string poll();
void stop();

}  // namespace vj::windows_portable_updates
