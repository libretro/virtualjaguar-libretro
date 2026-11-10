#pragma once
#include <cctype>
#include <cstdlib>
#include <string>

namespace vj {
// Windows command-line quoting, including empty arguments and trailing backslashes.
inline std::wstring quoteSoftwareGLArgument(const std::wstring& arg) {
    std::wstring out = L"\"";
    size_t slashes = 0;
    for (wchar_t c : arg) {
        if (c == L'\\') { ++slashes; continue; }
        out.append(slashes * (c == L'"' ? 2 : 1), L'\\');
        slashes = 0;
        if (c == L'"') out += L'\\';
        out += c;
    }
    out.append(slashes * 2, L'\\');
    return out + L'"';
}
// True when `path` names the opengl32.dll of the bundled Mesa: the file staged beside the app in software-opengl/
// (app/packaging/windows/stage-software-gl.py), i.e. a last component `opengl32.dll` directly under a component
// `software-opengl`, either separator, any case.  What SDL_OPENGL_LIBRARY names in the app's fallback child and in the
// GL test programs under the Windows GL jobs.  Pure (no OS calls), so the unit test runs it everywhere.
inline bool bundledSoftwareGLLibrary(const std::string& path) {
    auto lower = [](std::string s) {
        for (char& c : s) c = (char)std::tolower((unsigned char)c);
        return s;
    };
    size_t slash = path.find_last_of("\\/");
    if (slash == std::string::npos || slash == 0 || lower(path.substr(slash + 1)) != "opengl32.dll") return false;
    size_t prev = path.find_last_of("\\/", slash - 1);
    size_t start = prev == std::string::npos ? 0 : prev + 1;
    return lower(path.substr(start, slash - start)) == "software-opengl";
}
#ifdef _WIN32
// Called only after the failed App is destroyed and its instance lock is released.
// Runs at most once and returns the child exit status, preserving CLI/wait semantics.
bool retrySoftwareOpenGL(int argc, char** argv, const std::string& dataDir, int& exitCode);
// True in the copy that retrySoftwareOpenGL() launched beside the bundled Mesa (VJ_SOFTWARE_OPENGL_RETRY is set).
bool softwareGLChild();
// True when this process renders on the bundled Mesa: softwareGLChild(), or SDL_OPENGL_LIBRARY (the library SDL loaded
// for the GL context) names a software-opengl/opengl32.dll -- the GL test programs run that way by windows-gl-test.yml,
// gl-qualification.yml and app-package.yml (#1179).  The environment, not GetModuleHandle("opengl32.dll"): gdi32 loads
// the system opengl32.dll too (pixel-format calls), so the module list can hold both and name resolution is ambiguous.
bool usingBundledSoftwareGL();
// Ends the process with `exitCode` WITHOUT unloading the bundled Mesa (#1167, #1179).  SDL_DestroyWindow / SDL_Quit
// would FreeLibrary it, and the MINGW64 (x64) Mesa's DllMain then waits, under the loader lock, for llvmpipe's rasterizer
// threads, which never finish: the process lingered forever (the app's parent waiting on it, the next start instance 2,
// a test run killed by its timeout after printing its verdict).  Flushes stdio and terminates the process: Windows
// reclaims the window, the threads and the instance lock; nothing that must be persisted may be pending (the app calls
// it from App::shutdown() after the config is saved and the core unloaded; the test programs after their verdict).
[[noreturn]] void exitWithoutUnloadingGL(int exitCode);
#else
inline bool retrySoftwareOpenGL(int, char**, const std::string&, int&) { return false; }
inline bool softwareGLChild() { return false; }
inline bool usingBundledSoftwareGL() { return false; }
[[noreturn]] inline void exitWithoutUnloadingGL(int exitCode) { std::_Exit(exitCode); }
#endif
}
