// The Dear ImGui side of the themes: the style colours for each scheme.
#pragma once

#include "imgui.h"
#include "theme.h"
#include "theme_custom.h"

namespace vj {
namespace theme {

// Sets ONLY style.Colors[]: sizes and rounding (and the DPI scaling applied to them once at start-up) are
// left alone, so this can run again whenever the scheme in force changes.
void applyStyleColors(ImGuiStyle& style, Scheme scheme);

// A custom theme: Dark or Light underneath (its base), then the user's colours; the many widget colours (buttons,
// headers, tabs, scrollbars, borders ...) are derived from the few the theme has, so editing "Accent" restyles all of them.
void applyCustomColors(ImGuiStyle& style, const Custom& c);

// Rounding and spacing at a UI scale (the display's content scale, times the user's ui.scale): the stock values
// times `spacing`, truncated to whole pixels exactly as ImGuiStyle::ScaleAllSizes does, so rounding 4 / spacing 1
// is the look the app always had.  Fonts are not touched here (FontScaleDpi / the font settings own them).
void applyStyleMetrics(ImGuiStyle& style, float rounding, float spacing, float uiScale);

// Everything for the theme in force (theme::effective() / theme::activeCustom()): colours, then metrics.
void applyStyle(ImGuiStyle& style, float uiScale);

inline ImVec4 toVec4(Color c) {
    return ImVec4((float)c.r / 255.0f, (float)c.g / 255.0f, (float)c.b / 255.0f, (float)c.a / 255.0f);
}

}  // namespace theme
}  // namespace vj
