#include "theme_style.h"

#include <cmath>

namespace vj {
namespace theme {

namespace {

ImVec4 c(int r, int g, int b, float a = 1.0f) { return ImVec4(r / 255.0f, g / 255.0f, b / 255.0f, a); }

// The light look: Dear ImGui's light style (the same blue accent as the dark one) with the neutrals tuned
// to the dark theme's feel -- a faintly cool grey page, inset (tinted) inputs, ink-blue text -- and the quiet text
// darkened so hints (which are whole paragraphs here) stay readable.
void light(ImGuiStyle& st) {
    ImGui::StyleColorsLight(&st);
    ImVec4* k = st.Colors;
    k[ImGuiCol_Text] = c(28, 30, 36);
    k[ImGuiCol_TextDisabled] = c(96, 100, 110);
    k[ImGuiCol_WindowBg] = c(246, 247, 250);
    k[ImGuiCol_PopupBg] = c(252, 253, 255, 0.98f);
    k[ImGuiCol_Border] = c(120, 128, 144, 0.45f);
    k[ImGuiCol_FrameBg] = c(226, 231, 241);
    k[ImGuiCol_MenuBarBg] = c(228, 231, 238);
    k[ImGuiCol_TitleBg] = c(232, 235, 242);
    k[ImGuiCol_TitleBgActive] = c(208, 218, 238);
    k[ImGuiCol_TitleBgCollapsed] = c(232, 235, 242, 0.75f);
    k[ImGuiCol_ScrollbarBg] = c(236, 238, 243, 0.8f);
    k[ImGuiCol_ScrollbarGrab] = c(180, 186, 198);
    k[ImGuiCol_Separator] = c(150, 158, 174, 0.7f);
    k[ImGuiCol_CheckMark] = c(37, 99, 235);
    k[ImGuiCol_SliderGrab] = c(66, 133, 244, 0.85f);
    k[ImGuiCol_PlotHistogram] = c(66, 133, 244);
    k[ImGuiCol_TableHeaderBg] = c(214, 224, 242);
    k[ImGuiCol_TableBorderStrong] = c(160, 168, 184);
    k[ImGuiCol_TableBorderLight] = c(200, 206, 218);
    k[ImGuiCol_TableRowBgAlt] = c(0, 0, 0, 0.035f);
    k[ImGuiCol_TextSelectedBg] = c(66, 150, 250, 0.35f);
}

ImVec4 lerp4(ImVec4 a, ImVec4 b, float t) { return ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t); }
ImVec4 alpha(ImVec4 v, float a) { return ImVec4(v.x, v.y, v.z, a); }

}  // namespace

void applyCustomColors(ImGuiStyle& st, const Custom& cu) {
    applyStyleColors(st, cu.base);   // the base look first: whatever the theme does not decide keeps it
    ImVec4* k = st.Colors;
    auto col = [&](Key key) { return toVec4(cu.colors[key]); };
    const ImVec4 window = col(KeyWindow), frame = col(KeyFrame), text = col(KeyText), muted = col(KeyMuted), accent = col(KeyAccent);
    k[ImGuiCol_Text] = text;
    k[ImGuiCol_TextDisabled] = muted;
    k[ImGuiCol_WindowBg] = alpha(window, 1.0f);
    k[ImGuiCol_ChildBg] = alpha(window, 0.0f);
    k[ImGuiCol_PopupBg] = alpha(window, 0.98f);
    k[ImGuiCol_Border] = alpha(lerp4(window, text, 0.30f), 0.65f);
    k[ImGuiCol_FrameBg] = frame;
    k[ImGuiCol_FrameBgHovered] = alpha(accent, 0.40f);
    k[ImGuiCol_FrameBgActive] = alpha(accent, 0.67f);
    k[ImGuiCol_TitleBg] = lerp4(window, text, 0.08f);
    k[ImGuiCol_TitleBgActive] = lerp4(window, accent, 0.40f);
    k[ImGuiCol_TitleBgCollapsed] = alpha(k[ImGuiCol_TitleBg], 0.75f);
    k[ImGuiCol_MenuBarBg] = lerp4(window, text, 0.07f);
    k[ImGuiCol_ScrollbarBg] = alpha(window, 0.6f);
    k[ImGuiCol_ScrollbarGrab] = lerp4(window, text, 0.30f);
    k[ImGuiCol_ScrollbarGrabHovered] = lerp4(window, text, 0.42f);
    k[ImGuiCol_ScrollbarGrabActive] = lerp4(window, text, 0.55f);
    k[ImGuiCol_CheckMark] = accent;
    k[ImGuiCol_SliderGrab] = alpha(accent, 0.85f);
    k[ImGuiCol_SliderGrabActive] = accent;
    k[ImGuiCol_Button] = alpha(accent, 0.40f);
    k[ImGuiCol_ButtonHovered] = accent;
    k[ImGuiCol_ButtonActive] = lerp4(accent, window, 0.25f);
    k[ImGuiCol_Header] = alpha(accent, 0.31f);
    k[ImGuiCol_HeaderHovered] = alpha(accent, 0.80f);
    k[ImGuiCol_HeaderActive] = accent;
    k[ImGuiCol_Separator] = alpha(lerp4(window, text, 0.30f), 0.8f);
    k[ImGuiCol_SeparatorHovered] = alpha(accent, 0.78f);
    k[ImGuiCol_SeparatorActive] = accent;
    k[ImGuiCol_ResizeGrip] = alpha(accent, 0.20f);
    k[ImGuiCol_ResizeGripHovered] = alpha(accent, 0.67f);
    k[ImGuiCol_ResizeGripActive] = alpha(accent, 0.95f);
    // Tabs follow Dear ImGui's own recipe (headers blended toward the active title bar).
    k[ImGuiCol_Tab] = lerp4(k[ImGuiCol_Header], k[ImGuiCol_TitleBgActive], 0.80f);
    k[ImGuiCol_TabHovered] = k[ImGuiCol_HeaderHovered];
    k[ImGuiCol_TabSelected] = lerp4(k[ImGuiCol_HeaderActive], k[ImGuiCol_TitleBgActive], 0.60f);
    k[ImGuiCol_TabSelectedOverline] = k[ImGuiCol_HeaderActive];
    k[ImGuiCol_TabDimmed] = lerp4(k[ImGuiCol_Tab], k[ImGuiCol_TitleBg], 0.80f);
    k[ImGuiCol_TabDimmedSelected] = lerp4(k[ImGuiCol_TabSelected], k[ImGuiCol_TitleBg], 0.40f);
    k[ImGuiCol_PlotHistogram] = accent;
    k[ImGuiCol_TableHeaderBg] = lerp4(window, accent, 0.25f);
    k[ImGuiCol_TableBorderStrong] = lerp4(window, text, 0.35f);
    k[ImGuiCol_TableBorderLight] = lerp4(window, text, 0.20f);
    k[ImGuiCol_TextSelectedBg] = alpha(accent, 0.35f);
}

void applyStyleMetrics(ImGuiStyle& st, float rounding, float spacing, float uiScale) {
    const ImGuiStyle def;   // the stock padding and spacing
    auto px = [&](float v) { return std::floor(v * spacing * uiScale); };
    auto px2 = [&](ImVec2 v) { return ImVec2(px(v.x), px(v.y)); };
    st.WindowRounding = std::floor(rounding * uiScale);
    st.FrameRounding = std::floor(std::fmax(0.0f, rounding - 1.0f) * uiScale);
    st.WindowPadding = px2(def.WindowPadding);
    st.FramePadding = px2(def.FramePadding);
    st.ItemSpacing = px2(def.ItemSpacing);
    st.ItemInnerSpacing = px2(def.ItemInnerSpacing);
    st.CellPadding = px2(def.CellPadding);
}

void applyStyle(ImGuiStyle& st, float uiScale) {
    if (const Custom* c = activeCustom()) {
        applyCustomColors(st, *c);
        applyStyleMetrics(st, c->rounding, c->spacing, uiScale);
    } else {
        applyStyleColors(st, effective());
        applyStyleMetrics(st, kDefaultRounding, kDefaultSpacing, uiScale);
    }
}

void applyStyleColors(ImGuiStyle& st, Scheme scheme) {
    if (scheme == Scheme::Light) {
        light(st);
    } else {
        ImGui::StyleColorsDark(&st);
        // Dear ImGui's dark TextDisabled (grey 0.5) is 3.4:1 on its own input fields, and hints here are whole paragraphs.
        st.Colors[ImGuiCol_TextDisabled] = c(170, 170, 174);
    }
    // Opaque windows: the dark theme's 0.94 alpha let the home screen's text show through
    // (About, Settings).  Overlays that want to be see-through set SetNextWindowBgAlpha.
    st.Colors[ImGuiCol_WindowBg].w = 1.0f;
}

}  // namespace theme
}  // namespace vj
