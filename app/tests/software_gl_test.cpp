#include "software_gl.h"
#include <cstdio>
#include <cstdlib>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#endif

// The bundled-Mesa path detection (#1179) is pure string logic: the same table on every platform.
static bool bundledLibraryCases() {
    struct Case { const char* path; bool bundled; };
    const Case cases[] = {
        {"C:\\Program Files\\Virtual Jaguar\\software-opengl\\opengl32.dll", true},
        {"C:/VirtualJaguar/software-opengl/opengl32.dll", true},
        {"D:\\a\\stage\\bin\\SOFTWARE-OPENGL\\OpenGL32.DLL", true},   // any case: Windows file names
        {"software-opengl\\opengl32.dll", true},                         // relative, nothing above the folder
        {"software-opengl/opengl32.dll", true},
        {"C:\\Windows\\System32\\opengl32.dll", false},                 // the system library
        {"C:\\Windows\\SysWOW64\\opengl32.dll", false},
        {"C:\\VirtualJaguar\\software-opengl\\libgallium_wgl.dll", false},   // the right folder, another file
        {"C:\\VirtualJaguar\\software-opengl\\", false},                      // the folder itself
        {"C:\\VirtualJaguar\\software-opengl", false},
        {"C:\\mesa\\my-software-opengl\\opengl32.dll", false},         // a folder that only ends in the name
        {"C:\\software-opengl-old\\opengl32.dll", false},
        {"C:\\software-opengl\\sub\\opengl32.dll", false},             // not directly under it
        {"opengl32.dll", false},                                         // bare name: SDL takes the system one
        {"", false},
        {"/", false},
    };
    bool ok = true;
    for (const Case& c : cases) {
        if (vj::bundledSoftwareGLLibrary(c.path) != c.bundled) {
            std::printf("FAIL bundledSoftwareGLLibrary(\"%s\") should be %s\n", c.path, c.bundled ? "true" : "false");
            ok = false;
        }
    }
    return ok;
}

int main() {
    if (!bundledLibraryCases()) return 1;
    const std::vector<std::wstring> args = {L"", L"plain", L"path with spaces", L"C:\\", L"C:\\folder\\", L"quoted \"value\"", L"slashes\\\\\"quote", L"Unicode \u2603"};
#ifdef _WIN32
    std::wstring command = L"program.exe";
    for (const auto& arg : args) command += L" " + vj::quoteSoftwareGLArgument(arg);
    int count = 0;
    wchar_t** parsed = CommandLineToArgvW(command.c_str(), &count);
    bool ok = parsed && count == (int)args.size() + 1;
    if (ok) for (size_t i = 0; i < args.size(); ++i) ok = ok && args[i] == parsed[i + 1];
    if (parsed) LocalFree(parsed);
    if (!ok) { std::puts("FAIL Windows argument roundtrip"); return 1; }
    int exitCode = 123;
    SetEnvironmentVariableW(L"SDL_OPENGL_LIBRARY", nullptr);
    if (vj::softwareGLChild()) { std::puts("FAIL not the Mesa child without VJ_SOFTWARE_OPENGL_RETRY"); return 1; }
    if (vj::usingBundledSoftwareGL()) { std::puts("FAIL not on the bundled Mesa with neither variable set (#1179)"); return 1; }
    SetEnvironmentVariableW(L"VJ_SOFTWARE_OPENGL_RETRY", L"1");
    if (!vj::softwareGLChild()) { std::puts("FAIL the Mesa child is recognised by VJ_SOFTWARE_OPENGL_RETRY (#1167)"); return 1; }
    if (!vj::usingBundledSoftwareGL()) { std::puts("FAIL the Mesa child is on the bundled Mesa (#1179)"); return 1; }
    if (vj::retrySoftwareOpenGL(0, nullptr, "", exitCode) || exitCode != 123) {
        std::puts("FAIL software retry loop guard"); return 1;
    }
    SetEnvironmentVariableW(L"VJ_SOFTWARE_OPENGL_RETRY", nullptr);
    if (vj::softwareGLChild()) { std::puts("FAIL still the Mesa child after the variable is cleared"); return 1; }
    // The GL test programs' way onto the bundled Mesa: SDL_OPENGL_LIBRARY alone, naming the staged file (#1179).
    SetEnvironmentVariableW(L"SDL_OPENGL_LIBRARY", L"D:\\a\\stage\\bin\\software-opengl\\opengl32.dll");
    if (!vj::usingBundledSoftwareGL()) { std::puts("FAIL SDL_OPENGL_LIBRARY naming software-opengl\\opengl32.dll is the bundled Mesa (#1179)"); return 1; }
    SetEnvironmentVariableW(L"SDL_OPENGL_LIBRARY", L"C:\\Windows\\System32\\opengl32.dll");
    if (vj::usingBundledSoftwareGL()) { std::puts("FAIL SDL_OPENGL_LIBRARY naming the system opengl32.dll is not the bundled Mesa"); return 1; }
    SetEnvironmentVariableW(L"SDL_OPENGL_LIBRARY", L"\u00e9t\u00e9\\software-opengl\\opengl32.dll");   // non-ASCII elsewhere in the path
    if (!vj::usingBundledSoftwareGL()) { std::puts("FAIL a non-ASCII prefix does not hide the bundled Mesa"); return 1; }
    SetEnvironmentVariableW(L"SDL_OPENGL_LIBRARY", nullptr);
    if (vj::usingBundledSoftwareGL()) { std::puts("FAIL still on the bundled Mesa after SDL_OPENGL_LIBRARY is cleared"); return 1; }
    // This unit binary's build directory has no staged software-opengl app.
    if (vj::retrySoftwareOpenGL(0, nullptr, "", exitCode) || exitCode != 123) {
        std::puts("FAIL missing runtime should not launch a child"); return 1;
    }
#else
    if (vj::quoteSoftwareGLArgument(L"C:\\") != L"\"C:\\\\\"" || vj::quoteSoftwareGLArgument(L"") != L"\"\"") {
        std::puts("FAIL Windows trailing-backslash quoting"); return 1;
    }
    // Off Windows nothing is the bundled Mesa, whatever the environment says: the normal SDL_Quit path always runs.
    setenv("VJ_SOFTWARE_OPENGL_RETRY", "1", 1);
    setenv("SDL_OPENGL_LIBRARY", "/opt/vj/software-opengl/opengl32.dll", 1);
    if (vj::softwareGLChild() || vj::usingBundledSoftwareGL()) { std::puts("FAIL the bundled Mesa exists only on Windows"); return 1; }
    unsetenv("VJ_SOFTWARE_OPENGL_RETRY");
    unsetenv("SDL_OPENGL_LIBRARY");
#endif
    std::puts("PASS software graphics arguments, retry guards and bundled-Mesa detection");
}
