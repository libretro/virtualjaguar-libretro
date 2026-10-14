// The Object list window (Tools > Object list...): the display list the Object Processor walked, each
// object decoded, a preview of bitmap objects with a pixel painter, PNG export and an outline over the
// game picture.  See op_view.h for the model and op_decode.h / op_render.h for the hardware side.
//
// Wiring (App::wireHost, before registerPlaceholderTools): registerObjectsTool(app).  The tool's draw hook
// (DebugTool::draw, called by drawDebugWindows every UI frame, also while halted) builds an
// ObjectsContext from the App and calls drawObjectsWindow(); the outline goes on the background draw
// list, on top of the game picture and under every window.
#pragma once

#include <functional>
#include <string>

#include "debug_session.h"

namespace vj {

class App;
class CheatManager;

struct ObjectsContext {
    // Where video.draw() put the game picture (screen points) and the core framebuffer it shows.
    float picX = 0, picY = 0, picW = 0, picH = 0;
    unsigned frameW = 0, frameH = 0;
    bool emulationRunning = true;           // false when paused: a pending snapshot needs a frame
    std::function<void()> stepFrame;        // optional: run one frame (so "Arm snapshot" can finish while paused)
    // GL texture helpers (VideoOut::createTextureRGBA / destroyTexture).
    unsigned (*createTexture)(const void* rgba, int w, int h) = nullptr;
    void (*destroyTexture)(unsigned tex) = nullptr;
    CheatManager* cheats = nullptr;         // optional: enables "Pin as cheat"
    std::string exportDir;                  // where "Export PNG" writes (created if missing); "" = the working directory
    std::string contentName;                // file-name prefix for exports
};

// Registers (or replaces) the "objects" entry of the Tools menu, its draw hook and the forced-close hook.
void registerObjectsTool(App& app);
bool objectsWindowOpen();
void setObjectsWindowOpen(bool open);
// Draws the window (when open) and the on-screen outline.  Safe to call every frame.
void drawObjectsWindow(DebugSession& session, const ObjectsContext& ctx);
// Frees the preview texture (the window frees it itself when closed or disabled).
void releaseObjectsTextures();

}  // namespace vj
