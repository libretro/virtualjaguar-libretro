// Real Dear ImGui viewport selection + native close forwarding, with a fake platform backend (no display/GL).
#include "window_chrome.h"
#include <cstdio>
static int failures = 0;
#define CHECK(c) do { if (!(c)) { ++failures; std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #c); } } while (0)
static bool open = true;
static void frame(float x, bool main = false, int collapse = -1) {
    ImGui::GetIO().DeltaTime = 1.0f / 60.0f;
    ImGui::NewFrame();
    if (main) ImGui::SetNextWindowViewport(ImGui::GetMainViewport()->ID);
    ImGui::SetNextWindowPos(ImVec2(x, 100));
    ImGui::SetNextWindowSize(ImVec2(300, 200));
    if (collapse >= 0) ImGui::SetNextWindowCollapsed(collapse != 0);
    if (vj::ui::beginWindow("Chrome test", &open)) ImGui::TextUnformatted("Window contents");
    ImGui::End();
    ImGui::Render();
    for (auto* t : ImGui::GetPlatformIO().Textures) {
        if (t->Status == ImTextureStatus_WantCreate) t->SetTexID((ImTextureID)1);
        if (t->Status == ImTextureStatus_WantCreate || t->Status == ImTextureStatus_WantUpdates) t->SetStatus(ImTextureStatus_OK);
        else if (t->Status == ImTextureStatus_WantDestroy) t->SetStatus(ImTextureStatus_Destroyed);
    }
    ImGui::UpdatePlatformWindows();
}
int main() {
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2(640, 480);
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
    io.BackendFlags |= ImGuiBackendFlags_PlatformHasViewports | ImGuiBackendFlags_RendererHasViewports | ImGuiBackendFlags_RendererHasTextures;
    io.ConfigViewportsNoDecoration = false;
    auto& p = ImGui::GetPlatformIO();
    ImGuiPlatformMonitor monitor;
    monitor.MainPos = monitor.WorkPos = ImVec2(0, 0);
    monitor.MainSize = monitor.WorkSize = ImVec2(1920, 1080);
    monitor.DpiScale = 1;
    p.Monitors.push_back(monitor);
    p.Platform_CreateWindow = [](ImGuiViewport* v) { v->PlatformHandle = v; v->PlatformUserData = v; };
    p.Platform_DestroyWindow = [](ImGuiViewport* v) { v->PlatformHandle = v->PlatformUserData = nullptr; };
    p.Platform_ShowWindow = [](ImGuiViewport*) {};
    p.Platform_SetWindowPos = [](ImGuiViewport*, ImVec2) {};
    p.Platform_GetWindowPos = [](ImGuiViewport* v) { return v->Pos; };
    p.Platform_SetWindowSize = [](ImGuiViewport*, ImVec2) {};
    p.Platform_GetWindowSize = [](ImGuiViewport* v) { return v->Size; };
    p.Platform_SetWindowFocus = [](ImGuiViewport*) {};
    p.Platform_GetWindowFocus = [](ImGuiViewport*) { return true; };
    p.Platform_GetWindowMinimized = [](ImGuiViewport*) { return false; };
    p.Platform_SetWindowTitle = [](ImGuiViewport*, const char*) {};
    ImGui::GetMainViewport()->PlatformHandle = ImGui::GetCurrentContext();
    frame(40, true); frame(40, true);
    auto* w = ImGui::FindWindowByName("Chrome test");
    CHECK(w && !w->ViewportOwned && w->TitleBarHeight > 0);
    frame(40, true, 1);
    CHECK(w->Collapsed);
    frame(850); frame(850); frame(850);
    CHECK(w->ViewportOwned && !(w->Viewport->Flags & ImGuiViewportFlags_NoDecoration));
    CHECK(w->TitleBarHeight == 0 && (w->Flags & ImGuiWindowFlags_NoTitleBar));
    CHECK(!w->Collapsed);
    // Native close still forwards into the caller's show/hide state without an ImGui close button.
    w->Viewport->PlatformRequestClose = true;
    frame(850);
    CHECK(!open);
    open = true;
    frame(40, true); frame(40, true);
    CHECK(!w->ViewportOwned && w->TitleBarHeight > 0 && !(w->Flags & ImGuiWindowFlags_NoTitleBar));
    // A titlebar-less platform (or the setting off) must retain the in-app controls.
    io.ConfigViewportsNoDecoration = true;
    frame(850); frame(850); frame(850);
    CHECK(w->ViewportOwned && (w->Viewport->Flags & ImGuiViewportFlags_NoDecoration));
    CHECK(w->TitleBarHeight > 0);
    io.ConfigFlags &= ~ImGuiConfigFlags_ViewportsEnable;
    frame(40, true);
    CHECK(!w->ViewportOwned && w->TitleBarHeight > 0);
    ImGui::DestroyPlatformWindows();
    ImGui::DestroyContext();
    std::printf("window_chrome: %d failed\n", failures);
    return failures ? 1 : 0;
}
