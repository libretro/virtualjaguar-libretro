// The UI scale and the pixel density, as pure arithmetic (testable without a window).
//
// Two numbers describe a high-DPI display and they must not be mixed up:
//   * pixel density   framebuffer pixels per window point (2.0 on every Retina Mac, 1.0 elsewhere).  Dear ImGui's
//                     SDL3 backend reports it as io.DisplayFramebufferScale and rasterises glyphs at it
//                     (the font atlas bakes at size * density, and draws in points).  The app never touches it.
//   * content scale   the user's display scaling (200% on a 4K Windows/Linux display) -- the UI scale.
// SDL_GetWindowDisplayScale() is content scale * pixel density, so the UI scale divides the density back out.
#pragma once

#include <algorithm>

namespace vj {
namespace uiscale {

// UI scale = (display scale / pixel density) * the user's preference (clamped 0.5..4); a nonsense result is 1.
inline float contentScale(float displayScale, float pixelDensity, float userScale) {
    const float density = pixelDensity > 0.0f ? pixelDensity : 1.0f;
    float scale = displayScale / density;
    scale *= std::min(4.0f, std::max(0.5f, userScale));
    return scale < 0.5f ? 1.0f : scale;
}

}  // namespace uiscale
}  // namespace vj
