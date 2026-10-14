// Windows GUI-subsystem builds (VJ_APP_WIN32_GUI): a double-clicked exe must not
// open a console window, but `virtualjaguar --help`, `--version` and the CTest smoke
// tests still need stdout.  When the process was started with no usable standard
// output (launched from a console, no redirection), attach to the parent's console
// and point stdio at it.  Redirected output (pipes, files, CTest) is left alone.
#ifdef _WIN32
#include <windows.h>
#include <cstdio>

namespace {
struct ConsoleAttach {
    ConsoleAttach() {
        HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
        if (out != nullptr && out != INVALID_HANDLE_VALUE) return;  // redirected: fine
        if (!AttachConsole(ATTACH_PARENT_PROCESS)) return;          // double-click: no console
        FILE* f = nullptr;
        freopen_s(&f, "CONOUT$", "w", stdout);
        freopen_s(&f, "CONOUT$", "w", stderr);
        freopen_s(&f, "CONIN$", "r", stdin);
    }
} g_consoleAttach;
}  // namespace
#endif
