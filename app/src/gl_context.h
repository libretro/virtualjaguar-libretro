#pragma once
#include <cstdio>

namespace vj {
// SDL's WGL backend can return a legacy context even when 3.3 core was requested.
inline bool supportsDesktopGL33(const char* version) {
    int major = 0, minor = 0;
    return version && std::sscanf(version, "%d.%d", &major, &minor) == 2 &&
           (major > 3 || (major == 3 && minor >= 3));
}
// Requires a current SDL context. Reports the actual driver and sets SDL's error on failure.
bool initOpenGLLoader();
}
