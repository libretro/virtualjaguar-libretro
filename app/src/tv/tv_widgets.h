// TV mode (#991): the pieces every TV screen is drawn from -- a row, a picture tile, the button-hint bar, a banner --
// all on an ImDrawList, all sized from tv::Metrics (u = viewport height / 1080), all coloured from the theme palette
// (theme::pal(), so a custom theme carries over).  Nothing here owns a window or an ImGui item ID: hover and click
// are plain rectangle tests the caller makes inside its own window, so a TV window never holds ImGui's active item
// (an active item would make io.WantCaptureKeyboard true, and InputManager gates F4 and the hold edges on that; a click
// in a window's empty space activates its move ID all the same, which Shell's menu window overrides each frame).
//
// Focus is never colour alone: the focused row grows to Metrics::focusScale, gets a ring of Metrics::ring in the
// theme's text colour and a drop shadow, and eases there over kTweenSeconds (FocusTweens).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "imgui.h"
#include "theme.h"
#include "tv/tv_style.h"

namespace vj::tv {
namespace widgets {

// Tests: every string the widgets draw is also appended to `sink` (nullptr = off, the default).
void setTextProbe(std::vector<std::string>* sink);
// Tests: ... and where each one was drawn (the pen position, pixels).  Separate from the string probe so a test that wants places
// switches it on alone (nullptr = off, the default).
struct TextAt {
    std::string s;
    ImVec2 pos;
    float px;
};
void setTextPosProbe(std::vector<TextAt>* sink);

// One 0..1 "how focused" value per item of a list; moving the focus retargets two of them (the old item eases out,
// the new one eases in).  A change of `key` (another page) or of the item count starts over with no animation.
struct FocusTweens {
    std::vector<Tween> t;
    int focus = -1;
    int key = -1;
    void update(int count, int focusIndex, int pageKey, double now);
    float at(int i, double now) const;
};

struct Hit {
    bool hovered = false;   // the pointer is over the item (and over this window, not something drawn above it)
};

inline ImU32 col(theme::Color c, float alphaMul = 1.0f) { return (ImU32)c.u32(alphaMul); }

// Dims whatever is under (pos, size).
void scrim(ImDrawList* dl, ImVec2 pos, ImVec2 size, float alpha);
// A rounded panel in the theme's home colour: the surface TV text is drawn on, so it reads over any game frame.
void panel(ImDrawList* dl, const Metrics& m, ImVec2 pos, ImVec2 size);

// Text at `px` (design size x u) in `col`, cut with an ellipsis at maxW (0 = no limit) or wrapped at wrapW (0 = no wrap).
// Returns the drawn size.
ImVec2 text(ImDrawList* dl, float px, ImVec2 pos, ImU32 col, const std::string& s, float maxW = 0, float wrapW = 0);
float textWidth(float px, const std::string& s);
// The size `text` would draw `s` at, wrapped at wrapW (0 = no wrap).
ImVec2 textSize(float px, const std::string& s, float wrapW = 0);
// `s` broken into lines no wider than maxW, at spaces (never inside a word).  More than maxLines (0 = any number) and the last line
// is cut at a word, plus "..."; a single word wider than maxW cannot be broken at a space, so it is split where it must (the only
// time a word is cut), and the ellipsis does not apply to it.  UTF-8 safe.  maxW <= 0 = one line.
std::vector<std::string> wrapLines(float px, const std::string& s, float maxW, int maxLines = 0);
// Draws wrapLines' result one line under the other (`lineGap` between them), the left edge at pos.x, or -- `alignRight` -- every line
// ending at pos.x.  The text probe gets the lines joined with spaces once (the whole text, as `text` would), the position probe each
// line at its place.  Returns the block's size.
ImVec2 textLines(ImDrawList* dl, float px, ImVec2 pos, ImU32 col, const std::vector<std::string>& lines, float lineGap = 0,
                 bool alignRight = false);
// "16 Oct 2026": a date in the local time zone, from seconds since the epoch (English month names, whatever the locale).
std::string formatDate(int64_t secs);

// A list row: label (and optional lines under it), `focus` 0..1 from FocusTweens.  A disabled row is drawn muted.
// `thumb` (an OpenGL texture, 0 = none) of thumbW x thumbH design units is a picture box at the row's left: slot rows.
// `thumbSquare` makes the box a square (a badge, an icon) instead of 16:9.  `value` is right-aligned in the row's muted caption
// text and takes its room from the label (the points of an achievement).
struct RowArgs {
    std::string label, desc, sub;   // desc: a muted line between the label and `sub` (an achievement's description)
    bool enabled = true;
    float focus = 0;
    unsigned thumb = 0;
    int thumbSrcW = 0, thumbSrcH = 0;
    bool thumbBox = false;   // draw the picture box even without a picture (an empty slot)
    bool thumbSquare = false;
    std::string value;
};
Hit row(ImDrawList* dl, const Metrics& m, ImVec2 pos, ImVec2 size, const RowArgs& a);

// A cover tile (Home, Library, Game details' art): the picture fitted inside, or -- with no picture yet -- the card with the
// title wrapped at its foot; a "CD" badge top-right for a disc.  `focus` 0..1 from FocusTweens grows it to
// Metrics::focusScale about its centre and adds the ring and the shadow, as `row` does.
struct TileArgs {
    std::string title;
    bool disc = false;
    unsigned tex = 0;   // an OpenGL texture, 0 = the placeholder
    int texW = 0, texH = 0;
    float focus = 0;
};
Hit tile(ImDrawList* dl, const Metrics& m, ImVec2 pos, ImVec2 size, const TileArgs& a);

// The button-hint bar: "[A] Select   [B] Back".  Glyph "" draws the label alone.  The LAST hint is the way out (Back /
// Resume): its room is kept, so on a narrow bar the others are the ones that drop.
struct Hint {
    std::string glyph, label;
};
void hintBar(ImDrawList* dl, const Metrics& m, ImVec2 pos, float maxW, const std::vector<Hint>& hints, float px = 0);   // px 0 = Metrics::caption
// Removes the hints whose glyph is "": a button the current input device does not have (Host::glyph says so), so the bar
// lists only what works.  The screens' bars (chrome) and Home's hero buttons call it; hintBar itself keeps drawing a bare label.
void dropUnbound(std::vector<Hint>& hints);

// A banner pill (a state the player should not forget); returns its height.
float banner(ImDrawList* dl, const Metrics& m, ImVec2 pos, float w, const std::string& s);

// Up / down arrows at the edge of a list that scrolls.
void moreArrow(ImDrawList* dl, const Metrics& m, ImVec2 center, bool up);

}  // namespace widgets
}  // namespace vj::tv
