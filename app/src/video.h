// Presents the core's XRGB8888 frame through OpenGL 3.3 core (texture + the
// ImGui draw list; no private shader or GL loader beyond what the ImGui GL3
// backend already carries).
#pragma once
#include <cstdint>

#include "core_host.h"
#include "imgui.h"

namespace vj {

struct PresentOptions {
    bool integerScale = false;
    bool linearFilter = false;
    // 0 = use the core's reported aspect, otherwise width/height override
    float aspectOverride = 0.0f;
    bool stretch = false;  // ignore aspect, fill the area
};

class VideoOut {
public:
    ~VideoOut();
    // Must be called with a current GL context after ImGui_ImplOpenGL3_Init.
    bool init();
    void shutdown();

    void upload(const Frame& f);  // no-op if f.serial already uploaded
    // Draw the frame into the given area using `dl`.  Returns the rectangle used.
    // `pixelScale` is framebuffer pixels per ImGui point (2 on a Retina display):
    // integer scaling and pixel snapping are done in framebuffer pixels.
    ImVec4 draw(ImDrawList* dl, ImVec2 areaPos, ImVec2 areaSize, float coreAspect,
                const PresentOptions& opt, float pixelScale = 1.0f);
    bool hasFrame() const { return w_ > 0; }
    unsigned frameWidth() const { return w_; }
    unsigned frameHeight() const { return h_; }

    // Small helper textures for save-state thumbnails (RGBA8, tight).
    static unsigned createTextureRGBA(const void* pixels, int w, int h);
    static void destroyTexture(unsigned tex);

private:
    unsigned tex_ = 0;
    unsigned w_ = 0, h_ = 0;
    uint64_t serial_ = 0;
    bool linear_ = false;
};

}  // namespace vj
