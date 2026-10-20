#include "video.h"

#include <SDL3/SDL_video.h>

#include <algorithm>
#include <cmath>

#include "perf.h"
#include "present_util.h"

// The GL loader includes windows.h; keep its min/max macros out of the spinner maths.
#ifndef NOMINMAX
#define NOMINMAX
#endif

// Entry points come from the ImGui GL3 backend's own loader (declarations
// only; the definitions live in imgui_impl_opengl3.cpp).
#include "imgui_impl_opengl3_loader.h"

// The backend's trimmed loader has no framebuffer binding entry points or glGetFloatv: drawBusy resolves them itself
// (SDL_GL_GetProcAddress, as shaders.cpp does).
#ifndef GL_DRAW_FRAMEBUFFER
#define GL_DRAW_FRAMEBUFFER 0x8CA9
#endif
#ifndef GL_DRAW_FRAMEBUFFER_BINDING
#define GL_DRAW_FRAMEBUFFER_BINDING 0x8CA6
#endif
#ifndef GL_COLOR_CLEAR_VALUE
#define GL_COLOR_CLEAR_VALUE 0x0C22
#endif

// Not in the backend's trimmed constant list.
#ifndef GL_BGRA
#define GL_BGRA 0x80E1
#endif
#ifndef GL_RGB8
#define GL_RGB8 0x8051
#endif
#ifndef GL_RGBA8
#define GL_RGBA8 0x8058
#endif
#ifndef GL_TEXTURE_SWIZZLE_A
#define GL_TEXTURE_SWIZZLE_A 0x8E45
#endif
#ifndef GL_TEXTURE_MAX_LEVEL
#define GL_TEXTURE_MAX_LEVEL 0x813D
#endif

namespace vj {

VideoOut::~VideoOut() { shutdown(); }

bool VideoOut::init() {
    glGenTextures(1, &tex_);
    glBindTexture(GL_TEXTURE_2D, tex_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    shader.probeGlCaps();   // the preset picker marks what this GL cannot compile
    return tex_ != 0;
}

void VideoOut::shutdown() {
    shader.destroyGL();   // while the GL context is still current
    if (tex_) {
        GLuint t = tex_;
        glDeleteTextures(1, &t);
        tex_ = 0;
    }
}

void VideoOut::upload(const Frame& f) {
    if (!tex_ || f.width == 0 || f.height == 0 || f.serial == serial_) return;
    glBindTexture(GL_TEXTURE_2D, tex_);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);  // Frame is tightly packed
    // RGBA8 storage (the format every renderer, librashader's chains included,
    // can use as a source and copy), with the sampler told alpha is 1: the X byte
    // of XRGB8888 is garbage and must never reach the blend unit.
    if (f.width != w_ || f.height != h_) {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_A, GL_ONE);
        // Level 0 only.  librashader samples this texture with mipmapped min filters;
        // without a mip chain it would be "incomplete" and read as black.
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, (GLsizei)f.width, (GLsizei)f.height, 0,
                     GL_BGRA, GL_UNSIGNED_BYTE, f.pixels.data());
        w_ = f.width;
        h_ = f.height;
    } else {
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, (GLsizei)f.width, (GLsizei)f.height,
                        GL_BGRA, GL_UNSIGNED_BYTE, f.pixels.data());
    }
    serial_ = f.serial;
    emuFrames_++;
}

ImVec4 VideoOut::draw(ImDrawList* dl, ImVec2 pos, ImVec2 size, float coreAspect,
                      const PresentOptions& opt, float ps) {
    if (!tex_ || !w_ || !h_ || size.x < 1 || size.y < 1) return ImVec4(pos.x, pos.y, 0, 0);
    if (ps < 0.5f) ps = 1.0f;
    // Work in framebuffer pixels from here on.
    pos = ImVec2(pos.x * ps, pos.y * ps);
    size = ImVec2(size.x * ps, size.y * ps);

    bool wantLinear = opt.linearFilter && !opt.integerScale;
    if (wantLinear != linear_) {
        glBindTexture(GL_TEXTURE_2D, tex_);
        GLint f = wantLinear ? GL_LINEAR : GL_NEAREST;
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, f);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, f);
        linear_ = wantLinear;
    }

    PresentRect r = computePresentRect(pos.x, pos.y, size.x, size.y, w_, h_, coreAspect, opt);

    if (shader.enabled) {
        // The chain renders into a texture the size of the rectangle (whole pixels),
        // so integer scaling and aspect apply with or without a shader.
        float rw = std::floor(r.w + 0.5f), rh = std::floor(r.h + 0.5f);
        r.x = std::floor(r.x + (r.w - rw) * 0.5f);
        r.y = std::floor(r.y + (r.h - rh) * 0.5f);
        r.w = rw;
        r.h = rh;
        ShaderFrame sf;
        sf.srcTex = tex_;
        sf.srcW = w_;
        sf.srcH = h_;
        sf.outW = (unsigned)rw;
        sf.outH = (unsigned)rh;
        if (perf && perf->on && perf->forceW) {   // dev: profile the chain at another resolution
            sf.outW = perf->forceW;
            sf.outH = perf->forceH;
        }
        sf.frameCount = emuFrames_;
        sf.aspect = rh > 0 ? rw / rh : 0.0f;
        sf.fps = fps_;
        uint64_t pt0 = perf && perf->on ? perf->tick() : 0;
        unsigned out = shader.render(sf);
        if (pt0) {
            if (perf->gpu) Perf::finish();
            perf->add(Perf::Shader, 1000.0 * (double)(perf->tick() - pt0) / (double)perf->freq);
        }
        if (out) {
            // librashader writes the output texture in the same row order as its
            // input (row 0 = top of the picture), so the UVs are the plain ones.
            // (Checked by `vj_shader_tests gl`, which reads the pixels back.)
            dl->AddImage(ImTextureRef((ImTextureID)(intptr_t)out), ImVec2(r.x / ps, r.y / ps),
                         ImVec2((r.x + r.w) / ps, (r.y + r.h) / ps), ImVec2(0, 0), ImVec2(1, 1),
                         IM_COL32_WHITE);
            return ImVec4(r.x / ps, r.y / ps, r.w / ps, r.h / ps);
        }
        // fall through: plain picture
    }

    dl->AddImage(ImTextureRef((ImTextureID)(intptr_t)tex_), ImVec2(r.x / ps, r.y / ps),
                 ImVec2((r.x + r.w) / ps, (r.y + r.h) / ps), ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE);
    return ImVec4(r.x / ps, r.y / ps, r.w / ps, r.h / ps);
}

unsigned VideoOut::createTextureRGBA(const void* pixels, int w, int h) {
    GLuint t = 0;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    return t;
}

void VideoOut::destroyTexture(unsigned tex) {
    GLuint t = tex;
    if (t) glDeleteTextures(1, &t);
}

// A "busy" frame while a call() holds the core (#967): the window cleared dark, eight blocks in a ring, one lit and
// turning.  Scissored clears only (no shader, no texture), so it is safe in the middle of an ImGui frame; the GL
// state it touches is saved and restored.
void VideoOut::drawBusy(int fbW, int fbH, double seconds) {
    typedef void (*BindFramebufferFn)(GLenum target, GLuint framebuffer);
    typedef void (*GetFloatvFn)(GLenum pname, GLfloat* data);
    static BindFramebufferFn bindFbo = (BindFramebufferFn)SDL_GL_GetProcAddress("glBindFramebuffer");
    static GetFloatvFn getFloatv = (GetFloatvFn)SDL_GL_GetProcAddress("glGetFloatv");
    GLint fb = 0, vp[4], sc[4];
    GLfloat cc[4] = {0.f, 0.f, 0.f, 1.f};
    const GLboolean scissor = glIsEnabled(GL_SCISSOR_TEST);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &fb);
    glGetIntegerv(GL_VIEWPORT, vp);
    glGetIntegerv(GL_SCISSOR_BOX, sc);
    if (getFloatv) getFloatv(GL_COLOR_CLEAR_VALUE, cc);
    if (bindFbo) bindFbo(GL_DRAW_FRAMEBUFFER, 0);
    glViewport(0, 0, fbW, fbH);
    glDisable(GL_SCISSOR_TEST);
    glClearColor(0.06f, 0.06f, 0.07f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_SCISSOR_TEST);
    const int d = std::max(4, std::min(fbW, fbH) / 40);
    const double r = d * 3.0;
    const int lit = (int)(seconds * 10.0) % 8;
    for (int i = 0; i < 8; i++) {
        const double a = i * 3.14159265358979 / 4.0;
        const int x = fbW / 2 + (int)(r * std::cos(a)) - d / 2;
        const int y = fbH / 2 + (int)(r * std::sin(a)) - d / 2;
        const float v = i == lit ? 0.9f : 0.3f;
        glScissor(x, y, d, d);
        glClearColor(v, v, v, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
    }
    if (!scissor) glDisable(GL_SCISSOR_TEST);
    glScissor(sc[0], sc[1], sc[2], sc[3]);
    glViewport(vp[0], vp[1], vp[2], vp[3]);
    glClearColor(cc[0], cc[1], cc[2], cc[3]);
    if (bindFbo) bindFbo(GL_DRAW_FRAMEBUFFER, (GLuint)fb);
}

}  // namespace vj
