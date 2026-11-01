// Real-context regression: SDL_OPENGL_LIBRARY must also supply the UI's GL entry points.
#include "gl_context.h"
#include <SDL3/SDL.h>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "imgui_impl_opengl3_loader.h"
#include <cstring>

int main(int argc, char** argv) {
    if (!SDL_Init(SDL_INIT_VIDEO)) return 77;
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
#ifdef __APPLE__
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
#endif
    SDL_Window* window = SDL_CreateWindow("GL loader regression", 32, 32, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    SDL_GLContext context = window ? SDL_GL_CreateContext(window) : nullptr;
    if (!context) {
        std::fprintf(stderr, "SKIP no test GL context: %s\n", SDL_GetError());
        if (window) SDL_DestroyWindow(window);
        SDL_Quit();
        return 77;
    }
    auto getString = reinterpret_cast<PFNGLGETSTRINGPROC>(SDL_GL_GetProcAddress("glGetString"));
    const char* version = getString ? reinterpret_cast<const char*>(getString(GL_VERSION)) : nullptr;
    if (!vj::supportsDesktopGL33(version)) {
        std::fprintf(stderr, "SKIP driver has no desktop GL 3.3: %s\n", version ? version : "unavailable");
        SDL_GL_DestroyContext(context);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 77;
    }
    // 'legacy' replays the pre-fix independent OS-library loader for before/after evidence.
    bool loaded = argc > 1 && !std::strcmp(argv[1], "legacy") ? imgl3wInit() == 0 : vj::initOpenGLLoader();
    bool passed = false;
    if (loaded && glClearColor && glClear && glReadPixels && glGetString) {
        unsigned char pixel[4] = {};
        glClearColor(1.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        glReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
        passed = pixel[0] > 240 && pixel[1] < 15 && pixel[2] < 15;
        std::printf("GL pixel=%u,%u,%u; version=%s\n", pixel[0], pixel[1], pixel[2], glGetString(GL_VERSION));
    }
    imgl3wShutdown();
    SDL_GL_DestroyContext(context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    std::puts(passed ? "PASS GL loader and real framebuffer readback" : "FAIL GL loader or framebuffer readback");
    return passed ? 0 : 1;
}
