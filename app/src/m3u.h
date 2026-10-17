// .m3u multi-disc playlists, in RetroArch's format: one disc image per line, relative to the playlist's folder or
// absolute.  Blank lines and lines starting with '#' (#EXTM3U, #EXTINF, #LABEL) are skipped, and a "|label" suffix
// after the path is dropped.  Neither the core nor libretro.c reads these: CoreHost::loadContent boots the first disc
// and puts every disc in the core's disc list (File > Disc); the library lists the playlist instead of its discs.
//
// Header-only, so CoreHost (linked into tests without vj_library) and the library scan share one parser.
#pragma once
#include <cctype>
#include <string>
#include <vector>

namespace vj {

// The playlist's entries, in order, each joined to `dir` (the playlist's folder, with its trailing separator) unless
// it is already absolute.  No filesystem access: the caller normalises and checks that the files exist.
inline std::vector<std::string> parseM3u(const std::string& text, const std::string& dir) {
    std::vector<std::string> out;
    size_t pos = 0;
    if (text.compare(0, 3, "\xEF\xBB\xBF") == 0) pos = 3;   // a UTF-8 byte-order mark
    while (pos < text.size()) {
        size_t nl = text.find('\n', pos);
        std::string line = text.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
        pos = nl == std::string::npos ? text.size() : nl + 1;
        size_t bar = line.find('|');
        if (bar != std::string::npos) line.erase(bar);
        size_t b = 0, e = line.size();
        while (b < e && std::isspace((unsigned char)line[b])) b++;
        while (e > b && std::isspace((unsigned char)line[e - 1])) e--;   // also the CR of a CRLF file
        line = line.substr(b, e - b);
        if (line.empty() || line[0] == '#') continue;
        for (char& c : line)
            if (c == '\\') c = '/';   // a playlist written on Windows
        const bool absolute = line[0] == '/' || (line.size() >= 2 && std::isalpha((unsigned char)line[0]) && line[1] == ':');
        out.push_back(absolute ? line : dir + line);
    }
    return out;
}

}  // namespace vj
