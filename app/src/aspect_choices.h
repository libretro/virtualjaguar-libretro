// The aspect ratios the player can pick (Settings > Video and the TV Settings row): a label and the width / height the
// presenter is told (0 = the core's own).  Its own header so the pure TV settings model can name the table without
// pulling in video.h (OpenGL, ImGui).
#pragma once

namespace vj {

struct AspectChoice {
    const char* label;
    float value;   // PresentOptions::aspectOverride
};

constexpr int kAspectChoiceCount = 4;
extern const AspectChoice kAspectChoices[kAspectChoiceCount];

}  // namespace vj
