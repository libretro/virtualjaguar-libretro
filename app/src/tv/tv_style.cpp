#include "tv/tv_style.h"

namespace vj::tv {

Metrics metrics(float viewportH) {
    Metrics m;
    m.u = viewportH > 0 ? viewportH / 1080.0f : 1.0f;
    m.safeX = 96 * m.u;
    m.safeY = 54 * m.u;
    m.body = 30 * m.u;
    m.caption = 26 * m.u;
    m.minText = 24 * m.u;
    m.titleSmall = 56 * m.u;
    m.titleLarge = 84 * m.u;
    m.ring = 5 * m.u;
    m.focusScale = 1.08f;
    return m;
}

float bodyFontScale(float currentPx, float viewportH) {
    if (currentPx <= 0) return 1.0f;
    const float s = metrics(viewportH).body / currentPx;
    return s > 1.0f ? s : 1.0f;
}

float easeOut(float t) {
    if (t <= 0) return 0;
    if (t >= 1) return 1;
    float k = 1 - t;
    return 1 - k * k * k;
}

float Tween::at(double now) const {
    double t = (now - start) / kTweenSeconds;
    return from + (to - from) * easeOut((float)t);
}

void Tween::retarget(float newTo, double now) {
    from = at(now);
    to = newTo;
    start = now;
}

}  // namespace vj::tv
