// Secret storage for the RetroAchievements login token (#836).
//
// The token is a credential: anyone who can read it can act as the user on
// retroachievements.org.  It used to sit in config.ini in plain text; it now lives in the
// operating system's credential store:
//
//   macOS    Keychain (Security.framework, generic password item, service
//            "io.github.libretro.VirtualJaguar", account = RA user name)
//   Windows  Credential Manager (generic credential "VirtualJaguar:RetroAchievements:<user>")
//   Linux    Secret Service through libsecret, loaded with dlopen at run time: there is no
//            build or run dependency on it.
//
// Where no store is usable (no libsecret, no running keyring, a Flatpak without the secret
// portal, a denied or locked keychain) every call reports Unavailable and the caller keeps the
// token in config.ini exactly as before -- with a one-line log note and a hint in Settings.
// Nothing is ever erased on Unavailable: only an explicit NotFound / success answer counts.
//
// Backends are swappable: --ra-mock and the headless modes install the in-memory backend so
// a test run can never reach the real store, and the unit tests install their own.
#pragma once

#include <memory>
#include <string>

namespace vj {

class Config;

namespace secret {

// The one service name the app uses.
extern const char* const kService;   // "io.github.libretro.VirtualJaguar"

enum class Status {
    Ok,           // done / value returned
    NotFound,     // the store answered: no such item (erase of a missing item is Ok)
    Unavailable,  // no store, locked, denied, or any other failure: nothing was changed
};

class Backend {
public:
    virtual ~Backend() {}
    virtual const char* name() const = 0;
    // Cheap "could this ever work here" probe (Linux: libsecret loaded).  Operations may still
    // report Unavailable (a headless Linux box with libsecret but no keyring daemon).
    virtual bool available() = 0;
    virtual Status get(const std::string& service, const std::string& account, std::string& out) = 0;
    virtual Status set(const std::string& service, const std::string& account, const std::string& secret) = 0;
    virtual Status erase(const std::string& service, const std::string& account) = 0;
};

// The active backend: the platform store unless useBackend()/useMemoryBackend() replaced it.
// The platform backend is created lazily, on first use (no work at static-init time).
Backend& active();
// Replace the active backend (not owned; nullptr restores the platform store).  For tests.
void useBackend(Backend* b);
// Install the process-wide in-memory backend (--ra-mock / --headless).  Idempotent.
void useMemoryBackend();
// A fresh in-memory backend (tests).
std::unique_ptr<Backend> makeMemoryBackend();

// ---- the RetroAchievements token, kept in the store with config.ini as the fallback --------

struct TokenLoad {
    std::string token;         // "" = none
    bool inSettingsFile = false;   // the only copy is the plain-text one in config.ini
};

// Startup: returns the saved token for `user`.  A token found in config.ini ("ra.token") is moved
// into the store (written, read back and compared, THEN removed from the config and the file
// rewritten atomically) when `allowMigrate` -- the primary instance only: a secondary copy's
// Config::save() writes just its own keys, so it can neither migrate nor clean the shared file.
// If the store refuses, the config token stays and inSettingsFile is set.  With no user name and no
// config token the store is not touched at all.
TokenLoad loadToken(Config& cfg, Backend& b, const std::string& user, bool allowMigrate);

// Explicit password login: keeps the token for `user` in the store and out of config.ini.  Falls
// back to the config file (returns false) when the store refuses.  `prevUser` (the account the
// config named before this login) loses its stored token when it differs from `user`.
bool saveToken(Config& cfg, Backend& b, const std::string& user, const std::string& prevUser,
               const std::string& token);

// Logout / stale token: erase from the store and from config.ini.  Caller saves the config.
void clearToken(Config& cfg, Backend& b, const std::string& user);

}  // namespace secret
}  // namespace vj
