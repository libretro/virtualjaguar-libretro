// Shared entry point for ordinary app windows. Overlays and child windows keep their own flags.
#pragma once
#include "imgui.h"
#include "imgui_internal.h"
namespace vj { namespace ui {
inline bool beginWindow(const char* name, bool* open, ImGuiWindowFlags flags = 0) {
    // Begin selects a viewport after taking its flags, so use the last frame's ownership. One transition
    // frame is harmless; embedded windows regain their controls on the next frame after reattachment.
    ImGuiWindow* previous = ImGui::FindWindowByName(name);
    const bool native = (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable) && previous &&
                        previous->ViewportOwned && previous->Viewport != ImGui::GetMainViewport() &&
                        !(previous->Viewport->Flags & ImGuiViewportFlags_NoDecoration);
    if (native) {
        flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse;
        ImGui::SetNextWindowCollapsed(false);   // a previously collapsed in-app window must have visible content
    }
    // Keep p_open: Begin forwards the native platform close request even without an ImGui title bar.
    return ImGui::Begin(name, open, flags);
}
} }
