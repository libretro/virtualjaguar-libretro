// Where the app keeps its files.
//
//   normal   SDL_GetPrefPath()   (~/Library/Application Support/..., %APPDATA%, XDG)
//   portable <dir of binary>/data/   when a file named portable.txt sits next
//                                    to the executable
//   override --data-dir <dir>
#pragma once
#include <string>
#include <vector>

namespace vj {

struct Paths {
    std::string root;        // data root (always ends with a separator)
    std::string saves;       // <root>saves/    SRAM / EEPROM / Memory Track (.srm)
    std::string states;      // <root>states/   save states + thumbnails
    std::string system;      // <root>system/   BIOS files, if the user wants real ones
    std::string configFile;  // <root>config.ini
    std::string imguiIni;    // <root>imgui.ini
    bool portable = false;
    int instance = 1;        // 1 = the first copy running on this data folder (instance.h)
    std::string savesShared;      // instance > 1: the first copy's saves/ folder (read-only fallback for a game's first run)
    std::string instanceConfig;   // instance > 1: <root>config-N.ini, the only settings file it writes

    // Resolves and creates the directories.  Returns false (with `err`) if the
    // data root cannot be created.
    bool resolve(const std::string& overrideDir, std::string& err);
    // Switches to instance `n` (instance.h).  n <= 1 changes nothing.  Otherwise game saves go
    // to saves/instance-N/ (the GameDrive SD tree the core keeps under the save folder follows),
    // and imgui.ini / the settings file get -N names.  Returns false (with `err`) if the
    // save folder cannot be created.
    bool applyInstance(int n, std::string& err);
};

// Directory part / file stem / extension helpers (no filesystem access).
std::string pathStem(const std::string& path);
std::string pathExt(const std::string& path);   // lower-case, without the dot
std::string pathDir(const std::string& path);   // with trailing separator, or ""

// Absolute, lexically normalised form of `path` (relative to the current directory).
std::string absolutePath(const std::string& path);

bool fileExists(const std::string& path);
bool readFile(const std::string& path, std::vector<unsigned char>& out);
bool writeFileAtomic(const std::string& path, const void* data, size_t size);

}  // namespace vj
