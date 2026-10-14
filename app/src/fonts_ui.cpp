#include "fonts_ui.h"

#include <mutex>
#include <string>

#include <SDL3/SDL.h>

#include "app.h"
#include "fonts_imgui.h"
#include "imgui.h"
#include "ui_util.h"

namespace vj {

namespace {

// The file dialog answers on another thread (SDL_ShowOpenFileDialog); the answer waits here for the UI.
std::mutex g_mu;
std::string g_picked;
bool g_pickedReady = false;
std::string g_message;       // result of the last import
bool g_messageIsError = false;

void SDLCALL fontDialogDone(void*, const char* const* list, int) {
    std::lock_guard<std::mutex> lk(g_mu);
    if (list && list[0]) {
        g_picked = list[0];
        g_pickedReady = true;
    }
}

void openFontDialog(App& app) {
    static SDL_DialogFileFilter flt[2] = {{"Font files", "ttf;otf;ttc"}, {"All files", "*"}};
    if (app.window()) SDL_ShowOpenFileDialog(fontDialogDone, nullptr, app.window(), flt, 2, nullptr, false);
}

// The OS lookups are cached: on Linux resolving asks fontconfig (a child process).
const fonts::FontFile& systemFace(fonts::Role r) {
    static fonts::FontFile ui, mono;
    static bool done = false;
    if (!done) {
        ui = fonts::resolve(fonts::Role::Ui);
        mono = fonts::resolve(fonts::Role::Mono);
        done = true;
    }
    return r == fonts::Role::Mono ? mono : ui;
}

}  // namespace

void drawFontSettings(App& app) {
    using namespace fonts;
    ImGui::SeparatorText("Font");
    Settings s = app.fontSettings;
    bool changed = false;

    // A file picked in the dialog: validate it, keep a copy in the data folder, switch to it.
    {
        std::string picked;
        {
            std::lock_guard<std::mutex> lk(g_mu);
            if (g_pickedReady) {
                picked = g_picked;
                g_pickedReady = false;
            }
        }
        if (!picked.empty()) {
            std::string stored, err;
            if (importCustomFont(picked, customDir(), stored, err)) {
                s.custom = stored;
                s.family = Family::Custom;
                changed = true;
                g_message = "Using " + stored + ".";
                g_messageIsError = false;
            } else {
                g_message = err;
                g_messageIsError = true;
            }
        }
    }

    ImGui::SetNextItemWidth(ui::fitWidth(0.55f, 12.0f, 22.0f));
    if (ImGui::BeginCombo("Typeface", familyLabel(s.family))) {
        static const Family order[] = {Family::Retro, Family::System, Family::Mono, Family::Custom};
        for (Family f : order) {
            std::string label = familyLabel(f);
            bool usable = true;
            if (f == Family::System || f == Family::Mono) {
                const FontFile& face = systemFace(f == Family::Mono ? Role::Mono : Role::Ui);
                if (face.empty()) {
                    label += " (not found)";
                    usable = false;
                } else {
                    label += " - " + displayName(face);
                }
            } else if (f == Family::Custom && !s.custom.empty()) {
                label = "Custom file: " + s.custom;
            }
            ImGui::BeginDisabled(!usable);
            if (ImGui::Selectable(label.c_str(), s.family == f)) {
                if (f == Family::Custom && s.custom.empty()) {
                    openFontDialog(app);   // nothing chosen yet: ask for a file; the face changes when it loads
                } else {
                    s.family = f;
                    changed = true;
                }
            }
            ImGui::EndDisabled();
        }
        ImGui::EndCombo();
    }
    if (s.family == Family::Custom) {
        ImGui::SameLine();
        if (ImGui::Button("Choose file...")) openFontDialog(app);
        ui::hint("A copy of the font is kept in the app's data folder (fonts/), so moving or deleting the original is safe.");
    }

    int size = (int)(s.size + 0.5f);
    ImGui::SetNextItemWidth(ui::fitWidth(0.55f, 10.0f, 20.0f));
    if (ImGui::SliderInt("Size", &size, (int)kMinSize, (int)kMaxSize, "%d pt")) {
        s.size = (float)size;
        changed = true;
    }

    if (!warning().empty()) ui::coloredWrapped(ImVec4(1.0f, 0.55f, 0.35f, 1.0f), warning().c_str());
    if (!g_message.empty())
        ui::coloredWrapped(g_messageIsError ? ImVec4(1.0f, 0.5f, 0.5f, 1.0f) : ImVec4(0.55f, 0.85f, 0.55f, 1.0f),
                           g_message.c_str());

    // Preview: the same strings in the chosen face, then the monospace one.  UTF-8 escapes keep this source ASCII.
    ImGui::TextDisabled("Preview");
    ImGui::BeginChild("##fontpreview", ImVec2(0, ImGui::GetTextLineHeightWithSpacing() * 6.2f), ImGuiChildFlags_Borders);
    ImGui::TextUnformatted("The quick brown fox jumps over the lazy dog 0123456789");
    ImGui::TextUnformatted("Caf\xC3\xA9 - \xC3\x89lan Vital - Pok\xC3\xA9mon - Sch\xC3\xB6ne Gr\xC3\xBC\xC3\x9F" "e");
    ImGui::TextUnformatted("\xE2\x86\x90 \xE2\x86\x92 \xE2\x86\x91 \xE2\x86\x93   \xE2\x98\x85 favourite   \xE2\x9C\x93 done   loading\xE2\x80\xA6");
    ImGui::TextUnformatted("Atari Jaguar - Virtual Jaguar - Iron Soldier 2");
    pushMono();
    ImGui::TextUnformatted("00F00000  MOVE.W  D0,(A0)+   0123456789ABCDEF");
    popMono();
    ImGui::EndChild();
    ui::hint("Debug windows (memory, disassembly) always use the monospace face.");

    if (s.family != Family::Retro) {
        if (freetypeAvailable()) {
            ImGui::SetNextItemWidth(ui::fitWidth(0.55f, 12.0f, 22.0f));
            if (ImGui::BeginCombo("Hinting", hintingLabel(s.hinting))) {
                static const Hinting hs[] = {Hinting::None, Hinting::Light, Hinting::Full};
                for (Hinting h : hs)
                    if (ImGui::Selectable(hintingLabel(h), s.hinting == h)) {
                        s.hinting = h;
                        changed = true;
                    }
                ImGui::EndCombo();
            }
            if (ImGui::Checkbox("Smooth edges (anti-aliasing)", &s.antialias)) changed = true;
        } else {
            ui::hint("Text is drawn anti-aliased at your display's full pixel density (crisp on Retina and 150%% / 200%% "
                     "scaling). Hinting and anti-aliasing choices are available in builds that include FreeType.");
        }
    }
    if (ImGui::Button("Default font")) {
        const std::string keep = s.custom;
        s = Settings();
        s.custom = keep;
        changed = true;
    }

    if (changed) {
        s = sanitized(s);
        app.fontSettings = s;
        request(s);
    }
}

}  // namespace vj
