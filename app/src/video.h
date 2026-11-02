// Presents the core's XRGB8888 frame through OpenGL 3.3 core (texture + the
// ImGui draw list; no private shader or GL loader beyond what the ImGui GL3
// backend already carries).
#pragma once
#include <cstdint>

#include "aspect_choices.h"
#include "core_host.h"
#include "imgui.h"
#include "shaders.h"

namespace vj {

struct Perf;

struct PresentOptions {
    bool integerScale = false;
    bool linearFilter = false;
    // 0 = use the core's reported aspect, otherwise width/height override (the choices: aspect_choices.h)
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
    // Drops the picture: nothing is drawn (hasFrame() is false) until the next upload.  A content switch on the
    // emulation thread uses it so the previous game's last frame does not stay on screen (#967).
    void invalidate() { w_ = h_ = 0; serial_ = 0; }
    // Draw the frame into the given area using `dl`.  Returns the rectangle used.
    // `pixelScale` is framebuffer pixels per ImGui point (2 on a Retina display):
    // integer scaling and pixel snapping are done in framebuffer pixels.
    ImVec4 draw(ImDrawList* dl, ImVec2 areaPos, ImVec2 areaSize, float coreAspect,
                const PresentOptions& opt, float pixelScale = 1.0f);
    // A "busy" frame while a call() holds the core (#967): the window cleared dark, eight blocks in a ring, one lit and
    // turning.  Scissored clears only (no shader, no texture), so it is safe in the middle of an ImGui frame; the GL
    // state it touches is saved and restored.
    void drawBusy(int fbWidth, int fbHeight, double seconds);
    bool hasFrame() const { return w_ > 0; }
    unsigned texture() const { return tex_; }
    unsigned frameWidth() const { return w_; }
    unsigned frameHeight() const { return h_; }

    // RetroArch .slangp presets (librashader, loaded at runtime).  When it is
    // active, draw() renders the frame through it into the same rectangle the
    // plain blit would use; otherwise (disabled, library missing, preset broken)
    // the plain picture is drawn and the shader reports why via takeNotice().
    ShaderChain shader;
    // Fields per second of the running core (feeds the shaders' OriginalFPS).
    void setSourceFps(float fps) { fps_ = fps; }
    Perf* perf = nullptr;   // dev: --perf-frames timers

    // Small helper textures for save-state thumbnails (RGBA8, tight).
    static unsigned createTextureRGBA(const void* pixels, int w, int h);
    static void destroyTexture(unsigned tex);

private:
    unsigned tex_ = 0;
    unsigned w_ = 0, h_ = 0;
    uint64_t serial_ = 0;
    bool linear_ = false;
    float fps_ = 0.0f;
    uint64_t emuFrames_ = 0;   // distinct core frames seen (the shaders' FrameCount)
};

}  // namespace vj
