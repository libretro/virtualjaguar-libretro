// The Dear ImGui side of the themes: the style colours for each scheme.
#pragma once

#include "imgui.h"
#include "theme.h"

namespace vj {
namespace theme {

// Sets ONLY style.Colors[]: sizes and rounding (and the DPI scaling applied to them once at start-up) are
// left alone, so this can run again whenever the scheme in force changes.
void applyStyleColors(ImGuiStyle& style, Scheme scheme);

inline ImVec4 toVec4(Color c) {
    return ImVec4((float)c.r / 255.0f, (float)c.g / 255.0f, (float)c.b / 255.0f, (float)c.a / 255.0f);
}

}  // namespace theme
}  // namespace vj
