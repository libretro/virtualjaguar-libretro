// Store file selection retains the NSURL security scope before returning paths to SDL clients.
#pragma once
#include <SDL3/SDL.h>
namespace vj {
class Config;
namespace files {
#ifdef VJ_MAC_APP_STORE
void restore(Config& cfg);
void shutdown();
void remember(const char* path); // Finder open/drag-and-drop: user granted this path, never an arbitrary parent
void forget(const char* path);
void openFile(SDL_DialogFileCallback cb, void* ud, SDL_Window* parent, const SDL_DialogFileFilter* filters,
              int count, const char* location, bool multiple);
void openFolder(SDL_DialogFileCallback cb, void* ud, SDL_Window* parent, const char* location, bool multiple);
void saveFile(SDL_DialogFileCallback cb, void* ud, SDL_Window* parent, const SDL_DialogFileFilter* filters,
              int count, const char* location);
#else
inline void restore(Config&) {}
inline void shutdown() {}
inline void remember(const char*) {}
inline void forget(const char*) {}
inline void openFile(SDL_DialogFileCallback cb, void* ud, SDL_Window* p, const SDL_DialogFileFilter* f,
                     int n, const char* l, bool m) { SDL_ShowOpenFileDialog(cb, ud, p, f, n, l, m); }
inline void openFolder(SDL_DialogFileCallback cb, void* ud, SDL_Window* p, const char* l, bool m) {
    SDL_ShowOpenFolderDialog(cb, ud, p, l, m);
}
inline void saveFile(SDL_DialogFileCallback cb, void* ud, SDL_Window* p, const SDL_DialogFileFilter* f,
                     int n, const char* l) { SDL_ShowSaveFileDialog(cb, ud, p, f, n, l); }
#endif
} // namespace files
} // namespace vj
