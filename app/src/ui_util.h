// Small layout helpers shared by every ImGui window: sizes in font units clamped to the
// main window, widths as fractions of what is left, wrap-around rows.  Header only, no
// state; call them between ImGui::NewFrame and Render like any ImGui function.
//
// The rules the app's windows follow (#833 UI polish):
//   * a window opens at a preferred size in em (font heights, so it scales with DPI),
//     never larger than ~80% of the main window, and is constrained to 95% of the
//     main window every frame, so a size saved in imgui.ini by a bigger window (or a
//     window that was later shrunk) can never overflow the viewport;
//   * a window that fits its contents (About, sign-in, keypad) has a fixed width and an
//     automatic height instead of AlwaysAutoResize on its own, which lets one long line
//     widen the window;
//   * long text is wrapped (TextWrapped / PushTextWrapPos), never a single long line;
//   * controls take a fraction of the width that is left (fitWidth) rather than a fixed
//     number of ems, with a floor and a ceiling.
#pragma once

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <string>

#include "imgui.h"
#include "theme_style.h"

namespace vj {
namespace ui {

// The theme palette (theme.h) as ImGui colours: every colour the UI draws with comes from here, so it
// reads in both the dark and the light theme.
inline const theme::Palette& pal() { return theme::pal(); }
inline const theme::Overlay& overlay() { return theme::overlay(); }
inline ImVec4 vec4(theme::Color c) { return theme::toVec4(c); }
inline ImU32 u32(theme::Color c, float alphaMul = 1.0f) { return (ImU32)c.u32(alphaMul); }

inline float em(float n) { return ImGui::GetFontSize() * n; }

inline ImVec2 workSize() { return ImGui::GetMainViewport()->WorkSize; }

// Extra OS windows (Settings > Storage > Desktop > "Let windows move outside the main window"): on, ImGui coordinates
// are the screen's and a window may live anywhere on any monitor, so the main window is not the limit any more.
inline bool outsideWindowsOn() { return (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable) != 0; }

// The monitor the main window is on (work area, screen coordinates); false when ImGui knows no monitors.
inline bool mainMonitorWork(ImVec2* pos, ImVec2* size) {
    ImGuiViewport* vp = ImGui::GetMainViewport();
    const ImVec2 c = vp->GetCenter();
    for (const ImGuiPlatformMonitor& m : ImGui::GetPlatformIO().Monitors) {
        if (c.x >= m.MainPos.x && c.x < m.MainPos.x + m.MainSize.x && c.y >= m.MainPos.y && c.y < m.MainPos.y + m.MainSize.y) {
            *pos = m.WorkPos;
            *size = m.WorkSize;
            return m.WorkSize.x > 0 && m.WorkSize.y > 0;
        }
    }
    return false;
}

// How big a window may get: 95% of the main window, or of its monitor's work area when windows may leave the main one.
inline ImVec2 maxWindowSize() {
    ImVec2 ws = ImGui::GetMainViewport()->WorkSize;
    if (outsideWindowsOn()) {
        ImVec2 p, s;
        if (mainMonitorWork(&p, &s)) ws = ImVec2(std::max(ws.x, s.x), std::max(ws.y, s.y));
    }
    return ImVec2(ws.x * 0.95f, ws.y * 0.95f);
}

// Width for the next control: `frac` of the space left in the window, at least `minEm`,
// at most `maxEm`, and never more than the space left.
inline float fitWidth(float frac, float minEm, float maxEm) {
    float avail = ImGui::GetContentRegionAvail().x;
    float w = std::min(std::max(avail * frac, em(minEm)), em(maxEm));
    return std::max(1.0f, std::min(w, avail));
}

enum class WinFit {
    Resizable,      // preferred size the first time, then whatever the user chose (within the viewport)
    FixedWidth,     // fixed width, height follows the contents (use with AlwaysAutoResize)
};

// Call right before ImGui::Begin().
inline void nextWindow(float prefWEm, float prefHEm, float minWEm, float minHEm, WinFit fit = WinFit::Resizable) {
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImVec2 ws = vp->WorkSize;
    const ImVec2 mx = maxWindowSize();
    float maxW = mx.x, maxH = mx.y;
    float prefW = std::min(em(prefWEm), ws.x * 0.8f), prefH = std::min(em(prefHEm), ws.y * 0.85f);
    float minW = std::min(em(minWEm), maxW), minH = std::min(em(minHEm), maxH);
    ImVec2 centre(vp->WorkPos.x + ws.x * 0.5f, vp->WorkPos.y + ws.y * 0.5f);
    if (outsideWindowsOn() && fit == WinFit::Resizable && (em(prefWEm) > ws.x * 0.8f || em(prefHEm) > ws.y * 0.85f)) {
        // The main window is too small for this one: open it at its full size in the middle of the monitor, which
        // is bigger than the main window, so ImGui makes it an OS window of its own.
        ImVec2 mp, ms;
        if (mainMonitorWork(&mp, &ms)) {
            prefW = std::min(em(prefWEm), ms.x * 0.8f);
            prefH = std::min(em(prefHEm), ms.y * 0.85f);
            centre = ImVec2(mp.x + ms.x * 0.5f, mp.y + ms.y * 0.5f);
        }
    }
    ImGui::SetNextWindowPos(centre, ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
    if (fit == WinFit::FixedWidth) {
        float w = std::min(em(prefWEm), maxW);
        ImGui::SetNextWindowSizeConstraints(ImVec2(w, 0.0f), ImVec2(w, maxH));
    } else {
        ImGui::SetNextWindowSize(ImVec2(prefW, prefH), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSizeConstraints(ImVec2(minW, minH), ImVec2(maxW, maxH));
    }
}

// Call right after ImGui::Begin() returned (true or false): a window whose saved position
// is outside the (now smaller) main window is pulled back inside.
inline void keepOnScreen() {
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImVec2 p = ImGui::GetWindowPos(), s = ImGui::GetWindowSize();
    if (outsideWindowsOn()) {
        // An extra OS window is the OS's to place (it keeps it on a monitor).  A window still in the main one is only
        // rescued when a saved position left it on no monitor at all (a display that is gone).
        if (ImGui::GetWindowViewport() != vp) return;
        const ImVec2 c(p.x + s.x * 0.5f, p.y + s.y * 0.5f);
        bool seen = false, onAny = false;
        for (const ImGuiPlatformMonitor& m : ImGui::GetPlatformIO().Monitors) {
            seen = true;
            if (c.x >= m.WorkPos.x && c.x < m.WorkPos.x + m.WorkSize.x && c.y >= m.WorkPos.y && c.y < m.WorkPos.y + m.WorkSize.y)
                onAny = true;
        }
        if (!seen || onAny) return;
    }
    ImVec2 lo = vp->WorkPos, hi(vp->WorkPos.x + vp->WorkSize.x, vp->WorkPos.y + vp->WorkSize.y);
    ImVec2 n(std::min(std::max(p.x, lo.x), std::max(lo.x, hi.x - s.x)),
             std::min(std::max(p.y, lo.y), std::max(lo.y, hi.y - s.y)));
    if (n.x != p.x || n.y != p.y) ImGui::SetWindowPos(n);
}

// Wrapped text in the disabled colour.
inline void hint(const char* fmt, ...) IM_FMTARGS(1);
inline void hint(const char* fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("%s", buf);
    ImGui::PopStyleColor();
}

inline void coloredWrapped(const ImVec4& c, const char* text) {
    ImGui::PushStyleColor(ImGuiCol_Text, c);
    ImGui::TextWrapped("%s", text);
    ImGui::PopStyleColor();
}

// A tooltip whose text wraps at a sensible width (and never wider than the main window).
inline void beginWrappedTooltip(float maxEm = 30.0f) {
    ImGui::BeginTooltip();
    ImGui::PushTextWrapPos(std::min(em(maxEm), workSize().x * 0.6f));
}
inline void endWrappedTooltip() {
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
}

// ---- rows that wrap: put the next item on this line only if it still fits ------------------
struct Flow {
    float right = 0;   // screen x of the right edge of the row
    Flow() { right = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x; }
    // Call before each item except the first, with the item's width.
    void next(float w) const {
        if (ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x + w <= right) ImGui::SameLine();
    }
    static float buttonW(const char* label) {
        return ImGui::CalcTextSize(label, nullptr, true).x + ImGui::GetStyle().FramePadding.x * 2.0f;
    }
    static float checkW(const char* label) {
        return ImGui::GetFrameHeight() + ImGui::GetStyle().ItemInnerSpacing.x + ImGui::CalcTextSize(label, nullptr, true).x;
    }
};

}  // namespace ui
}  // namespace vj
