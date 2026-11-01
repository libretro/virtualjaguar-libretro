#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dxgi1_2.h>
#include <d3d12.h>
#include "software_gl.h"
#include <algorithm>
#include <cstdio>
#include <cwchar>
#include <vector>

namespace vj {
namespace {
// The description of the first hardware adapter that can create a Direct3D 12
// device at feature level 11_0, or empty when the machine has none.  The
// "Microsoft Basic Render Driver" (WARP) is a software adapter and is skipped:
// GL on WARP is slower than llvmpipe and the app did not survive it on the
// hosted ARM64 runner.  dxgi.dll / d3d12.dll are loaded by hand so the
// executable keeps running on a machine without either (no link dependency).
std::wstring hardwareD3D12Adapter() {
    std::wstring found;
    HMODULE dxgi = LoadLibraryW(L"dxgi.dll");
    HMODULE d3d12 = LoadLibraryW(L"d3d12.dll");
    if (!dxgi || !d3d12) {
        if (dxgi) FreeLibrary(dxgi);
        if (d3d12) FreeLibrary(d3d12);
        return found;
    }
    using CreateFactory1 = HRESULT(WINAPI*)(REFIID, void**);
    using CreateDevice = HRESULT(WINAPI*)(IUnknown*, D3D_FEATURE_LEVEL, REFIID, void**);
    auto createFactory = reinterpret_cast<CreateFactory1>(GetProcAddress(dxgi, "CreateDXGIFactory1"));
    auto createDevice = reinterpret_cast<CreateDevice>(GetProcAddress(d3d12, "D3D12CreateDevice"));
    IDXGIFactory1* factory = nullptr;
    if (createFactory && createDevice && SUCCEEDED(createFactory(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&factory)))) {
        IDXGIAdapter1* adapter = nullptr;
        for (UINT i = 0; found.empty() && factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i) {
            DXGI_ADAPTER_DESC1 desc = {};
            // DXGI_ADAPTER_FLAG_SOFTWARE is not reliably set on the Basic Render
            // Driver (the hosted ARM64 runner enumerates it without the flag), so
            // also reject Microsoft's vendor id 0x1414 and the name itself.
            if (SUCCEEDED(adapter->GetDesc1(&desc))) {
                const bool software = (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) || desc.VendorId == 0x1414 ||
                                      std::wcsstr(desc.Description, L"Basic Render") != nullptr;
                if (!software && SUCCEEDED(createDevice(adapter, D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), nullptr)))
                    found = desc.Description;
            }
            adapter->Release();
            adapter = nullptr;
        }
        factory->Release();
    }
    FreeLibrary(d3d12);
    FreeLibrary(dxgi);
    return found;
}
std::wstring wide(const std::string& text) {
    if (text.empty()) return {};
    int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), (int)text.size(), nullptr, 0);
    if (!size) return {};
    std::wstring result((size_t)size, L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), (int)text.size(), result.data(), size);
    return result;
}
bool fileExists(const std::wstring& path) {
    DWORD attr = GetFileAttributesW(path.c_str());
    return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
}
}

bool retrySoftwareOpenGL(int argc, char** argv, const std::string& dataDir, int& exitCode) {
    if (GetEnvironmentVariableW(L"VJ_SOFTWARE_OPENGL_RETRY", nullptr, 0)) return false;
    wchar_t path[32768];
    DWORD size = GetModuleFileNameW(nullptr, path, (DWORD)(sizeof path / sizeof path[0]));
    if (!size || size >= sizeof path / sizeof path[0]) return false;
    std::wstring exe(path, size);
    size_t slash = exe.find_last_of(L"\\/");
    if (slash == std::wstring::npos) return false;
    std::wstring folder = exe.substr(0, slash + 1) + L"software-opengl\\";
    std::wstring child = folder + L"virtualjaguar.exe";
    std::wstring dll = folder + L"opengl32.dll";
    if (!fileExists(child) || !fileExists(dll) || !fileExists(folder + L"libgallium_wgl.dll")) {
        std::fprintf(stderr, "[gl] bundled software OpenGL is unavailable\n");
        return false;
    }
    std::wstring command = quoteSoftwareGLArgument(child);
    for (int i = 1; i < argc; ++i) command += L" " + quoteSoftwareGLArgument(wide(argv[i]));
    // The executable lives beside Mesa, but config/saves and the instance number
    // must keep using the original app's data root (including portable installs).
    command += L" --data-dir " + quoteSoftwareGLArgument(wide(dataDir));
    std::vector<std::wstring> entries;
    wchar_t* block = GetEnvironmentStringsW();
    if (!block) return false;
    for (const wchar_t* p = block; *p; p += std::wcslen(p) + 1) {
        if (!_wcsnicmp(p, L"SDL_OPENGL_LIBRARY=", 19) || !_wcsnicmp(p, L"VJ_SOFTWARE_OPENGL_RETRY=", 25)) continue;
        entries.emplace_back(p);
    }
    FreeEnvironmentStringsW(block);
    entries.push_back(L"SDL_OPENGL_LIBRARY=" + dll);
    entries.push_back(L"VJ_SOFTWARE_OPENGL_RETRY=1");
    // Which Gallium driver the bundled Mesa uses.  It carries d3d12 (OpenGL on
    // the Direct3D 12 driver every stock Windows 10/11 install has, the same
    // layer as Microsoft's OpenGL Compatibility Pack) and llvmpipe (pure
    // software).  With a hardware D3D12 adapter present, d3d12 renders on the
    // GPU, pinned to that adapter so Mesa cannot drift onto WARP; without one,
    // llvmpipe: Mesa left alone takes the Microsoft Basic Render Driver, which
    // is software D3D12 under a GL translation, slower than llvmpipe and not
    // something the app survived on the hosted ARM64 runner.  A GALLIUM_DRIVER
    // the user already set wins over this probe.
    if (!GetEnvironmentVariableW(L"GALLIUM_DRIVER", nullptr, 0)) {
        std::wstring adapter = hardwareD3D12Adapter();
        if (!adapter.empty()) {
            entries.push_back(L"GALLIUM_DRIVER=d3d12");
            if (!GetEnvironmentVariableW(L"MESA_D3D12_DEFAULT_ADAPTER_NAME", nullptr, 0))
                entries.push_back(L"MESA_D3D12_DEFAULT_ADAPTER_NAME=" + adapter);
            std::fprintf(stderr, "[gl] bundled Mesa will render through Direct3D 12 on %ls\n", adapter.c_str());
        } else {
            entries.push_back(L"GALLIUM_DRIVER=llvmpipe");
            entries.push_back(L"LIBGL_ALWAYS_SOFTWARE=true");
            std::fprintf(stderr, "[gl] no hardware Direct3D 12 adapter; bundled Mesa will render in software (llvmpipe)\n");
        }
    }
    std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) { return _wcsicmp(a.c_str(), b.c_str()) < 0; });
    std::vector<wchar_t> environment;
    for (const auto& entry : entries) environment.insert(environment.end(), entry.c_str(), entry.c_str() + entry.size() + 1);
    environment.push_back(L'\0');
    STARTUPINFOW startup = {};
    startup.cb = sizeof startup;
    // Preserve redirected diagnostics without inheriting an instance lock.
    HANDLE copies[3] = {};
    const DWORD ids[] = {STD_INPUT_HANDLE, STD_OUTPUT_HANDLE, STD_ERROR_HANDLE};
    for (int i = 0; i < 3; ++i) {
        HANDLE h = GetStdHandle(ids[i]);
        if (h && h != INVALID_HANDLE_VALUE)
            DuplicateHandle(GetCurrentProcess(), h, GetCurrentProcess(), &copies[i], 0, TRUE, DUPLICATE_SAME_ACCESS);
    }
    if (copies[1] || copies[2]) {
        startup.dwFlags = STARTF_USESTDHANDLES;
        startup.hStdInput = copies[0]; startup.hStdOutput = copies[1]; startup.hStdError = copies[2];
    }
    PROCESS_INFORMATION process = {};
    std::fprintf(stderr, "[gl] automatically retrying with bundled Mesa OpenGL (Direct3D 12 when available, software otherwise)\n");
    std::fflush(nullptr);
    BOOL created = CreateProcessW(child.c_str(), command.data(), nullptr, nullptr, TRUE,
                                 CREATE_UNICODE_ENVIRONMENT, environment.data(), nullptr, &startup, &process);
    DWORD error = GetLastError();
    for (HANDLE h : copies) if (h) CloseHandle(h);
    if (!created) {
        std::fprintf(stderr, "[gl] software renderer could not start (Windows error %lu)\n", (unsigned long)error);
        return false;
    }
    CloseHandle(process.hThread);
    DWORD result = 1;
    if (WaitForSingleObject(process.hProcess, INFINITE) == WAIT_OBJECT_0) GetExitCodeProcess(process.hProcess, &result);
    CloseHandle(process.hProcess);
    exitCode = (int)result;
    return true;
}
}
