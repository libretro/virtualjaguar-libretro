// TV mode (#991): the sizes and the motion every TV screen draws with.  All lengths are in pixels at the
// current viewport: u = viewport height / 1080, so a 4K TV and a 1080p one lay out identically.  Floats only;
// the ImGui side applies them.
#pragma once

namespace vj::tv {

struct Metrics {
    float u;                      // pixels per design unit (viewport height / 1080)
    float safeX, safeY;           // 5% title-safe margins: 96 x 54 at 1080p
    float body, caption, minText; // type: 30, 26, and the floor nothing is drawn below (24)
    float titleSmall, titleLarge; // headline range: 56 .. 84
    float ring;                   // focus ring thickness: 5
    float focusScale;             // focused tile scale: 1.08 (inside 1.05-1.10)
};
Metrics metrics(float viewportH);
// The factor that makes text `currentPx` tall (the font size in force at scale 1, DPI included) as tall as the TV body text
// (30u) at that viewport height; never below 1, so a window on a big screen grows and a small one is left alone.  0 or less
// (no font yet) is 1.
float bodyFontScale(float currentPx, float viewportH);

float easeOut(float t);           // 0..1 -> 0..1, cubic

constexpr double kTweenSeconds = 0.120;

// A value that eases to its target over kTweenSeconds; retarget() restarts from wherever it is now.
struct Tween {
    float from = 0, to = 0;
    double start = 0;
    float at(double now) const;
    void retarget(float newTo, double now);
};

}  // namespace vj::tv
