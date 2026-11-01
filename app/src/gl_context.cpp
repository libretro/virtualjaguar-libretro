#include "gl_context.h"
#include <SDL3/SDL.h>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "imgui_impl_opengl3_loader.h"

namespace vj {
bool initOpenGLLoader() {
    auto getString = reinterpret_cast<PFNGLGETSTRINGPROC>(SDL_GL_GetProcAddress("glGetString"));
    const char* version = getString ? reinterpret_cast<const char*>(getString(GL_VERSION)) : nullptr;
    const char* vendor = getString ? reinterpret_cast<const char*>(getString(GL_VENDOR)) : nullptr;
    const char* renderer = getString ? reinterpret_cast<const char*>(getString(GL_RENDERER)) : nullptr;
    std::fprintf(stderr, "[gl] version=%s; vendor=%s; renderer=%s\n",
                 version ? version : "unavailable", vendor ? vendor : "unavailable", renderer ? renderer : "unavailable");
    if (!supportsDesktopGL33(version)) {
        SDL_SetError("Virtual Jaguar requires desktop OpenGL 3.3 or newer; this driver provides %s. "
                     "Use a compatible graphics driver or a software OpenGL implementation.", version ? version : "no current OpenGL context");
        std::fprintf(stderr, "[gl] %s\n", SDL_GetError());
        return false;
    }
#ifdef _WIN32
    // Use the same library that created the context, including SDL_OPENGL_LIBRARY.
    // ImGui's default loader independently opens opengl32.dll, which can be a
    // different (legacy system) implementation on Windows ARM / software-GL setups.
    const int result = imgl3wInit2([](const char* name) -> GL3WglProc {
        return reinterpret_cast<GL3WglProc>(SDL_GL_GetProcAddress(name));
    });
#else
    const int result = imgl3wInit();
#endif
    if (result != 0) {
        SDL_SetError("Could not initialize the OpenGL function loader (error %d).", result);
        std::fprintf(stderr, "[gl] %s\n", SDL_GetError());
        return false;
    }
    return true;
}
}
