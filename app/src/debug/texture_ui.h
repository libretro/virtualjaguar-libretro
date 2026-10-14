// The Textures window (Tools > Textures...): turn texture dumping on, browse what the game has used,
// export a tile's PNG, replace its art with a PNG of your own and have the core pick it up.
// The logic is in texture_pack.*; this file is the ImGui side.
//
// Entry points (no App dependency; App::wireHost / App::shutdown call them):
//   textureToolInit(...)     once, after paths.applyInstance(): registers the Tools entry "textures"
//                            (the window itself is drawn by addDebugTools's provider calling textureToolDraw() every UI frame)
//   textureToolDraw()        the window, called by that hook; cheap when closed
//   textureToolShutdown()    before the GL context is destroyed: frees the thumbnails
#pragma once

#include <functional>

struct SDL_Window;

namespace vj {

class CoreHost;
class DebugSession;
struct Paths;

// `window` is asked for when a file dialog opens (the main window may not exist yet at start-up).
void textureToolInit(DebugSession& session, CoreHost& host, const Paths& paths, std::function<SDL_Window*()> window);
void textureToolDraw();
void textureToolShutdown();

}  // namespace vj
