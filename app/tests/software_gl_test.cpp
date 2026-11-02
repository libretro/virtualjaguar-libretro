#include "software_gl.h"
#include <cstdio>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#endif

int main() {
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
    SetEnvironmentVariableW(L"VJ_SOFTWARE_OPENGL_RETRY", L"1");
    if (vj::retrySoftwareOpenGL(0, nullptr, "", exitCode) || exitCode != 123) {
        std::puts("FAIL software retry loop guard"); return 1;
    }
    SetEnvironmentVariableW(L"VJ_SOFTWARE_OPENGL_RETRY", nullptr);
    // This unit binary's build directory has no staged software-opengl app.
    if (vj::retrySoftwareOpenGL(0, nullptr, "", exitCode) || exitCode != 123) {
        std::puts("FAIL missing runtime should not launch a child"); return 1;
    }
#else
    if (vj::quoteSoftwareGLArgument(L"C:\\") != L"\"C:\\\\\"" || vj::quoteSoftwareGLArgument(L"") != L"\"\"") {
        std::puts("FAIL Windows trailing-backslash quoting"); return 1;
    }
#endif
    std::puts("PASS software graphics arguments and retry guards");
}
