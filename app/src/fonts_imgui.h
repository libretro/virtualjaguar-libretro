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

// ---- previews: a typeface drawn in itself (the font list, the debugger font samples) ---------------------
//
// ImGui 1.92 loads a font's glyphs on demand, but a font still has to be ADDED to the atlas (and its file read) before
// text can be drawn with it.  The system has hundreds, so previews are lazy: a row asks for its face with preview();
// the answer is Pending until the next applyPending() (which loads at most kPreviewLoadsPerFrame files, before
// NewFrame, so the atlas is never changed mid-frame), and Ready afterwards.  Only the rows being drawn ask (the list
// uses ImGuiListClipper), so the number of faces in memory follows what is on screen; the least recently used ones
// beyond kPreviewMaxFonts / kPreviewMaxBytes are removed again.  A face that cannot be used (unreadable, no Latin
// letters, cannot draw its own name, too big) is remembered as Failed so it is not read every frame.
// The cache is emptied whenever the typeface itself is reloaded.
enum class PreviewState { Pending, Ready, Failed };
struct Preview {
    PreviewState state = PreviewState::Pending;
    ImFont* font = nullptr;     // valid for the current frame when Ready; null otherwise (draw with the UI font)
    std::string why;            // Failed: one short phrase ("no Latin letters", "too large to preview", ...)
    bool usable = true;         // Failed only: false = the face cannot be the UI font either (validateFontData failed)
};
constexpr size_t kPreviewMaxFonts = 48;
constexpr size_t kPreviewMaxBytes = 48u * 1024u * 1024u;
constexpr size_t kPreviewMaxFileBytes = 8u * 1024u * 1024u;   // bigger files are still selectable, just not previewed
constexpr size_t kPreviewLoadsPerFrame = 4;
// `key` identifies the face (its id); `text` is what will be drawn with it (the face must be able to draw it).
Preview preview(const std::string& key, const FontFile& file, const std::string& text);
Preview previewRetro();         // the built-in vector pixel font, for the "Retro" row

struct PreviewStats {
    size_t fonts = 0, bytes = 0, failed = 0, pending = 0;
    size_t loads = 0, evictions = 0;   // totals since start
};
PreviewStats previewStats();

}  // namespace fonts
}  // namespace vj
