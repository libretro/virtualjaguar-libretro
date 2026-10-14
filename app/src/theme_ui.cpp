// Themes in the UI: Settings > Video > Appearance (the choice) and the theme editor window (custom themes:
// edit with a live preview, save, duplicate, delete, export, import).  The logic and the files are in
// theme_custom.cpp; this is only the widgets.
#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdio>
#include <mutex>
#include <string>
#include <vector>

#include "app.h"
#include "imgui.h"
#include "theme_custom.h"
#include "ui_util.h"

namespace vj {

namespace {

// File dialogs call back on an arbitrary thread; results wait here for the UI thread.
struct Mailbox {
    std::mutex m;
    std::string importPath;   // a theme file chosen to import
    std::string note;         // "Exported to ..." / the reason an export failed
    bool noteError = false;
};
Mailbox g_mail;

void SDLCALL importDone(void*, const char* const* files, int) {
    if (!files || !files[0]) return;
    std::lock_guard<std::mutex> lk(g_mail.m);
    g_mail.importPath = files[0];
}

// The export dialog gets the file's text at the moment of the click: what is written is what was on screen.
void SDLCALL exportDone(void* user, const char* const* files, int) {
    std::string* text = static_cast<std::string*>(user);
    if (files && files[0]) {
        std::string path = files[0];
        if (pathExt(path) != "ini") path += ".ini";
        const bool ok = writeFileAtomic(path, text->data(), text->size());
        std::lock_guard<std::mutex> lk(g_mail.m);
        g_mail.note = ok ? "Exported to " + path : "Cannot write " + path;
        g_mail.noteError = !ok;
    }
    delete text;
}

struct Editor {
    theme::Custom work, shown;   // what is being edited; what the live preview last showed
    std::string orig;            // the saved theme this started from ("" = a new one)
    std::string note;
    bool noteError = false;
    bool confirmDelete = false;
    bool inited = false;
    char name[64] = "";
};
Editor g_ed;

void startEditing(App& app) {
    theme::Store st = app.themeStore();
    if (const theme::Custom* c = theme::activeCustom()) {
        g_ed.work = *c;
        g_ed.orig = st.exists(c->name) ? c->name : "";
    } else {
        g_ed.work = theme::Custom::fromBase(theme::effective(), st.uniqueName("My theme"));
        g_ed.orig.clear();
    }
    g_ed.shown = g_ed.work;
    g_ed.note.clear();
    g_ed.confirmDelete = false;
    std::snprintf(g_ed.name, sizeof g_ed.name, "%s", g_ed.work.name.c_str());
    g_ed.inited = true;
    app.showThemeEditor = true;
}

void setNote(const std::string& s, bool err) {
    g_ed.note = s;
    g_ed.noteError = err;
}

// The theme `c` is the one being edited from here on (saved, imported, duplicated).
void adopt(App& app, const theme::Custom& c, bool saved) {
    g_ed.work = c;
    g_ed.shown = c;
    g_ed.orig = saved ? c.name : "";
    std::snprintf(g_ed.name, sizeof g_ed.name, "%s", c.name.c_str());
    g_ed.confirmDelete = false;
    if (saved) {
        app.setThemeChoice("custom:" + c.name);   // chosen and remembered
        app.themePreviewEnd(true);
    }
}

}  // namespace

void drawAppearanceSettings(App& app) {
    ImGui::SeparatorText("Appearance");
    static std::vector<theme::Custom> saved;
    static double listedAt = -100.0;
    if (app.now() - listedAt > 1.0) {   // themes can be added by hand or by another copy of the app
        saved = app.themeStore().list();
        listedAt = app.now();
    }
    const std::string cur = app.themePreviewing_ ? std::string() : theme::choiceKey();
    const theme::Mode mode = theme::state().mode();
    std::string label = mode == theme::Mode::Custom ? "Custom: " + theme::state().customName() : theme::modeLabel(mode);
    ImGui::SetNextItemWidth(ui::fitWidth(0.5f, 12.0f, 22.0f));
    if (ImGui::BeginCombo("Theme", label.c_str())) {
        for (theme::Mode m : {theme::Mode::System, theme::Mode::Dark, theme::Mode::Light})
            if (ImGui::Selectable(theme::modeLabel(m), cur == theme::modeKey(m))) app.setThemeChoice(theme::modeKey(m));
        if (!saved.empty()) ImGui::Separator();
        for (const theme::Custom& c : saved) {
            const std::string key = "custom:" + c.name;
            if (ImGui::Selectable(("Custom: " + c.name).c_str(), cur == key)) app.setThemeChoice(key);
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::Button("Customize...")) startEditing(app);
    if (mode == theme::Mode::System) ui::hint("Following the system: currently %s.", theme::isDark() ? "dark" : "light");
    if (app.paths.instance > 1) ui::hint("This window is a secondary copy: it can use the shared themes but not change them.");
}

void drawThemeEditor(App& app) {
    if (!g_ed.inited) startEditing(app);
    theme::Store store = app.themeStore();
    ui::nextWindow(34.0f, 46.0f, 24.0f, 24.0f);
    bool open = true;
    if (!ImGui::Begin("Theme editor", &open)) {
        ImGui::End();
        if (!open) { app.showThemeEditor = false; app.themePreviewEnd(false); }
        return;
    }
    ui::keepOnScreen();
    const bool ro = store.readOnly();

    ImGui::SetNextItemWidth(ui::fitWidth(0.6f, 12.0f, 24.0f));
    if (ImGui::InputText("Name", g_ed.name, sizeof g_ed.name)) g_ed.work.name = g_ed.name;
    if (ImGui::SmallButton("Reset to Dark")) g_ed.work = theme::Custom::fromBase(theme::Scheme::Dark, g_ed.work.name);
    ImGui::SameLine();
    if (ImGui::SmallButton("Reset to Light")) g_ed.work = theme::Custom::fromBase(theme::Scheme::Light, g_ed.work.name);
    ui::hint("Starts from %s. Colours, rounding and spacing show live; Save keeps the theme and chooses it.",
             g_ed.work.base == theme::Scheme::Dark ? "Dark" : "Light");
    ImGui::Separator();

    for (int i = 0; i < theme::KeyCount; i++) {
        theme::Color& c = g_ed.work.colors[i];
        float f[3] = {c.r / 255.0f, c.g / 255.0f, c.b / 255.0f};
        ImGui::PushID(i);
        ImGui::SetNextItemWidth(ui::fitWidth(0.55f, 10.0f, 18.0f));
        if (ImGui::ColorEdit3(theme::keyInfo((theme::Key)i).label, f,
                              ImGuiColorEditFlags_DisplayHex | ImGuiColorEditFlags_InputRGB | ImGuiColorEditFlags_NoAlpha)) {
            c = theme::Color((int)(f[0] * 255.0f + 0.5f), (int)(f[1] * 255.0f + 0.5f), (int)(f[2] * 255.0f + 0.5f));
        }
        ImGui::PopID();
    }
    ImGui::SetNextItemWidth(ui::fitWidth(0.5f, 10.0f, 20.0f));
    ImGui::SliderFloat("Rounding", &g_ed.work.rounding, theme::kMinRounding, theme::kMaxRounding, "%.0f px");
    ImGui::SetNextItemWidth(ui::fitWidth(0.5f, 10.0f, 20.0f));
    ImGui::SliderFloat("Spacing", &g_ed.work.spacing, theme::kMinSpacing, theme::kMaxSpacing, "%.2fx");

    ImGui::SeparatorText("Contrast");
    const std::vector<theme::Warning> warns = theme::contrastWarnings(g_ed.work);
    if (warns.empty()) {
        ImGui::TextWrapped("Every text pair is readable (at least 4.5:1).");
    } else {
        ImGui::TextWrapped("Some text will be hard to read:");
        for (const theme::Warning& w : warns) ImGui::BulletText("%s: %.1f:1 (needs %.1f:1)", w.what.c_str(), w.ratio, w.needed);
    }

    ImGui::SeparatorText("Theme");
    const std::string cleaned = theme::sanitizeName(g_ed.work.name);
    const bool exists = !cleaned.empty() && store.exists(cleaned);
    ImGui::BeginDisabled(ro || cleaned.empty());
    if (ImGui::Button(exists ? "Save (replace)" : "Save")) {
        theme::Custom c = g_ed.work;
        c.name = cleaned;
        std::string err;
        if (store.save(c, err)) {
            adopt(app, c, true);
            setNote("Saved as '" + c.name + "'.", false);
        } else {
            setNote(err, true);
        }
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(ro || cleaned.empty());
    if (ImGui::Button("Duplicate")) {
        theme::Custom c = g_ed.work;
        c.name = store.uniqueName(cleaned + " copy");
        std::string err;
        if (store.save(c, err)) {
            adopt(app, c, true);
            setNote("Saved a copy as '" + c.name + "'.", false);
        } else {
            setNote(err, true);
        }
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(ro || g_ed.orig.empty());
    if (!g_ed.confirmDelete) {
        if (ImGui::Button("Delete")) g_ed.confirmDelete = true;
    } else {
        if (ImGui::Button("Really delete?")) {
            std::string err;
            if (store.remove(g_ed.orig, err)) {
                const bool wasChosen = theme::state().mode() == theme::Mode::Custom && theme::state().customName() == g_ed.orig;
                setNote("Deleted '" + g_ed.orig + "'.", false);
                g_ed.orig.clear();
                if (wasChosen) app.setThemeChoice("system");
                g_ed.work = theme::Custom::fromBase(theme::effective(), g_ed.work.name);
            } else {
                setNote(err, true);
            }
            g_ed.confirmDelete = false;
        }
        ImGui::SameLine();
        if (ImGui::Button("Keep")) g_ed.confirmDelete = false;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Export...") && app.window()) {
        theme::Custom c = g_ed.work;
        c.name = cleaned.empty() ? "My theme" : cleaned;
        static SDL_DialogFileFilter flt[1] = {{"Virtual Jaguar themes", "ini"}};
        SDL_ShowSaveFileDialog(exportDone, new std::string(theme::serializeTheme(c)), app.window(), flt, 1,
                               (c.name + ".ini").c_str());
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(ro);
    if (ImGui::Button("Import...") && app.window()) {
        static SDL_DialogFileFilter flt[2] = {{"Virtual Jaguar themes", "ini"}, {"All files", "*"}};
        SDL_ShowOpenFileDialog(importDone, nullptr, app.window(), flt, 2, nullptr, false);
    }
    ImGui::EndDisabled();
    if (ro) ui::hint("This window is a secondary copy of the app: themes are read-only here (Export still works).");
    if (!g_ed.note.empty())
        ui::coloredWrapped(g_ed.noteError ? ui::vec4(ui::pal().error) : ui::vec4(ui::pal().success), g_ed.note.c_str());

    ImGui::End();

    // The live preview: show the theme being edited, whenever it differs from what is on screen.
    if (g_ed.work != g_ed.shown) {
        g_ed.shown = g_ed.work;
        app.themePreview(g_ed.work);
    }
    if (!open) {
        app.showThemeEditor = false;
        app.themePreviewEnd(false);   // unsaved edits go; a saved theme was chosen when it was saved
        g_ed.inited = false;
    }
}

void themeFrameHook(App& app) {
    std::string imp, note;
    bool err = false;
    {
        std::lock_guard<std::mutex> lk(g_mail.m);
        imp.swap(g_mail.importPath);
        note.swap(g_mail.note);
        err = g_mail.noteError;
    }
    if (!note.empty()) {
        setNote(note, err);
    }
    if (!imp.empty()) {
        theme::Custom c;
        std::string e;
        if (app.themeStore().importFile(imp, c, e)) {
            if (!g_ed.inited) startEditing(app);
            adopt(app, c, true);
            setNote("Imported '" + c.name + "'.", false);
        } else {
            setNote("Not imported: " + e, true);
        }
    }
}

}  // namespace vj
