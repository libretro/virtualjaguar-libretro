#pragma once
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
#else
inline bool retrySoftwareOpenGL(int, char**, const std::string&, int&) { return false; }
#endif
}
