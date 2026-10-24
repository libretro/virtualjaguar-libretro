// Windows: a dark title bar (the UI is dark), the AppUserModelID that groups the taskbar button and
// its Jump List with the installer's shortcut, "Recent" in the Jump List / Explorer, Show in Explorer.
//
// Everything that is not in every Windows SDK / MinGW header set is looked up at run time
// (GetProcAddress), so the same file builds with MSVC and MinGW-w64 without extra link libraries.
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>

#include <cstdio>

#include "platform/platform_os.h"

namespace vj {
namespace platform {
namespace os {

namespace {

// Same value as the installer's [Icons] AppUserModelID, so the taskbar pin, the running window and
// the Jump List are one thing.
const wchar_t kAppUserModelId[] = L"io.github.libretro.VirtualJaguar";

std::wstring widen(const std::string& s) {
    if (s.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    if (n <= 0) return std::wstring();
    std::wstring w((size_t)n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
    return w;
}

}  // namespace

void initNetworking() {
    static bool done = false;
    if (done) return;
    done = true;
    WSADATA wsa;
    const int rc = WSAStartup(MAKEWORD(2, 2), &wsa);
    if (rc != 0) std::fprintf(stderr, "[app] WSAStartup failed (%d): the core's sockets (GDB stub, netplay) will not open\n", rc);
}

void beforeWindow() {
    typedef HRESULT(WINAPI * SetAumidFn)(PCWSTR);
    HMODULE shell = LoadLibraryW(L"shell32.dll");
    if (!shell) return;
    SetAumidFn fn = (SetAumidFn)(void*)GetProcAddress(shell, "SetCurrentProcessExplicitAppUserModelID");
    if (fn) fn(kAppUserModelId);
}

namespace {
HWND g_hwnd = nullptr;
}

void windowCreated(SDL_Window* w) {
    if (!w) return;
    g_hwnd = (HWND)SDL_GetPointerProperty(SDL_GetWindowProperties(w), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
}

void setDarkChrome(bool dark) {
    HWND hwnd = g_hwnd;
    if (!hwnd) return;
    // DWMWA_USE_IMMERSIVE_DARK_MODE is 20 (Windows 10 2004+ and 11); 19 on the first builds that had it.
    // Follows theme::isDark() (the app's theme, which may itself follow the system), not the system alone.
    typedef HRESULT(WINAPI * DwmSetFn)(HWND, DWORD, LPCVOID, DWORD);
    HMODULE dwm = LoadLibraryW(L"dwmapi.dll");
    if (!dwm) return;
    DwmSetFn set = (DwmSetFn)(void*)GetProcAddress(dwm, "DwmSetWindowAttribute");
    if (!set) return;
    const BOOL on = dark ? TRUE : FALSE;
    if (FAILED(set(hwnd, 20, &on, sizeof on))) set(hwnd, 19, &on, sizeof on);
    // Make the non-client area repaint now rather than on the next resize.
    SetWindowPos(hwnd, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
}

bool installMenus() { return false; }   // the in-window menu bar is the Windows menu bar here
void renderMenus(const MenuBar&, const std::vector<MenuItem>&) {}

bool displayAsleep(SDL_Window*) { return false; }

void noteRecent(const std::string& path) {
    // Explorer's Recent and the taskbar Jump List's "Recent" category (the latter once the installer
    // has registered the file types, so Windows knows this program opens them).
    std::wstring w = widen(path);
    if (!w.empty()) SHAddToRecentDocs(SHARD_PATHW, w.c_str());
}

void clearRecents() {
    // Deliberately nothing: SHAddToRecentDocs(SHARD_PIDL, NULL) clears the user's whole Recent items list, not just
    // this program's, and the in-window "Clear list" only clears the app's own list.
}

void setActivity(bool) {}

bool revealFile(const std::string& path) {
    std::wstring w = widen(path);
    if (w.empty()) return false;
    for (wchar_t& c : w)
        if (c == L'/') c = L'\\';
    std::wstring args = L"/select,\"" + w + L"\"";
    return (INT_PTR)ShellExecuteW(nullptr, L"open", L"explorer.exe", args.c_str(), nullptr, SW_SHOWNORMAL) > 32;
}

const char* fileManagerName() { return "Explorer"; }

int dumpMenus() {
    std::printf("native-menu: SKIP no native menu bar on this OS (the in-window menu is used)\n");
    return 77;
}

void shutdown() {}

}  // namespace os
}  // namespace platform
}  // namespace vj

#endif  // _WIN32
