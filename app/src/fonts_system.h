// The fonts installed on this machine (#935): which files exist and what each one is called, so the font picker can
// list them.  No ImGui and no GL (the headless tests cover it); the preview cache that draws the names in their own
// faces is fonts_imgui.h, the list widget is fonts_ui.cpp.
//
// How: the standard font folders of the OS are walked (SDL's directory enumeration, so the same code serves macOS,
// Windows and Linux) and only the *header* of every .ttf / .otf / .ttc / .otc is read -- the table directory, then the
// small `name`, `OS/2` and `post` tables -- never the whole file.  About 700 faces on a Mac take a few tens of
// milliseconds; the walk runs on a worker thread, started the first time the picker is shown, never at start-up.
//
//   macOS    /System/Library/Fonts[/Supplemental], /Library/Fonts, ~/Library/Fonts
//   Windows  %WINDIR%\Fonts, %LOCALAPPDATA%\Microsoft\Windows\Fonts
//   Linux    /usr/share/fonts, /usr/local/share/fonts, ~/.fonts, $XDG_DATA_HOME/fonts, $XDG_DATA_DIRS/fonts
//            (the folders fontconfig itself indexes by default, found without needing fontconfig installed)
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "fonts.h"

namespace vj {
namespace fonts {

struct SystemFont {
    std::string path;
    int index = 0;                  // face inside a .ttc collection
    std::string family;             // "Helvetica Neue"
    std::string style;              // "Bold Italic" ("" = Regular)
    std::string name;               // what the list shows: "Helvetica Neue Bold Italic"
    int weight = 400;               // OS/2 usWeightClass
    bool italic = false;
    bool mono = false;              // post.isFixedPitch: usable as a debugger font
    std::string id() const;
};

// "path|index" -- how a face is named in the settings and in the favorites / recents lists.
std::string makeFontId(const std::string& path, int index);
bool parseFontId(const std::string& id, FontFile& out);

// The folders worth scanning on `os` (existing or not), from the environment `p` describes.
std::vector<std::string> systemFontDirs(Os os, const Probe& p);

// Reads face `faceIndex` of one file: names, weight, italic and fixed-pitch flags, from the header and three small
// tables only.  False for anything that is not a usable outline font.  `numFaces` is the face count of the file.
bool readFaceInfo(const std::string& path, int faceIndex, SystemFont& out, int* numFaces = nullptr);

// Every face under `dirs` (recursive, depth-capped), de-duplicated by name and sorted family / weight / style.
std::vector<SystemFont> scanFontDirs(const std::vector<std::string>& dirs, size_t maxFiles = 8000);

// ---- the machine's list, scanned once on a worker thread --------------------------------------------------------
void systemFontsStart();                                      // idempotent; returns at once
bool systemFontsReady();
std::shared_ptr<const std::vector<SystemFont>> systemFonts(); // null until the scan finished
// The face with this id, or null (not scanned yet / no longer installed).
const SystemFont* findSystemFont(const std::vector<SystemFont>& list, const std::string& id);

}  // namespace fonts
}  // namespace vj
