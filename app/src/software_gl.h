#pragma once
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
#ifdef _WIN32
// Called only after the failed App is destroyed and its instance lock is released.
// Runs at most once and returns the child exit status, preserving CLI/wait semantics.
bool retrySoftwareOpenGL(int argc, char** argv, const std::string& dataDir, int& exitCode);
// True in the copy that retrySoftwareOpenGL() launched beside the bundled Mesa (VJ_SOFTWARE_OPENGL_RETRY is set).
bool softwareGLChild();
// Ends that copy with `exitCode` WITHOUT unloading the bundled Mesa (#1167).  SDL_DestroyWindow / SDL_Quit would
// FreeLibrary it, and the MINGW64 Mesa's DllMain then waits, under the loader lock, for llvmpipe's rasterizer threads,
// which never finish: the process (and the parent waiting on it) lingered forever, and the next start was instance 2.
// Flushes stdio and terminates the process: Windows reclaims the window, the threads and the instance lock; nothing
// that must be persisted may be pending (App::shutdown() has saved the config and unloaded the core before this).
[[noreturn]] void exitSoftwareGLChild(int exitCode);
#else
inline bool retrySoftwareOpenGL(int, char**, const std::string&, int&) { return false; }
inline bool softwareGLChild() { return false; }
[[noreturn]] inline void exitSoftwareGLChild(int exitCode) { std::_Exit(exitCode); }
#endif
}
