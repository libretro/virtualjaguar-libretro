// Several copies of the app at once (netplay testing on one machine, local multiplayer on a big
// screen).  Nothing stops a second process from starting; this file is what makes that SAFE:
//
//   * InstanceLock hands every process a number.  The first one gets 1 and behaves exactly as
//     before; the next ones get 2, 3, ... (the lowest number nobody holds).  The lock is an OS
//     file lock on <data root>/instance-N.lock, released by the OS when the process dies, so a
//     crash never leaves a stale "instance 2" behind.  The lock lives in the data root, so two
//     copies pointed at different --data-dir folders (or a portable copy) are both instance 1:
//     they share nothing and need no coordination.
//   * The helpers below derive the per-instance names (save folder, settings file, title) so the
//     rules are in one tested place.  Policy table: app/README.md, "Running several instances".
//   * spawnNewInstance() is File > New Window.
//
// No SDL, no GL: the unit test (tests/instance_test.cpp) links this file alone.
#pragma once
#include <string>

namespace vj {

constexpr int kMaxInstances = 16;

// ---- pure naming rules (instance 1 keeps today's names; header-only so paths.cpp needs no extra object) ----
inline std::string instanceLockName(int n) { return "instance-" + std::to_string(n) + ".lock"; }

inline std::string instanceSavesDir(const std::string& savesDir, int n) {
    if (n <= 1) return savesDir;
    return savesDir + "instance-" + std::to_string(n) + "/";
}

inline std::string instanceConfigName(int n) { return n <= 1 ? "config.ini" : "config-" + std::to_string(n) + ".ini"; }
inline std::string instanceImguiName(int n) { return n <= 1 ? "imgui.ini" : "imgui-" + std::to_string(n) + ".ini"; }
inline std::string instanceTitleSuffix(int n) { return n <= 1 ? "" : " [Instance " + std::to_string(n) + "]"; }
inline std::string instancePadKey(int n) { return "input.pads." + std::to_string(n < 1 ? 1 : n); }

inline int parsePadFilter(const std::string& v) {
    if (v == "none") return 0;
    if (v.size() == 1 && v[0] >= '1' && v[0] <= '9') return v[0] - '0';
    return -1;
}
inline std::string padFilterValue(int f) {
    if (f == 0) return "none";
    if (f > 0 && f <= 9) return std::to_string(f);
    return "all";
}

// Implemented in paths.cpp (used for per-process temp names).
unsigned long processId();

// ---- the lock -------------------------------------------------------------------------------
class InstanceLock {
public:
    InstanceLock() = default;
    ~InstanceLock() { release(); }
    InstanceLock(const InstanceLock&) = delete;
    InstanceLock& operator=(const InstanceLock&) = delete;

    // Takes the lowest free number in 1..kMaxInstances under `dir` (ends with a separator).
    // Returns it, or 0 when all are taken.  If the lock files cannot be created at all
    // (read-only folder) it returns 1 WITHOUT holding anything: the app then runs exactly as it
    // always did, and `held()` is false so a caller can tell.
    int acquire(const std::string& dir);
    void release();
    int number() const { return number_; }
    bool held() const { return held_; }

private:
    int number_ = 0;
    bool held_ = false;
#ifdef _WIN32
    void* handle_ = nullptr;   // HANDLE
#else
    int fd_ = -1;              // file descriptor
#endif
};

// ---- File > New Window ---------------------------------------------------------------------
// Starts another process of this program.  macOS app bundle: `open -n <bundle>` (so the new copy
// is a real, separate app instance); everything else runs the executable directly (AppImage:
// $APPIMAGE; Linux: /proc/self/exe; Windows: this .exe).  `dataDir` (may be empty) is forwarded
// as --data-dir so the new process lands in the same data folder and gets the next number.
// The child does not inherit this process's netplay environment overrides.
bool spawnNewInstance(const std::string& dataDir, std::string& err);

// Pure pieces of the above, exposed for the test: the .app bundle containing `exeDir`
// (e.g. ".../Virtual Jaguar.app/Contents/MacOS/"), or "" when it is not inside one.
std::string bundleOfExecutableDir(const std::string& exeDir);

}  // namespace vj
