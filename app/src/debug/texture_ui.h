// The Textures window (Tools > Textures...): turn texture dumping on, browse what the game has used,
// export a tile's PNG, replace its art with a PNG of your own and have the core pick it up.
// The logic is in texture_pack.*; this file is the ImGui side.
//
// Entry points (no App dependency; phase B wires them where it owns the main loop):
//   textureToolInit(...)     once, after the DebugSession exists: registers the Tools entry "textures"
//   textureToolDraw()        every UI frame (cheap when the window is closed)
//   textureToolShutdown()    before the GL context is destroyed: frees the thumbnails
#pragma once

struct SDL_Window;

namespace vj {

class CoreHost;
class DebugSession;
struct Paths;

void textureToolInit(DebugSession& session, CoreHost& host, const Paths& paths, SDL_Window* window);
void textureToolDraw();
void textureToolShutdown();

}  // namespace vj
