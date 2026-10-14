// The Dear ImGui half of the font selection (fonts.h has the file lookup and the settings).
//
// Dear ImGui 1.92 keeps a DYNAMIC font atlas: glyphs are rasterised on demand, at the size and pixel
// density they are drawn at (ImGuiBackendFlags_RendererHasTextures, which the OpenGL3 backend sets;
// the SDL3 backend reports the framebuffer scale, so a 2x Retina display bakes glyphs at 2x).  Changing
// the typeface is therefore ClearFonts() + AddFont*() between two frames; changing only the size is
// one number (ImGuiStyle::FontSizeBase), no atlas work.
//
//   fonts::request(settings)   remember what is wanted (any time, any thread that owns the UI)
//   fonts::applyPending()      call once per frame BEFORE ImGui::NewFrame(); does the atlas work
//   fonts::mono()              the monospace face (hex dumps, disassembly); never cache the pointer
#pragma once

#include <string>

#include "fonts.h"
#include "imgui.h"

namespace vj {
namespace fonts {

// Where custom fonts live (<data>/fonts/, ends with a separator).  Call before the first applyPending().
void setCustomDir(const std::string& dir);
const std::string& customDir();

void request(const Settings& s);
// True when something changed and the atlas was rebuilt or the size was updated.
bool applyPending();

// What is in effect (after fallbacks): e.g. Custom requested but the file was bad -> Retro.
const Settings& active();
// One sentence about a fallback that happened ("" if none); cleared by the next successful request.
const std::string& warning();
// The file the UI font was loaded from ("" for Retro) and the monospace one ("" = ProggyClean).
const FontFile& uiFile();
const FontFile& monoFile();
// Whether this build rasterises with FreeType (VJ_APP_FREETYPE) -- hinting / anti-aliasing need it.
bool freetypeAvailable();

// The monospace face, or null before the first applyPending().  Prefer pushMono() / popMono().
ImFont* mono();
// PushFont(mono(), current size) / PopFont().  Safe before the first apply (then it pushes the
// current font so the pop still balances).
void pushMono();
void popMono();

}  // namespace fonts
}  // namespace vj
