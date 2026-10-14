// GL-free presentation helpers shared by the plain blit, the shader path and
// the headless tests: where the picture goes in the window, and RetroArch
// .slangp preset parsing / validation / discovery.
#pragma once
#include <cstddef>
#include <string>
#include <vector>

#include "video.h"

namespace vj {

// ---- placement ---------------------------------------------------------------------------

struct PresentRect {
    float x = 0, y = 0, w = 0, h = 0;
};

// Where a srcW x srcH picture lands inside the area (all in framebuffer pixels):
// the core's aspect (or the override), stretched, or the largest whole multiple
// of the source height that fits (integer scaling).  Independent of shaders: the
// plain blit and a shader chain both draw into the rectangle this returns.
PresentRect computePresentRect(float areaX, float areaY, float areaW, float areaH,
                               unsigned srcW, unsigned srcH, float coreAspect,
                               const PresentOptions& opt);

// ---- .slangp presets ---------------------------------------------------------------------

struct PresetInfo {
    bool ok = false;
    std::string error;                    // one line, set when !ok
    std::string path;                     // as given
    std::vector<std::string> shaders;     // resolved shaderN paths, pass order
    std::vector<std::string> textures;    // resolved texture files (LUTs)
    std::vector<std::string> references;  // #reference chain, outermost first
};

// True for "*.slangp" (case-insensitive).  Only slang presets run on the GL
// path; .glslp / .cgp are the old formats and are rejected.
bool hasPresetExtension(const std::string& path);

// Parses a preset far enough to say, in plain words, why it cannot work: file
// missing, wrong type, no `shaders =` count, a shaderN or texture file that does
// not exist, a #reference loop.  Does NOT compile anything.  Never throws.
PresetInfo inspectPreset(const std::string& path);

// All *.slangp under `root` (skipping .git), sorted, as paths relative to root
// with '/' separators.  `truncated` is set when `maxResults` or `maxDepth` cut
// the walk short.  Never throws; an unreadable root yields an empty list.
std::vector<std::string> scanPresets(const std::string& root, size_t maxResults = 5000,
                                     int maxDepth = 10, bool* truncated = nullptr);

}  // namespace vj
