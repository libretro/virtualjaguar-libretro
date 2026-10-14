#include "paths.h"

#include "instance.h"

#include <SDL3/SDL.h>

#include <cctype>
#include <cstdio>
#include <filesystem>

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#else
#  include <unistd.h>
#endif

namespace vj {

unsigned long processId() {
#ifdef _WIN32
    return (unsigned long)GetCurrentProcessId();
#else
    return (unsigned long)getpid();
#endif
}

static std::string withSep(std::string s) {
    if (!s.empty() && s.back() != '/' && s.back() != '\\') s += '/';
    return s;
}

bool Paths::resolve(const std::string& overrideDir, std::string& err) {
    const char* base = SDL_GetBasePath();
    std::string baseDir = base ? base : "";

    if (!overrideDir.empty()) {
        root = withSep(overrideDir);
    } else if (!baseDir.empty() && fileExists(baseDir + "portable.txt")) {
        portable = true;
        root = baseDir + "data/";
    } else {
        // org = NULL: the folder is just "VirtualJaguar" under the OS's
        // per-user data location.
        char* pref = SDL_GetPrefPath(nullptr, "VirtualJaguar");
        if (!pref) {
            err = std::string("SDL_GetPrefPath failed: ") + SDL_GetError();
            return false;
        }
        root = pref;
        SDL_free(pref);
    }
    root = withSep(root);

    saves = root + "saves/";
    states = root + "states/";
    system = root + "system/";
    configFile = root + "config.ini";
    imguiIni = root + "imgui.ini";

    const std::string* dirs[] = {&root, &saves, &states, &system};
    for (const std::string* d : dirs) {
        if (!SDL_CreateDirectory(d->c_str())) {
            err = "cannot create " + *d + ": " + SDL_GetError();
            return false;
        }
    }
    return true;
}

bool Paths::applyInstance(int n, std::string& err) {
    instance = n < 1 ? 1 : n;
    if (instance == 1) return true;
    savesShared = saves;
    saves = instanceSavesDir(saves, instance);
    imguiIni = root + instanceImguiName(instance);
    instanceConfig = root + instanceConfigName(instance);
    if (!SDL_CreateDirectory(saves.c_str())) {
        err = "cannot create " + saves + ": " + SDL_GetError();
        return false;
    }
    return true;
}

std::string pathStem(const std::string& path) {
    size_t slash = path.find_last_of("/\\");
    std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
    size_t dot = name.find_last_of('.');
    return dot == std::string::npos || dot == 0 ? name : name.substr(0, dot);
}

std::string pathExt(const std::string& path) {
    size_t slash = path.find_last_of("/\\");
    size_t dot = path.find_last_of('.');
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash)) return "";
    std::string e = path.substr(dot + 1);
    for (char& c : e) c = (char)std::tolower((unsigned char)c);
    return e;
}

std::string pathDir(const std::string& path) {
    size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? "" : path.substr(0, slash + 1);
}

std::string absolutePath(const std::string& path) {
    std::error_code ec;
    std::filesystem::path p = std::filesystem::absolute(std::filesystem::u8path(path), ec);
    if (ec) return path;
    return p.lexically_normal().string();
}

bool fileExists(const std::string& path) {
    SDL_PathInfo info;
    return SDL_GetPathInfo(path.c_str(), &info) && info.type == SDL_PATHTYPE_FILE;
}

bool readFile(const std::string& path, std::vector<unsigned char>& out) {
    size_t size = 0;
    void* data = SDL_LoadFile(path.c_str(), &size);
    if (!data) return false;
    out.assign((unsigned char*)data, (unsigned char*)data + size);
    SDL_free(data);
    return true;
}

bool writeFileAtomic(const std::string& path, const void* data, size_t size) {
    // The temp name is per process: two running copies writing the same file (the library cache, a
    // thumbnail) must not share one temp file, or one could rename the other's half-written bytes.
    std::string tmp = path + "." + std::to_string(processId()) + ".tmp";
    if (!SDL_SaveFile(tmp.c_str(), data, size)) return false;
    // SDL_RenamePath replaces an existing destination on every platform SDL
    // supports (MoveFileEx with REPLACE_EXISTING on Windows).
    if (!SDL_RenamePath(tmp.c_str(), path.c_str())) {
        SDL_RemovePath(tmp.c_str());
        return false;
    }
    return true;
}

}  // namespace vj
