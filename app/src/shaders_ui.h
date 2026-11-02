// The shader presets as the TV Settings row lists and applies them (shaders_ui.cpp).
#pragma once
#include <string>
#include <vector>

namespace vj {

class App;

// Every preset the desktop list offers, as full paths: the built-in ones, then the user's folder.  Skips the ones a GL
// without arrays of arrays cannot compile.  Scanned on first use and when the folder changed.
std::vector<std::string> shaderPresetPaths(App& app);
// Uses a preset and turns the shader on; a preset that does not load is a toast and the previous one stays.
void applyShaderPreset(App& app, const std::string& path);

}  // namespace vj
