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

    // Resolves and creates the directories.  Returns false (with `err`) if the
    // data root cannot be created.
    bool resolve(const std::string& overrideDir, std::string& err);
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
