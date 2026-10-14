#include "instance.h"

#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#else
#  include <fcntl.h>
#  include <spawn.h>
#  include <sys/file.h>
#  include <sys/wait.h>
#  include <unistd.h>
#  include <thread>
#  ifdef __APPLE__
#    include <mach-o/dyld.h>
#  endif
extern char** environ;
#endif

namespace vj {

// ---- the lock ------------------------------------------------------------------------------
int InstanceLock::acquire(const std::string& dir) {
    release();
    bool anyCreated = false;
    for (int n = 1; n <= kMaxInstances; n++) {
        const std::string path = dir + instanceLockName(n);
#ifdef _WIN32
        // Share mode 0 = nobody else may open it while we hold it; the OS closes the handle (and so
        // frees the number) if we die.  Opening is the lock, no LockFileEx needed.
        int wlen = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0);
        std::vector<wchar_t> w((size_t)(wlen > 0 ? wlen : 1), 0);
        MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, w.data(), wlen);
        HANDLE h = CreateFileW(w.data(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS,
                               FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) {
            if (GetLastError() == ERROR_SHARING_VIOLATION || GetLastError() == ERROR_LOCK_VIOLATION) {
                anyCreated = true;   // exists and is held by another process
                continue;
            }
            continue;                // cannot create here at all: try the next, then give up below
        }
        handle_ = h;
#else
        // O_CLOEXEC matters: a child we spawn must not inherit the descriptor, or it would keep
        // our number locked after we quit.  flock (not fcntl/lockf): the lock belongs to the open
        // file description, so a second open in the SAME process also conflicts (the unit test
        // relies on that) and exec does not leak it.
        int fd = ::open(path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0644);
        if (fd < 0) continue;
        anyCreated = true;
        if (::flock(fd, LOCK_EX | LOCK_NB) != 0) {
            ::close(fd);
            continue;
        }
        fd_ = fd;
#endif
        anyCreated = true;
        number_ = n;
        held_ = true;
        return n;
    }
    if (!anyCreated) {   // read-only data folder: no coordination possible, behave as before
        number_ = 1;
        held_ = false;
        return 1;
    }
    number_ = 0;
    return 0;
}

void InstanceLock::release() {
#ifdef _WIN32
    if (handle_) CloseHandle((HANDLE)handle_);
    handle_ = nullptr;
#else
    if (fd_ >= 0) ::close(fd_);   // closing drops the flock
    fd_ = -1;
#endif
    held_ = false;
    number_ = 0;
}

// ---- spawning ------------------------------------------------------------------------------
std::string bundleOfExecutableDir(const std::string& exeDir) {
    static const char kTail[] = ".app/Contents/MacOS/";
    size_t at = exeDir.rfind(kTail);
    if (at == std::string::npos || at + sizeof(kTail) - 1 != exeDir.size()) return "";
    return exeDir.substr(0, at + 4);   // up to and including ".app"
}

#ifdef _WIN32

namespace {
std::wstring widen(const std::string& s) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w((size_t)(n > 0 ? n : 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], n);
    w.resize(n > 0 ? (size_t)n - 1 : 0);
    return w;
}
}  // namespace

bool spawnNewInstance(const std::string& dataDir, std::string& err) {
    wchar_t exe[32768];
    DWORD len = GetModuleFileNameW(nullptr, exe, (DWORD)(sizeof exe / sizeof exe[0]));
    if (len == 0 || len >= sizeof exe / sizeof exe[0]) {
        err = "cannot find the executable";
        return false;
    }
    std::wstring cmd = L"\"" + std::wstring(exe, len) + L"\"";
    if (!dataDir.empty()) {
        std::wstring d = widen(dataDir);
        while (!d.empty() && (d.back() == L'\\' || d.back() == L'/')) d.pop_back();   // a trailing \ would escape the quote
        cmd += L" --data-dir \"" + d + L"\"";
    }
    // The child's environment: ours minus the netplay overrides (they belong to THIS session).
    std::vector<wchar_t> env;
    if (wchar_t* block = GetEnvironmentStringsW()) {
        for (const wchar_t* p = block; *p; p += wcslen(p) + 1) {
            if (_wcsnicmp(p, L"VJ_NETLINK_HOST=", 16) == 0 || _wcsnicmp(p, L"VJ_NETLINK_PORT=", 16) == 0) continue;
            env.insert(env.end(), p, p + wcslen(p) + 1);
        }
        FreeEnvironmentStringsW(block);
    }
    env.push_back(L'\0');
    STARTUPINFOW si;
    ZeroMemory(&si, sizeof si);
    si.cb = sizeof si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&pi, sizeof pi);
    std::vector<wchar_t> cl(cmd.begin(), cmd.end());
    cl.push_back(L'\0');
    // bInheritHandles = FALSE: the child must not inherit our instance lock handle.
    if (!CreateProcessW(exe, cl.data(), nullptr, nullptr, FALSE, CREATE_UNICODE_ENVIRONMENT, env.data(), nullptr, &si, &pi)) {
        err = "CreateProcess failed (" + std::to_string((unsigned long)GetLastError()) + ")";
        return false;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
}

#else  // Unix

namespace {

#ifndef __APPLE__
std::string selfExecutable() {
    if (const char* ai = std::getenv("APPIMAGE"))
        if (*ai) return ai;                      // the .AppImage file, not the squashfs mount
#ifdef __linux__
    char buf[4096];
    ssize_t n = ::readlink("/proc/self/exe", buf, sizeof buf - 1);
    if (n > 0) {
        buf[n] = '\0';
        return buf;
    }
#endif
    return "";
}
#endif

}  // namespace

bool spawnNewInstance(const std::string& dataDir, std::string& err) {
    std::string exe;
    std::string exeDir;
    bool useOpen = false;
    std::string bundle;
#ifdef __APPLE__
    {
        uint32_t size = 0;
        _NSGetExecutablePath(nullptr, &size);
        std::vector<char> b(size + 1, 0);
        if (_NSGetExecutablePath(b.data(), &size) == 0) {
            char real[4096];
            exe = realpath(b.data(), real) ? real : b.data();
        }
        size_t slash = exe.find_last_of('/');
        exeDir = slash == std::string::npos ? "" : exe.substr(0, slash + 1);
        bundle = bundleOfExecutableDir(exeDir);
        useOpen = !bundle.empty();
    }
#else
    exe = selfExecutable();
#endif
    if (exe.empty() && !useOpen) {
        err = "cannot find the executable";
        return false;
    }

    std::vector<std::string> args;
    if (useOpen) {
        // LaunchServices: -n always starts a NEW instance of the bundle (a plain `open` or a
        // double-click only activates the running one).
        args = {"/usr/bin/open", "-n", bundle};
        if (!dataDir.empty()) {
            args.push_back("--args");
            args.push_back("--data-dir");
            args.push_back(dataDir);
        }
        exe = "/usr/bin/open";
    } else {
        args = {exe};
        if (!dataDir.empty()) {
            args.push_back("--data-dir");
            args.push_back(dataDir);
        }
    }
    std::vector<char*> argv;
    for (std::string& a : args) argv.push_back(&a[0]);
    argv.push_back(nullptr);

    // The child starts from our environment minus the netplay overrides (they belong to THIS
    // session; `open` hands the app a LaunchServices environment anyway).
    std::vector<std::string> envStore;
    for (char** e = environ; e && *e; e++) {
        if (std::strncmp(*e, "VJ_NETLINK_HOST=", 16) == 0 || std::strncmp(*e, "VJ_NETLINK_PORT=", 16) == 0) continue;
        envStore.emplace_back(*e);
    }
    std::vector<char*> envp;
    for (std::string& s : envStore) envp.push_back(&s[0]);
    envp.push_back(nullptr);

    pid_t pid = 0;
    int rc = posix_spawn(&pid, exe.c_str(), nullptr, nullptr, argv.data(), envp.data());
    if (rc != 0) {
        err = std::string("cannot start ") + exe + ": " + std::strerror(rc);
        return false;
    }
    // Reap it when it exits so a long-lived copy never becomes a zombie of this process.
    std::thread([pid] {
        int st = 0;
        while (waitpid(pid, &st, 0) < 0 && errno == EINTR) {}
    }).detach();
    return true;
}

#endif

}  // namespace vj
