// Running several copies of the app (instance.h).  No window, no GL, no core.
//
//   vj_instance_test            naming rules, the lock (in-process and across processes, incl. that a
//                               spawned child does NOT inherit the lock), the settings-file policy
//                               (a secondary copy never rewrites the shared config.ini), per-instance
//                               paths, unique temp names for atomic writes, bundle detection.
//   vj_instance_test hold DIR   (internal) take a number in DIR, print it, wait for stdin to close.
#include "config.h"
#include "instance.h"
#include "paths.h"

#include <SDL3/SDL.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#ifndef _WIN32
#  include <spawn.h>
#  include <sys/wait.h>
#  include <unistd.h>
extern char** environ;
#endif

namespace fs = std::filesystem;
using namespace vj;

static int g_checks = 0, g_fail = 0;
#define CHECK(c) do { g_checks++; if (!(c)) { g_fail++; std::fprintf(stderr, "FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)

static std::string slurp(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static void testNaming() {
    CHECK(instanceLockName(1) == "instance-1.lock");
    CHECK(instanceLockName(12) == "instance-12.lock");
    // Instance 1 keeps every name it has always had.
    CHECK(instanceSavesDir("/d/saves/", 1) == "/d/saves/");
    CHECK(instanceSavesDir("/d/saves/", 0) == "/d/saves/");
    CHECK(instanceConfigName(1) == "config.ini");
    CHECK(instanceImguiName(1) == "imgui.ini");
    CHECK(instanceTitleSuffix(1).empty());
    // Later ones are separate, and distinct from each other.
    CHECK(instanceSavesDir("/d/saves/", 2) == "/d/saves/instance-2/");
    CHECK(instanceSavesDir("/d/saves/", 3) == "/d/saves/instance-3/");
    CHECK(instanceConfigName(2) == "config-2.ini");
    CHECK(instanceImguiName(3) == "imgui-3.ini");
    CHECK(instanceTitleSuffix(2) == " [Instance 2]");
    CHECK(instancePadKey(1) == "input.pads.1");
    CHECK(instancePadKey(2) == "input.pads.2");
    CHECK(instancePadKey(0) == "input.pads.1");
    // The controller filter round-trips and defaults to "all".
    CHECK(parsePadFilter("") == -1);
    CHECK(parsePadFilter("all") == -1);
    CHECK(parsePadFilter("junk") == -1);
    CHECK(parsePadFilter("none") == 0);
    CHECK(parsePadFilter("1") == 1);
    CHECK(parsePadFilter("9") == 9);
    CHECK(parsePadFilter("10") == -1);
    for (int f = -1; f <= 9; f++) CHECK(parsePadFilter(padFilterValue(f)) == f);
    // File > New Window: a copy inside an .app bundle is started through LaunchServices.
    CHECK(bundleOfExecutableDir("/Applications/Virtual Jaguar.app/Contents/MacOS/") == "/Applications/Virtual Jaguar.app");
    CHECK(bundleOfExecutableDir("/Users/x/build/") == "");
    CHECK(bundleOfExecutableDir("/Users/x/Foo.app/Contents/Resources/") == "");
    CHECK(bundleOfExecutableDir("") == "");
}

static void testLock(const fs::path& tmp) {
    fs::create_directories(tmp);
    const std::string dir = tmp.string() + "/";
    {
        InstanceLock a, b, c;
        CHECK(a.acquire(dir) == 1 && a.held());
        CHECK(b.acquire(dir) == 2 && b.held());
        CHECK(c.acquire(dir) == 3);
        // The lowest FREE number is reused, so closing window 2 and opening another gives 2 again.
        b.release();
        CHECK(!b.held() && b.number() == 0);
        InstanceLock d;
        CHECK(d.acquire(dir) == 2);
        // Releasing is idempotent; the destructor releases the rest.
        a.release();
        a.release();
        InstanceLock e;
        CHECK(e.acquire(dir) == 1);
    }
    // Everything released: the first copy is instance 1 again.
    {
        InstanceLock a;
        CHECK(a.acquire(dir) == 1);
    }
    // All numbers taken: 0 (the app refuses to start rather than share a save folder).
    {
        std::vector<InstanceLock> many((size_t)kMaxInstances);
        for (int i = 0; i < kMaxInstances; i++) CHECK(many[(size_t)i].acquire(dir) == i + 1);
        InstanceLock extra;
        CHECK(extra.acquire(dir) == 0);
        CHECK(!extra.held());
    }
    // A folder the lock files cannot be created in: behave as before (instance 1), holding nothing.
    {
        InstanceLock a;
        CHECK(a.acquire((tmp / "does" / "not" / "exist").string() + "/") == 1);
        CHECK(!a.held());
    }
}

#ifndef _WIN32
// A second PROCESS sees the first one's number as taken, and a child we start does not keep it alive.
static int runHold(const std::string& dir) {
    InstanceLock l;
    int n = l.acquire(dir);
    std::printf("%d\n", n);
    std::fflush(stdout);
    char b;
    while (std::fread(&b, 1, 1, stdin) == 1) {}   // until the parent closes our stdin
    return 0;
}

static int holdChild(const char* self, const std::string& dir, pid_t& pid, FILE*& toChild, FILE*& fromChild) {
    int in[2], out[2];
    if (pipe(in) || pipe(out)) return -1;
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_adddup2(&fa, in[0], 0);
    posix_spawn_file_actions_adddup2(&fa, out[1], 1);
    posix_spawn_file_actions_addclose(&fa, in[1]);
    posix_spawn_file_actions_addclose(&fa, out[0]);
    std::string s = self, h = "hold", d = dir;
    char* argv[] = {&s[0], &h[0], &d[0], nullptr};
    int rc = posix_spawn(&pid, self, &fa, nullptr, argv, environ);
    posix_spawn_file_actions_destroy(&fa);
    close(in[0]);
    close(out[1]);
    if (rc != 0) return -1;
    toChild = fdopen(in[1], "w");
    fromChild = fdopen(out[0], "r");
    int n = 0;
    if (!fromChild || std::fscanf(fromChild, "%d", &n) != 1) return -1;
    return n;
}

static void testCrossProcess(const char* self, const fs::path& tmp) {
    fs::create_directories(tmp);
    const std::string dir = tmp.string() + "/";
    InstanceLock mine;
    CHECK(mine.acquire(dir) == 1);
    pid_t pid = 0;
    FILE *toChild = nullptr, *fromChild = nullptr;
    // The child must be told 2: this process holds 1.
    CHECK(holdChild(self, dir, pid, toChild, fromChild) == 2);
    // Now drop OUR number while the child (which was started while we held it) is still running: it
    // must not have inherited the descriptor, so 1 is free again and we get it back.
    mine.release();
    InstanceLock again;
    CHECK(again.acquire(dir) == 1);
    again.release();
    if (toChild) fclose(toChild);   // child exits
    if (fromChild) fclose(fromChild);
    int st = 0;
    if (pid) waitpid(pid, &st, 0);
    // The child died: its number (2) is free again without any cleanup (the OS drops the lock).
    InstanceLock a, b;
    CHECK(a.acquire(dir) == 1);
    CHECK(b.acquire(dir) == 2);
}
#endif

static void testConfigPolicy(const fs::path& tmp) {
    fs::create_directories(tmp);
    const std::string shared = (tmp / "config.ini").string();
    const std::string second = (tmp / "config-2.ini").string();
    {
        std::ofstream f(shared);
        f << "# Virtual Jaguar settings\nvideo.integer=1\naudio.volume=0.5\nwindow.w=900\nra.token=secret\n";
    }
    const std::string sharedBefore = slurp(shared);

    // First run of a secondary copy: it sees the shared settings...
    Config c;
    c.load(shared);
    c.overlay(second);                 // (missing file: fine)
    c.setPath(second);
    c.setSavePrefixes({"window.", "input.pads."});
    CHECK(c.getBool("video.integer", false));
    CHECK(c.get("ra.token") == "secret");
    // ...changes some of them, and its own window / controller...
    c.setBool("video.integer", false);
    c.setFloat("audio.volume", 0.9f);
    c.setInt("window.w", 1234);
    c.set("input.pads.2", "1");
    c.set("recent.0", "/x.j64");
    CHECK(c.save());
    // ...and the shared file is byte-for-byte what it was.
    CHECK(slurp(shared) == sharedBefore);
    // Only the allowed keys reached its own file.
    Config only;
    only.load(second);
    CHECK(only.getInt("window.w", 0) == 1234);
    CHECK(only.get("input.pads.2") == "1");
    CHECK(!only.has("video.integer"));
    CHECK(!only.has("audio.volume"));
    CHECK(!only.has("ra.token"));
    CHECK(!only.has("recent.0"));
    // Next start: shared settings + the copy's own overrides; a later change to the shared file shows through.
    {
        std::ofstream f(shared);
        f << "video.integer=1\naudio.volume=0.25\nwindow.w=700\n";
    }
    Config d;
    d.load(shared);
    d.overlay(second);
    CHECK(d.getInt("window.w", 0) == 1234);       // its own
    CHECK(d.getFloat("audio.volume", 0) == 0.25f);  // the shared one, not a stale snapshot
    // Without a prefix list the default behaviour (write everything) is unchanged.
    Config e;
    e.load(shared);
    e.setInt("x.y", 3);
    CHECK(e.save());
    CHECK(slurp(shared).find("x.y=3") != std::string::npos);
}

static void testPaths(const fs::path& tmp) {
    Paths p;
    std::string err;
    CHECK(p.resolve(tmp.string(), err));
    const std::string saves1 = p.saves, img1 = p.imguiIni;
    CHECK(p.applyInstance(1, err));
    CHECK(p.instance == 1 && p.saves == saves1 && p.imguiIni == img1 && p.instanceConfig.empty());
    CHECK(p.applyInstance(3, err));
    CHECK(p.instance == 3);
    CHECK(p.saves == saves1 + "instance-3/");
    CHECK(p.savesShared == saves1);
    CHECK(p.imguiIni == p.root + "imgui-3.ini");
    CHECK(p.instanceConfig == p.root + "config-3.ini");
    CHECK(fs::is_directory(p.saves));
    // The game-save file of two copies playing the same game can never be the same file.
    CHECK(p.saves + "Doom.srm" != saves1 + "Doom.srm");
}

static void testAtomicWrite(const fs::path& tmp) {
    fs::create_directories(tmp);
    const std::string f = (tmp / "x.bin").string();
    CHECK(writeFileAtomic(f, "abc", 3));
    CHECK(writeFileAtomic(f, "defg", 4));
    CHECK(slurp(f) == "defg");
    int leftovers = 0;
    for (const auto& e : fs::directory_iterator(tmp))
        if (e.path().extension() == ".tmp") leftovers++;
    CHECK(leftovers == 0);
    // The temp name is per process: two copies writing one file do not share a temp file.
    CHECK(processId() != 0);
}

int main(int argc, char** argv) {
#ifndef _WIN32
    if (argc >= 3 && !std::strcmp(argv[1], "hold")) return runHold(argv[2]);
#endif
    std::error_code ec;
    fs::path root = fs::temp_directory_path(ec) / ("vj-instance-test-" + std::to_string(processId()));
    fs::remove_all(root, ec);
    fs::create_directories(root);

    testNaming();
    testLock(root / "lock");
#ifndef _WIN32
    testCrossProcess(argv[0], root / "xproc");
#endif
    testConfigPolicy(root / "cfg");
    testPaths(root / "paths");
    testAtomicWrite(root / "atomic");

    fs::remove_all(root, ec);   // our own temp dir only
    std::printf("instance_test: %d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
