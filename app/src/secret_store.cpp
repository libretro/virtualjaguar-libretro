// Secret storage backends + the RetroAchievements token policy.  See secret_store.h.
#include "secret_store.h"

#include <cstdio>
#include <map>
#include <mutex>

#include "config.h"

#if defined(__APPLE__)
#include <CoreFoundation/CoreFoundation.h>
#include <Security/Security.h>
#elif defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <wincred.h>
#else
#include <dlfcn.h>
#endif

namespace vj {
namespace secret {

const char* const kService = "io.github.libretro.VirtualJaguar";

namespace {

// ---------------------------------------------------------------------------------
// in-memory backend (mock sessions, tests)
// ---------------------------------------------------------------------------------
class MemoryBackend : public Backend {
public:
    const char* name() const override { return "memory"; }
    bool available() override { return true; }
    Status get(const std::string& service, const std::string& account, std::string& out) override {
        std::lock_guard<std::mutex> lk(m_);
        auto it = items_.find(key(service, account));
        if (it == items_.end()) return Status::NotFound;
        out = it->second;
        return Status::Ok;
    }
    Status set(const std::string& service, const std::string& account, const std::string& secret) override {
        std::lock_guard<std::mutex> lk(m_);
        items_[key(service, account)] = secret;
        return Status::Ok;
    }
    Status erase(const std::string& service, const std::string& account) override {
        std::lock_guard<std::mutex> lk(m_);
        items_.erase(key(service, account));
        return Status::Ok;
    }

private:
    static std::string key(const std::string& s, const std::string& a) { return s + std::string(1, '\0') + a; }
    std::mutex m_;
    std::map<std::string, std::string> items_;
};

#if defined(__APPLE__)
// ---------------------------------------------------------------------------------
// macOS: Keychain, generic password item
// ---------------------------------------------------------------------------------
class SystemBackend : public Backend {
public:
    const char* name() const override { return "macOS Keychain"; }
    bool available() override { return true; }

    Status get(const std::string& service, const std::string& account, std::string& out) override {
        CFMutableDictionaryRef q = query(service, account);
        if (!q) return Status::Unavailable;
        CFDictionarySetValue(q, kSecReturnData, kCFBooleanTrue);
        CFDictionarySetValue(q, kSecMatchLimit, kSecMatchLimitOne);
        CFTypeRef res = nullptr;
        OSStatus st = SecItemCopyMatching(q, &res);
        CFRelease(q);
        if (st == errSecItemNotFound) return Status::NotFound;
        if (st != errSecSuccess || !res) {
            if (res) CFRelease(res);
            return Status::Unavailable;
        }
        Status r = Status::Unavailable;
        if (CFGetTypeID(res) == CFDataGetTypeID()) {
            CFDataRef d = (CFDataRef)res;
            out.assign((const char*)CFDataGetBytePtr(d), (size_t)CFDataGetLength(d));
            r = Status::Ok;
        }
        CFRelease(res);
        return r;
    }

    Status set(const std::string& service, const std::string& account, const std::string& secret) override {
        CFDataRef data = CFDataCreate(kCFAllocatorDefault, (const UInt8*)secret.data(), (CFIndex)secret.size());
        if (!data) return Status::Unavailable;
        Status r = Status::Unavailable;
        for (int attempt = 0; attempt < 2 && r == Status::Unavailable; attempt++) {
            CFMutableDictionaryRef q = query(service, account);
            if (!q) break;
            CFMutableDictionaryRef upd = CFDictionaryCreateMutable(kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks,
                                                                   &kCFTypeDictionaryValueCallBacks);
            CFDictionarySetValue(upd, kSecValueData, data);
            OSStatus st = SecItemUpdate(q, upd);
            CFRelease(upd);
            if (st == errSecItemNotFound) {
                CFDictionarySetValue(q, kSecValueData, data);
                CFStringRef label = CFSTR("Virtual Jaguar RetroAchievements login token");
                CFDictionarySetValue(q, kSecAttrLabel, label);
                st = SecItemAdd(q, nullptr);
                if (st == errSecDuplicateItem) {   // created between the update and the add: update again
                    CFRelease(q);
                    continue;
                }
            }
            CFRelease(q);
            if (st == errSecSuccess) r = Status::Ok;
            else break;
        }
        CFRelease(data);
        return r;
    }

    Status erase(const std::string& service, const std::string& account) override {
        CFMutableDictionaryRef q = query(service, account);
        if (!q) return Status::Unavailable;
        OSStatus st = SecItemDelete(q);
        CFRelease(q);
        return (st == errSecSuccess || st == errSecItemNotFound) ? Status::Ok : Status::Unavailable;
    }

private:
    static CFMutableDictionaryRef query(const std::string& service, const std::string& account) {
        CFStringRef s = CFStringCreateWithBytes(kCFAllocatorDefault, (const UInt8*)service.data(), (CFIndex)service.size(),
                                                kCFStringEncodingUTF8, false);
        CFStringRef a = CFStringCreateWithBytes(kCFAllocatorDefault, (const UInt8*)account.data(), (CFIndex)account.size(),
                                                kCFStringEncodingUTF8, false);
        CFMutableDictionaryRef q = nullptr;
        if (s && a) {
            q = CFDictionaryCreateMutable(kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks,
                                          &kCFTypeDictionaryValueCallBacks);
            if (q) {
                CFDictionarySetValue(q, kSecClass, kSecClassGenericPassword);
                CFDictionarySetValue(q, kSecAttrService, s);
                CFDictionarySetValue(q, kSecAttrAccount, a);
            }
        }
        if (s) CFRelease(s);
        if (a) CFRelease(a);
        return q;
    }
};

#elif defined(_WIN32)
// ---------------------------------------------------------------------------------
// Windows: Credential Manager, generic credential
// ---------------------------------------------------------------------------------
std::wstring widen(const std::string& s) {
    if (s.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    if (n <= 0) return std::wstring();
    std::wstring w((size_t)n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
    return w;
}

class SystemBackend : public Backend {
public:
    const char* name() const override { return "Windows Credential Manager"; }
    bool available() override { return true; }

    Status get(const std::string& service, const std::string& account, std::string& out) override {
        const std::wstring t = target(service, account);
        if (t.empty()) return Status::Unavailable;
        PCREDENTIALW p = nullptr;
        if (!CredReadW(t.c_str(), CRED_TYPE_GENERIC, 0, &p)) {
            return GetLastError() == ERROR_NOT_FOUND ? Status::NotFound : Status::Unavailable;
        }
        out.assign((const char*)p->CredentialBlob, (size_t)p->CredentialBlobSize);
        CredFree(p);
        return Status::Ok;
    }

    Status set(const std::string& service, const std::string& account, const std::string& secret) override {
        std::wstring t = target(service, account);
        std::wstring u = widen(account);
        if (t.empty() || u.empty()) return Status::Unavailable;
        CREDENTIALW c;
        ZeroMemory(&c, sizeof c);
        c.Type = CRED_TYPE_GENERIC;
        c.TargetName = &t[0];
        c.UserName = &u[0];
        c.CredentialBlobSize = (DWORD)secret.size();
        c.CredentialBlob = (LPBYTE)secret.data();
        c.Persist = CRED_PERSIST_LOCAL_MACHINE;
        return CredWriteW(&c, 0) ? Status::Ok : Status::Unavailable;
    }

    Status erase(const std::string& service, const std::string& account) override {
        const std::wstring t = target(service, account);
        if (t.empty()) return Status::Unavailable;
        if (CredDeleteW(t.c_str(), CRED_TYPE_GENERIC, 0)) return Status::Ok;
        return GetLastError() == ERROR_NOT_FOUND ? Status::Ok : Status::Unavailable;
    }

private:
    // The app's own service maps to the documented "VirtualJaguar:RetroAchievements:<user>"; any other
    // service name (tests) is "<service>:<account>" so it can never collide with the real entry.
    static std::wstring target(const std::string& service, const std::string& account) {
        if (account.empty()) return std::wstring();
        if (service == kService) return widen("VirtualJaguar:RetroAchievements:" + account);
        return widen(service + ":" + account);
    }
};

#else
// ---------------------------------------------------------------------------------
// Linux / BSD: libsecret (Secret Service), loaded at run time.  No build dependency, no run
// dependency: dlopen failure = unavailable.  The calls are the synchronous variadic helpers; the
// SecretSchema layout below mirrors libsecret/secret-schema.h (checked against 0.21.x).
// Inside a Flatpak libsecret talks to the Secret portal by itself (>= 0.19), so the sandbox
// needs no org.freedesktop.secrets permission.
// ---------------------------------------------------------------------------------
struct SecretSchemaAttribute {
    const char* name;
    int type;   // SECRET_SCHEMA_ATTRIBUTE_STRING = 0
};
struct SecretSchema {
    const char* name;
    int flags;   // SECRET_SCHEMA_NONE = 0
    SecretSchemaAttribute attributes[32];
    int reserved;
    void* reserved1;
    void* reserved2;
    void* reserved3;
    void* reserved4;
    void* reserved5;
    void* reserved6;
    void* reserved7;
};

typedef int (*StoreFn)(const SecretSchema*, const char* collection, const char* label, const char* password,
                       void* cancellable, void** error, ...);
typedef char* (*LookupFn)(const SecretSchema*, void* cancellable, void** error, ...);
typedef int (*ClearFn)(const SecretSchema*, void* cancellable, void** error, ...);
typedef void (*FreeFn)(char*);
typedef void (*ErrFreeFn)(void*);

class SystemBackend : public Backend {
public:
    const char* name() const override { return "libsecret"; }
    bool available() override { return load(); }

    Status get(const std::string& service, const std::string& account, std::string& out) override {
        if (!load()) return Status::Unavailable;
        void* err = nullptr;
        char* pw = lookup_(&schema_, nullptr, &err, "service", service.c_str(), "account", account.c_str(), (char*)nullptr);
        if (err) {   // service unreachable / locked / denied: NOT "not found"
            freeError(err);
            if (pw) pwFree_(pw);
            return Status::Unavailable;
        }
        if (!pw) return Status::NotFound;   // no error and no value = no such item
        out = pw;
        pwFree_(pw);
        return Status::Ok;
    }

    Status set(const std::string& service, const std::string& account, const std::string& secret) override {
        if (!load()) return Status::Unavailable;
        void* err = nullptr;
        int ok = store_(&schema_, "default", "Virtual Jaguar RetroAchievements login token", secret.c_str(), nullptr, &err,
                        "service", service.c_str(), "account", account.c_str(), (char*)nullptr);
        if (err) freeError(err);
        return (ok && !err) ? Status::Ok : Status::Unavailable;
    }

    Status erase(const std::string& service, const std::string& account) override {
        if (!load()) return Status::Unavailable;
        void* err = nullptr;
        int removed = clear_(&schema_, nullptr, &err, "service", service.c_str(), "account", account.c_str(), (char*)nullptr);
        if (err) {
            freeError(err);
            return Status::Unavailable;
        }
        (void)removed;   // false with no error = nothing to remove
        return Status::Ok;
    }

private:
    bool load() {
        std::lock_guard<std::mutex> lk(m_);
        if (tried_) return ok_;
        tried_ = true;
        void* h = dlopen("libsecret-1.so.0", RTLD_NOW | RTLD_LOCAL);
        if (!h) return false;
        store_ = (StoreFn)dlsym(h, "secret_password_store_sync");
        lookup_ = (LookupFn)dlsym(h, "secret_password_lookup_sync");
        clear_ = (ClearFn)dlsym(h, "secret_password_clear_sync");
        pwFree_ = (FreeFn)dlsym(h, "secret_password_free");
        errFree_ = (ErrFreeFn)dlsym(h, "g_error_free");   // glib is libsecret's own dependency
        if (!store_ || !lookup_ || !clear_ || !pwFree_) {
            dlclose(h);
            return false;
        }
        schema_ = SecretSchema();
        schema_.name = "io.github.libretro.VirtualJaguar.Token";
        schema_.flags = 0;
        schema_.attributes[0].name = "service";
        schema_.attributes[0].type = 0;
        schema_.attributes[1].name = "account";
        schema_.attributes[1].type = 0;
        ok_ = true;
        return true;
    }
    void freeError(void* e) const {
        if (errFree_) errFree_(e);
    }

    std::mutex m_;
    bool tried_ = false, ok_ = false;
    StoreFn store_ = nullptr;
    LookupFn lookup_ = nullptr;
    ClearFn clear_ = nullptr;
    FreeFn pwFree_ = nullptr;
    ErrFreeFn errFree_ = nullptr;
    SecretSchema schema_;
};
#endif

Backend* g_override = nullptr;

bool iequals(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); i++) {
        char x = a[i], y = b[i];
        if (x >= 'A' && x <= 'Z') x = (char)(x - 'A' + 'a');
        if (y >= 'A' && y <= 'Z') y = (char)(y - 'A' + 'a');
        if (x != y) return false;
    }
    return true;
}

}  // namespace

// ---------------------------------------------------------------------------------
// backend selection
// ---------------------------------------------------------------------------------
Backend& active() {
    if (g_override) return *g_override;
    static SystemBackend sys;   // first use only: no work at static-init time
    return sys;
}

void useBackend(Backend* b) { g_override = b; }

void useMemoryBackend() {
    static MemoryBackend mem;
    g_override = &mem;
}

std::unique_ptr<Backend> makeMemoryBackend() { return std::unique_ptr<Backend>(new MemoryBackend()); }

// ---------------------------------------------------------------------------------
// the RetroAchievements token
// ---------------------------------------------------------------------------------
TokenLoad loadToken(Config& cfg, Backend& b, const std::string& user, bool allowMigrate) {
    TokenLoad r;
    const std::string cfgTok = cfg.get("ra.token", "");
    if (user.empty() && cfgTok.empty()) return r;   // nothing to look up: never touch the store

    if (!cfgTok.empty()) {
        r.token = cfgTok;
        if (user.empty() || !allowMigrate) {
            r.inSettingsFile = true;
            return r;
        }
        std::string back;
        if (b.set(kService, user, cfgTok) == Status::Ok && b.get(kService, user, back) == Status::Ok && back == cfgTok) {
            cfg.erase("ra.token");
            if (cfg.save()) std::fprintf(stderr, "[ra] login token moved from the settings file to the system keychain\n");
            else std::fprintf(stderr, "[ra] login token stored in the keychain but the settings file could not be rewritten\n");
        } else {
            r.inSettingsFile = true;
            std::fprintf(stderr, "[ra] no system keyring available (%s); the login token stays in the settings file\n", b.name());
        }
        return r;
    }

    std::string tok;
    if (b.get(kService, user, tok) == Status::Ok) r.token = tok;
    return r;
}

bool saveToken(Config& cfg, Backend& b, const std::string& user, const std::string& prevUser, const std::string& token) {
    if (!prevUser.empty() && !iequals(prevUser, user)) b.erase(kService, prevUser);
    if (!user.empty() && !token.empty() && b.set(kService, user, token) == Status::Ok) {
        cfg.erase("ra.token");
        return true;
    }
    cfg.set("ra.token", token);
    std::fprintf(stderr, "[ra] no system keyring available (%s); the login token is kept in the settings file\n", b.name());
    return false;
}

void clearToken(Config& cfg, Backend& b, const std::string& user) {
    if (!user.empty()) b.erase(kService, user);
    cfg.erase("ra.token");
}

}  // namespace secret
}  // namespace vj
