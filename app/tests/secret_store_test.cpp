// The RetroAchievements login token's storage policy (secret_store.h).  No window, no core, no
// network, and NEVER the real credential store: every case runs against an in-memory or failing
// mock backend.
//
//   memory backend          get / set / erase round trip, NotFound, overwrite, erase of a missing item
//   migration               a plain-text ra.token in config.ini moves to the store; the file ON DISK no
//                           longer holds it afterwards (re-read from disk, not just the map)
//   migration, store down   a refusing backend keeps the token in config.ini and flags the hint
//   secondary instance      allowMigrate=false: the store is read, nothing is written or erased
//   no user, no token       the store is not touched at all
//   login                   saveToken keeps the token out of config.ini; refusal falls back to the file;
//                           switching accounts drops the previous account's item
//   logout / stale token    clearToken erases from the store AND from the file
//   Unavailable != NotFound a failing read never loses anything
//
// The real OS store is only exercised when VJ_TEST_REAL_KEYCHAIN=1 is set (off by default and in CI):
// it round-trips one throw-away item under the service "io.github.libretro.VirtualJaguar.test" and
// deletes it again.  It never touches the app's real item.
#include "config.h"
#include "secret_store.h"

#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace fs = std::filesystem;
using namespace vj;
using secret::Status;

static int g_checks = 0, g_fail = 0;
#define CHECK(c) do { g_checks++; if (!(c)) { g_fail++; std::fprintf(stderr, "FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)

static std::string slurp(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// A store that counts calls and can be told to fail reads, writes, or both.
class MockBackend : public secret::Backend {
public:
    bool failGet = false, failSet = false, failErase = false;
    int gets = 0, sets = 0, erases = 0;
    std::string item;   // the one stored secret (account tracked below)
    std::string account;
    bool has = false;

    const char* name() const override { return "mock"; }
    bool available() override { return !(failGet && failSet); }
    Status get(const std::string&, const std::string& acct, std::string& out) override {
        gets++;
        if (failGet) return Status::Unavailable;
        if (!has || acct != account) return Status::NotFound;
        out = item;
        return Status::Ok;
    }
    Status set(const std::string&, const std::string& acct, const std::string& s) override {
        sets++;
        if (failSet) return Status::Unavailable;
        has = true;
        account = acct;
        item = s;
        return Status::Ok;
    }
    Status erase(const std::string&, const std::string& acct) override {
        erases++;
        if (failErase) return Status::Unavailable;
        if (has && acct == account) has = false;
        return Status::Ok;
    }
    int calls() const { return gets + sets + erases; }
};

static fs::path tempDir() {
    fs::path d = fs::temp_directory_path() / ("vj_secret_test_" + std::to_string((unsigned long)std::rand()) + "_" +
                                              std::to_string((unsigned long)std::time(nullptr)));
    fs::create_directories(d);
    return d;
}

// A config file with a legacy plain-text token, as v3.8.0-era builds wrote it.
static void writeLegacy(const fs::path& file, const std::string& tok) {
    std::ofstream f(file, std::ios::binary);
    f << "# Virtual Jaguar settings\nra.enabled=1\nra.token=" << tok << "\nra.user=TestUser\nvideo.integer=1\n";
}

static void testMemoryBackend() {
    auto b = secret::makeMemoryBackend();
    std::string out;
    CHECK(b->get("svc", "alice", out) == Status::NotFound);
    CHECK(b->set("svc", "alice", "t1") == Status::Ok);
    CHECK(b->get("svc", "alice", out) == Status::Ok && out == "t1");
    CHECK(b->set("svc", "alice", "t2") == Status::Ok);   // overwrite
    CHECK(b->get("svc", "alice", out) == Status::Ok && out == "t2");
    CHECK(b->get("svc", "bob", out) == Status::NotFound);
    CHECK(b->get("other", "alice", out) == Status::NotFound);   // service is part of the key
    CHECK(b->erase("svc", "alice") == Status::Ok);
    CHECK(b->get("svc", "alice", out) == Status::NotFound);
    CHECK(b->erase("svc", "alice") == Status::Ok);   // erase of a missing item is fine
}

static void testMigration() {
    fs::path d = tempDir();
    fs::path file = d / "config.ini";
    writeLegacy(file, "SECRETTOKEN123");
    Config cfg;
    cfg.load(file.string());
    MockBackend b;
    secret::TokenLoad r = secret::loadToken(cfg, b, "TestUser", true);
    CHECK(r.token == "SECRETTOKEN123");
    CHECK(!r.inSettingsFile);
    CHECK(b.has && b.account == "TestUser" && b.item == "SECRETTOKEN123");
    CHECK(!cfg.has("ra.token"));
    // The FILE, re-read from disk, no longer holds the token; everything else survived.
    std::string disk = slurp(file);
    CHECK(disk.find("SECRETTOKEN123") == std::string::npos);
    CHECK(disk.find("ra.token") == std::string::npos);
    CHECK(disk.find("ra.user=TestUser") != std::string::npos);
    CHECK(disk.find("video.integer=1") != std::string::npos);
    // The next start finds it in the store, with no config token and no further writes.
    Config cfg2;
    cfg2.load(file.string());
    int setsBefore = b.sets;
    secret::TokenLoad r2 = secret::loadToken(cfg2, b, "TestUser", true);
    CHECK(r2.token == "SECRETTOKEN123" && !r2.inSettingsFile);
    CHECK(b.sets == setsBefore);
    // Idempotent: nothing left to migrate.
    CHECK(slurp(file).find("SECRETTOKEN123") == std::string::npos);
    std::error_code ec;
    fs::remove_all(d, ec);
}

static void testMigrationStoreDown() {
    fs::path d = tempDir();
    fs::path file = d / "config.ini";
    writeLegacy(file, "KEEPME");
    Config cfg;
    cfg.load(file.string());
    MockBackend b;
    b.failSet = true;
    secret::TokenLoad r = secret::loadToken(cfg, b, "TestUser", true);
    CHECK(r.token == "KEEPME");          // still usable this run
    CHECK(r.inSettingsFile);             // drives the Settings hint
    CHECK(cfg.get("ra.token") == "KEEPME");
    CHECK(slurp(file).find("KEEPME") != std::string::npos);   // the file is untouched

    // The store accepts the write but reads back garbage: not a successful migration either.
    class Liar : public MockBackend {
    public:
        Status get(const std::string&, const std::string&, std::string& out) override { out = "wrong"; return Status::Ok; }
    } liar;
    Config cfg2;
    cfg2.load(file.string());
    secret::TokenLoad r2 = secret::loadToken(cfg2, liar, "TestUser", true);
    CHECK(r2.token == "KEEPME" && r2.inSettingsFile);
    CHECK(slurp(file).find("KEEPME") != std::string::npos);
    std::error_code ec;
    fs::remove_all(d, ec);
}

static void testSecondaryInstance() {
    fs::path d = tempDir();
    fs::path file = d / "config.ini";
    writeLegacy(file, "SHAREDTOKEN");
    // 1) Token still in the shared file (primary could not migrate): the secondary uses it, writes nothing.
    {
        Config cfg;
        cfg.load(file.string());
        MockBackend b;
        secret::TokenLoad r = secret::loadToken(cfg, b, "TestUser", false);
        CHECK(r.token == "SHAREDTOKEN" && r.inSettingsFile);
        CHECK(b.sets == 0 && b.erases == 0);
        CHECK(slurp(file).find("SHAREDTOKEN") != std::string::npos);
    }
    // 2) Token already migrated by the primary: the secondary reads the store, writes nothing.
    {
        fs::path f2 = d / "config2.ini";
        std::ofstream(f2) << "ra.user=TestUser\n";
        Config cfg;
        cfg.load(f2.string());
        MockBackend b;
        b.has = true;
        b.account = "TestUser";
        b.item = "STORED";
        secret::TokenLoad r = secret::loadToken(cfg, b, "TestUser", false);
        CHECK(r.token == "STORED" && !r.inSettingsFile);
        CHECK(b.sets == 0 && b.erases == 0);
    }
    std::error_code ec;
    fs::remove_all(d, ec);
}

static void testNoUserNoToken() {
    Config cfg;
    MockBackend b;
    secret::TokenLoad r = secret::loadToken(cfg, b, "", true);
    CHECK(r.token.empty() && !r.inSettingsFile);
    CHECK(b.calls() == 0);   // a fresh profile never touches the credential store
    // A token with no user name to file it under cannot be migrated; it stays put.
    cfg.set("ra.token", "ORPHAN");
    secret::TokenLoad r2 = secret::loadToken(cfg, b, "", true);
    CHECK(r2.token == "ORPHAN" && r2.inSettingsFile);
    CHECK(b.calls() == 0 && cfg.get("ra.token") == "ORPHAN");
}

static void testStoreReadFailure() {
    // Unavailable is not "no token": nothing is erased, nothing is invented.
    Config cfg;
    MockBackend b;
    b.has = true;
    b.account = "TestUser";
    b.item = "STILLTHERE";
    b.failGet = true;
    secret::TokenLoad r = secret::loadToken(cfg, b, "TestUser", true);
    CHECK(r.token.empty() && !r.inSettingsFile);
    CHECK(b.erases == 0 && b.has && b.item == "STILLTHERE");
}

static void testLogin() {
    Config cfg;
    MockBackend b;
    // Normal: store gets it, config does not.
    cfg.set("ra.token", "stale-from-before");
    CHECK(secret::saveToken(cfg, b, "Alice", "", "TOK-A"));
    CHECK(b.has && b.account == "Alice" && b.item == "TOK-A");
    CHECK(!cfg.has("ra.token"));
    // Another account: the previous one's item is dropped.
    CHECK(secret::saveToken(cfg, b, "Bob", "Alice", "TOK-B"));
    CHECK(b.account == "Bob" && b.item == "TOK-B");
    CHECK(!cfg.has("ra.token"));
    // The same account in another case (RA names are case-insensitive) is not erased after being written.
    CHECK(secret::saveToken(cfg, b, "bob", "BOB", "TOK-B2"));
    CHECK(b.has && b.account == "bob" && b.item == "TOK-B2");
    // Store refuses: falls back to the settings file.
    MockBackend down;
    down.failSet = true;
    Config cfg2;
    CHECK(!secret::saveToken(cfg2, down, "Alice", "", "TOK-F"));
    CHECK(cfg2.get("ra.token") == "TOK-F");
}

static void testLogout() {
    fs::path d = tempDir();
    fs::path file = d / "config.ini";
    writeLegacy(file, "X");
    Config cfg;
    cfg.load(file.string());
    MockBackend b;
    b.has = true;
    b.account = "TestUser";
    b.item = "Y";
    secret::clearToken(cfg, b, "TestUser");
    CHECK(!b.has);
    CHECK(!cfg.has("ra.token"));
    cfg.save();
    CHECK(slurp(file).find("ra.token") == std::string::npos);
    std::error_code ec;
    fs::remove_all(d, ec);
}

// Opt-in: the real OS credential store, with a throw-away item under a TEST service name.
static void testRealStore() {
    const char* on = std::getenv("VJ_TEST_REAL_KEYCHAIN");
    if (!on || std::string(on) != "1") {
        std::printf("real credential store: skipped (set VJ_TEST_REAL_KEYCHAIN=1 to run)\n");
        return;
    }
    secret::Backend& s = secret::active();
    std::printf("real credential store: %s, available=%d\n", s.name(), (int)s.available());
    if (!s.available()) return;   // e.g. a Linux box without libsecret: nothing to test
    const std::string svc = "io.github.libretro.VirtualJaguar.test";
    const std::string acct = "vj-test-" + std::to_string((unsigned long)std::rand());
    std::string out;
    Status st = s.set(svc, acct, "test-token-\xC3\xA9");   // non-ASCII on purpose
    if (st == Status::Unavailable) {
        std::printf("real credential store: refused (no keyring running?); skipped\n");
        return;
    }
    CHECK(st == Status::Ok);
    CHECK(s.get(svc, acct, out) == Status::Ok && out == "test-token-\xC3\xA9");
    CHECK(s.set(svc, acct, "test-token-2") == Status::Ok);
    CHECK(s.get(svc, acct, out) == Status::Ok && out == "test-token-2");
    CHECK(s.erase(svc, acct) == Status::Ok);
    CHECK(s.get(svc, acct, out) == Status::NotFound);
    CHECK(s.erase(svc, acct) == Status::Ok);
}

int main() {
    std::srand((unsigned)std::time(nullptr));
    secret::useMemoryBackend();   // belt and braces: nothing below reaches the OS store unless opted in
    testMemoryBackend();
    testMigration();
    testMigrationStoreDown();
    testSecondaryInstance();
    testNoUserNoToken();
    testStoreReadFailure();
    testLogin();
    testLogout();
    secret::useBackend(nullptr);   // the platform store, only reachable through the opt-in test
    testRealStore();
    std::printf("secret_store_test: %d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
