#include "theme_style.h"

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

}  // namespace

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
