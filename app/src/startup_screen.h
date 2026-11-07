#pragma once
#include "imgui.h"
#include <algorithm>
#include <cmath>

namespace vj {
// Geometry only: no assets, network, game shaders, or persistent ImGui windows.
// Time is supplied by the caller so a slow content load can keep animating.
inline void drawStartupScreen(const char* status, double seconds) {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const ImVec2 p = vp->Pos, size = vp->Size;
    const float scale = std::max(0.6f, std::min(1.5f, size.x / 760.0f));
    const float cx = p.x + size.x * 0.5f, cy = p.y + size.y * 0.44f;
    dl->AddRectFilled(p, ImVec2(p.x + size.x, p.y + size.y), IM_COL32(7, 9, 13, 255));
    // A quiet raster texture, not a flashing CRT effect.
    for (float y = p.y; y < p.y + size.y; y += 4.0f * scale)
        dl->AddLine(ImVec2(p.x, y), ImVec2(p.x + size.x, y), IM_COL32(255, 255, 255, 3));
    for (int i = 0; i < 3; ++i) {
        const float x = cx + (i - 1) * 28.0f * scale;
        const float pulse = 0.5f + 0.5f * std::sin((float)seconds * 3.0f - i * 0.7f);
        const ImU32 color = IM_COL32(190 + (int)(55 * pulse), 35 + (int)(25 * pulse), 54, 255);
        dl->AddQuadFilled(ImVec2(x + 12 * scale, cy - 52 * scale),
                          ImVec2(x + 25 * scale, cy - 52 * scale),
                          ImVec2(x - 12 * scale, cy + 15 * scale),
                          ImVec2(x - 17 * scale, cy + 24 * scale), color);
    }
    auto centered = [&](const char* text, float y, ImU32 color) {
        const ImVec2 extent = ImGui::CalcTextSize(text);
        dl->AddText(ImVec2(cx - extent.x * 0.5f, y), color, text);
    };
    centered("V I R T U A L   J A G U A R", cy + 48 * scale, IM_COL32(232, 234, 238, 255));
    centered(status, cy + 83 * scale, IM_COL32(155, 163, 178, 255));
    // An indeterminate travelling highlight: no invented progress percentage.
    const float left = cx - 100 * scale, right = cx + 100 * scale;
    const float y = cy + 119 * scale;
    dl->AddLine(ImVec2(left, y), ImVec2(right, y), IM_COL32(40, 44, 54, 255), 2 * scale);
    const float phase = (float)(seconds - std::floor(seconds / 1.6) * 1.6) / 1.6f;
    const float head = left + phase * (right - left + 40 * scale);
    dl->PushClipRect(ImVec2(left, y - 3 * scale), ImVec2(right, y + 3 * scale), true);
    dl->AddLine(ImVec2(head - 40 * scale, y), ImVec2(head, y), IM_COL32(232, 55, 75, 255), 2 * scale);
    dl->PopClipRect();
}
}
