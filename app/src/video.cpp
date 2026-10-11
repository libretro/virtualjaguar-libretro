#include "video.h"

#include <cmath>

// Entry points come from the ImGui GL3 backend's own loader (declarations
// only; the definitions live in imgui_impl_opengl3.cpp).
#include "imgui_impl_opengl3_loader.h"

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

namespace vj {

VideoOut::~VideoOut() { shutdown(); }

bool VideoOut::init() {
    glGenTextures(1, &tex_);
    glBindTexture(GL_TEXTURE_2D, tex_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return tex_ != 0;
}

void VideoOut::shutdown() {
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
    // GL_RGB8 internal format: the X byte of XRGB8888 is garbage and must never
    // reach the blend unit, so the sampler is told alpha is 1.
    if (f.width != w_ || f.height != h_) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, (GLsizei)f.width, (GLsizei)f.height, 0,
                     GL_BGRA, GL_UNSIGNED_BYTE, f.pixels.data());
        w_ = f.width;
        h_ = f.height;
    } else {
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, (GLsizei)f.width, (GLsizei)f.height,
                        GL_BGRA, GL_UNSIGNED_BYTE, f.pixels.data());
    }
    serial_ = f.serial;
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

    float aspect = opt.aspectOverride > 0.0f ? opt.aspectOverride
                 : coreAspect > 0.0f ? coreAspect : (float)w_ / (float)h_;
    float dw, dh;
    if (opt.stretch) {
        dw = size.x;
        dh = size.y;
    } else {
        dh = size.y;
        dw = dh * aspect;
        if (dw > size.x) {
            dw = size.x;
            dh = dw / aspect;
        }
        if (opt.integerScale) {
            // Largest whole multiple of the source height that fits, never below 1x
            // unless the window is smaller than the source.
            float k = std::floor(dh / (float)h_);
            if (k >= 1.0f) {
                dh = k * (float)h_;
                dw = dh * aspect;
                if (dw > size.x) {
                    float kw = std::floor(size.x / (aspect * (float)h_));
                    if (kw >= 1.0f) {
                        dh = kw * (float)h_;
                        dw = dh * aspect;
                    }
                }
            }
        }
    }
    float x = std::floor(pos.x + (size.x - dw) * 0.5f);
    float y = std::floor(pos.y + (size.y - dh) * 0.5f);
    dl->AddImage(ImTextureRef((ImTextureID)(intptr_t)tex_), ImVec2(x / ps, y / ps),
                 ImVec2((x + dw) / ps, (y + dh) / ps), ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE);
    return ImVec4(x / ps, y / ps, dw / ps, dh / ps);
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

}  // namespace vj
