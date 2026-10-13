// Settings > Video > Shader: library status, preset picker, parameters.
#include <SDL3/SDL.h>

#include <cctype>
#include <cfloat>
#include <cstdio>
#include <mutex>
#include <string>
#include <vector>

#include "app.h"
#include "imgui.h"
#include "present_util.h"
#include "shaders.h"

namespace vj {

namespace {

// File dialogs call back on an arbitrary thread; results wait here until the UI
// thread picks them up.
struct Mailbox {
    std::mutex m;
    std::string preset;   // chosen .slangp
    std::string folder;   // chosen presets folder
};
Mailbox g_mail;

void SDLCALL presetDialogDone(void*, const char* const* files, int) {
    if (!files || !files[0]) return;
    std::lock_guard<std::mutex> lk(g_mail.m);
    g_mail.preset = files[0];
}
void SDLCALL folderDialogDone(void*, const char* const* files, int) {
    if (!files || !files[0]) return;
    std::lock_guard<std::mutex> lk(g_mail.m);
    g_mail.folder = files[0];
}

struct Listing {
    std::string root;
    std::vector<std::string> items;   // relative to root
    bool truncated = false;
    bool valid = false;
};
Listing g_user, g_builtin;
char g_filter[64] = "";
std::string g_presetError;

std::string builtinDir() {
    const char* b = SDL_GetBasePath();
    return b ? std::string(b) + "shaders/" : std::string();
}

void rescan(Listing& l, const std::string& root) {
    l.root = root;
    l.items = scanPresets(root, 5000, 10, &l.truncated);
    l.valid = true;
}

std::string lowerStr(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

void applyPreset(App& app, const std::string& path) {
    std::string err;
    ShaderChain& sc = app.video.shader;
    if (sc.usePreset(path, err)) {
        g_presetError.clear();
        sc.enabled = true;
        sc.retry();
    } else {
        g_presetError = err;
        app.toast("Shader preset not used: " + err, 6.0, 3);
    }
}

void drawList(App& app, Listing& l, const char* id, float height) {
    std::string f = lowerStr(g_filter);
    if (ImGui::BeginListBox(id, ImVec2(-FLT_MIN, height))) {
        for (const std::string& rel : l.items) {
            if (!f.empty() && lowerStr(rel).find(f) == std::string::npos) continue;
            std::string full = l.root + rel;
            bool sel = (full == app.video.shader.preset);
            if (ImGui::Selectable(rel.c_str(), sel)) applyPreset(app, full);
            if (sel) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndListBox();
    }
}

}  // namespace

void shaderFrameHook(App& app) {
    std::string p, f;
    {
        std::lock_guard<std::mutex> lk(g_mail.m);
        p.swap(g_mail.preset);
        f.swap(g_mail.folder);
    }
    if (!f.empty()) {
        if (f.back() != '/' && f.back() != '\\') f += '/';
        app.video.shader.folder = f;
        g_user.valid = false;
    }
    if (!p.empty()) applyPreset(app, p);
    std::string note;
    if (app.video.shader.takeNotice(note)) app.toast(note, 6.0, 2);
}

void drawShaderSettings(App& app) {
    ShaderChain& sc = app.video.shader;
    ImGui::SeparatorText("Shader (RetroArch .slangp presets)");

    // Library status.  Probing is a dlopen, done once and on "Check again".
    sc.probeLibrary();
    if (sc.libraryLoaded()) {
        ImGui::TextDisabled("librashader API %u: %s", sc.libraryApiVersion(), sc.libraryPath().c_str());
    } else {
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.4f, 1.0f), "librashader not available: %s", sc.libraryError().c_str());
        ImGui::TextWrapped(
            "Shaders need the librashader library (MPL-2.0). Put %s next to the app, or in the "
            "librashader folder of the data directory, or install it system-wide. Without it the "
            "picture is drawn plain; nothing else changes.",
            ShaderChain::libraryFileName());
        if (ImGui::Button("Check again")) {
            sc.probeLibrary(true);
            sc.retry();
        }
    }

    bool on = sc.enabled;
    if (ImGui::Checkbox("Use shader", &on)) {
        sc.enabled = on;
        sc.retry();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("(integer scaling and aspect above still apply)");

    ImGui::AlignTextToFramePadding();
    ImGui::Text("Preset:");
    ImGui::SameLine();
    ImGui::TextWrapped("%s", sc.preset.empty() ? "(none)" : sc.preset.c_str());
    if (ImGui::Button("Load preset file...") && app.window()) {
        static SDL_DialogFileFilter flt[2] = {{"Slang presets", "slangp"}, {"All files", "*"}};
        SDL_ShowOpenFileDialog(presetDialogDone, nullptr, app.window(), flt, 2,
                               sc.folder.empty() ? nullptr : sc.folder.c_str(), false);
    }
    ImGui::SameLine();
    if (ImGui::Button("None")) {
        sc.clearPreset();
        g_presetError.clear();
    }
    if (!g_presetError.empty()) ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.5f, 1.0f), "%s", g_presetError.c_str());
    else if (!sc.lastError().empty()) ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.5f, 1.0f), "%s", sc.lastError().c_str());

    // Built-in presets that ship with the app.
    std::string bd = builtinDir();
    if (!g_builtin.valid && !bd.empty()) rescan(g_builtin, bd);
    if (!g_builtin.items.empty()) {
        ImGui::Spacing();
        ImGui::Text("Built-in");
        drawList(app, g_builtin, "##builtin", ImGui::GetTextLineHeightWithSpacing() * 3.5f);
    }

    // A slang-shaders checkout (or any folder of presets).
    ImGui::Spacing();
    ImGui::Text("Preset folder (for example a clone of libretro/slang-shaders):");
    char buf[512];
    std::snprintf(buf, sizeof buf, "%s", sc.folder.c_str());
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - ImGui::GetFontSize() * 14.0f);
    if (ImGui::InputText("##folder", buf, sizeof buf)) {
        sc.folder = buf;
        g_user.valid = false;
    }
    ImGui::SameLine();
    if (ImGui::Button("Browse...") && app.window())
        SDL_ShowOpenFolderDialog(folderDialogDone, nullptr, app.window(),
                                 sc.folder.empty() ? nullptr : sc.folder.c_str(), false);
    ImGui::SameLine();
    if (ImGui::Button("Rescan")) g_user.valid = false;
    if (!sc.folder.empty()) {
        if (!g_user.valid || g_user.root != sc.folder) rescan(g_user, sc.folder);
        if (g_user.items.empty()) {
            ImGui::TextDisabled("No .slangp presets found in that folder.");
        } else {
            ImGui::SetNextItemWidth(ImGui::GetFontSize() * 14.0f);
            ImGui::InputTextWithHint("##filter", "filter (crt, scanline, ...)", g_filter, sizeof g_filter);
            ImGui::SameLine();
            ImGui::TextDisabled("%zu presets%s", g_user.items.size(), g_user.truncated ? " (list cut short)" : "");
            drawList(app, g_user, "##user", ImGui::GetTextLineHeightWithSpacing() * 8.0f);
        }
    }

    // Preset parameters (CRT curvature, scanline strength, ...).
    std::vector<ShaderParam>& ps = sc.params();
    if (!ps.empty() && ImGui::CollapsingHeader("Preset parameters")) {
        if (ImGui::Button("Reset parameters")) sc.resetParams();
        ImGui::BeginChild("##params", ImVec2(0, ImGui::GetTextLineHeightWithSpacing() * 10.0f), ImGuiChildFlags_Borders);
        for (size_t i = 0; i < ps.size(); i++) {
            ShaderParam& p = ps[i];
            ImGui::PushID((int)i);
            ImGui::SetNextItemWidth(ImGui::GetFontSize() * 14.0f);
            if (ImGui::SliderFloat(p.description.c_str(), &p.value, p.minimum, p.maximum, "%.3f"))
                sc.paramChanged(i);
            ImGui::PopID();
        }
        ImGui::EndChild();
    }
}

}  // namespace vj
