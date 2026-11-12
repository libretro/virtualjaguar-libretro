#include "winsparkle_updates.h"
#include "winsparkle_config.h"

#include <SDL3/SDL.h>
#include <winsparkle.h>

#include <windows.h>
#include <shellapi.h>

#include <atomic>
#include <filesystem>
#include <string>

namespace vj::windows_updates {
namespace {
enum class InstallState { blocked, allowed, quitting };
std::atomic<InstallState> installState{InstallState::blocked};
bool active = false;
std::wstring installerArguments;

bool readInstallLocation()
{
    std::wstring executable(32768, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
    if (!length || length >= executable.size())
        return false;
    executable.resize(length);
    const auto directory = std::filesystem::path(executable).parent_path();
    const auto marker = directory / L"direct-install.ini";
    if (GetFileAttributesW((directory / L"portable.txt").c_str()) != INVALID_FILE_ATTRIBUTES)
        return false;

    wchar_t scope[16]{};
    GetPrivateProfileStringW(L"Install", L"Scope", L"", scope, 16, marker.c_str());
    const std::wstring installScope(scope);
    if (installScope != L"currentuser" && installScope != L"allusers")
        return false;

    auto path = directory.wstring();
    if (path.back() == L'\\')
        path += L'\\';  // Preserve a final backslash before the closing command-line quote.
    installerArguments = L"/SILENT /SP- /NORESTART /" + installScope + L" /DIR=\"" + path + L"\"";
    return true;
}

int __cdecl canShutdown()
{
    // Reserve shutdown before accepting the installer. The UI must consult
    // setCanInstall(false) before beginning a content load.
    auto expected = InstallState::allowed;
    return installState.compare_exchange_strong(expected, InstallState::quitting);
}

int __cdecl runInstaller(const wchar_t* installer)
{
    SHELLEXECUTEINFOW launch{};
    launch.cbSize = sizeof(launch);
    launch.fMask = SEE_MASK_FLAG_NO_UI;
    launch.lpFile = installer;
    launch.lpParameters = installerArguments.c_str();
    launch.nShow = SW_SHOWDEFAULT;
    if (ShellExecuteExW(&launch))
        return 1;

    // The update dialog reports the launch failure. Permit play and retries.
    auto expected = InstallState::quitting;
    installState.compare_exchange_strong(expected, InstallState::allowed);
    return WINSPARKLE_RETURN_ERROR;
}

void __cdecl requestShutdown()
{
    SDL_Event event{};
    event.type = SDL_EVENT_QUIT;
    SDL_PushEvent(&event);
}
} // namespace

bool start(bool backgroundChecks)
{
    if (active)
        return true;
    if (!readInstallLocation() || !win_sparkle_set_eddsa_public_key(VJ_WINSPARKLE_PUBLIC_KEY))
        return false;
    win_sparkle_set_app_details(L"Virtual Jaguar", L"Virtual Jaguar", VJ_WINSPARKLE_DISPLAY_VERSION);
    win_sparkle_set_registry_path(VJ_WINSPARKLE_REGISTRY);
    win_sparkle_set_app_build_version(VJ_WINSPARKLE_BUILD_VERSION);
    win_sparkle_set_appcast_url(VJ_WINSPARKLE_FEED);
    win_sparkle_set_can_shutdown_callback(canShutdown);
    win_sparkle_set_shutdown_request_callback(requestShutdown);
    win_sparkle_set_user_run_installer_callback(runInstaller);
    // Setting this explicitly before init also suppresses the first-run prompt.
    win_sparkle_set_automatic_check_for_updates(backgroundChecks ? 1 : 0);
    win_sparkle_init();
    active = true;
    return true;
}

void setBackgroundChecks(bool enabled)
{
    if (active)
        win_sparkle_set_automatic_check_for_updates(enabled ? 1 : 0);
}

void check()
{
    if (active)
        win_sparkle_check_update_with_ui();
}

bool setCanInstall(bool allowed)
{
    auto expected = installState.load();
    while (expected != InstallState::quitting) {
        if (installState.compare_exchange_weak(expected, allowed ? InstallState::allowed : InstallState::blocked))
            return true;
    }
    return false;
}

void stop()
{
    installState.store(InstallState::blocked);
    if (active) {
        win_sparkle_cleanup();
        active = false;
    }
}

} // namespace vj::windows_updates
