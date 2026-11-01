#include "gl_context.h"
#include <cstdlib>

int main() {
    struct Case { const char* version; bool supported; };
    const Case cases[] = {{nullptr, false}, {"", false}, {"1.1.0", false}, {"2.1 Mesa", false},
        {"3.2", false}, {"3.3 (Core Profile) Mesa", true}, {"4.6.0", true},
        {"OpenGL ES 3.3", false}, {"garbage", false}};
    for (const auto& c : cases) {
        if (vj::supportsDesktopGL33(c.version) != c.supported) {
            std::fprintf(stderr, "FAIL GL version gate: %s\n", c.version ? c.version : "null");
            return 1;
        }
    }
    std::puts("PASS desktop GL 3.3 gate (including SDL's legacy-context fallback)");
}
