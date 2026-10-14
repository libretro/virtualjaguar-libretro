#include "fonts_ui.h"

#include <cctype>
#include <cstdio>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <SDL3/SDL.h>

#include "app.h"
#include "fonts_imgui.h"
#include "fonts_system.h"
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

// ---- the font list ---------------------------------------------------------------------------------------------
//
// One flat list of rows (section headers and choices, all the same height so ImGuiListClipper can skip what is
// scrolled away).  Only the rows that are drawn ask fonts:: for their preview face, so a few dozen faces are loaded
// however many fonts the machine has (fonts_imgui.h).

struct Row {
    bool header = false;
    std::string id;          // choice id (fonts.h); "@auto" = the debugger's automatic face; headers: unused
    std::string label;       // what the row says (and the text its own font must be able to draw)
    fonts::FontFile file;    // where the face is; empty for the built-in pixel font
    bool retro = false;
    bool missing = false;    // the file is gone (a favorite whose font was uninstalled)
};

struct ListCache {
    std::string key;
    std::vector<Row> rows;
};

const char* const kDebugSample = "00F00000  MOVE.W  D0,(A0)+";

std::string lowered(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

bool matches(const std::string& lowFilter, const std::string& label) {
    return lowFilter.empty() || lowered(label).find(lowFilter) != std::string::npos;
}

// Row for a stored id (favorites / recents): the built-ins, a custom file, or an installed font.
bool rowForId(const std::string& id, const std::vector<fonts::SystemFont>* list, const std::string& customDir, Row& r) {
    using namespace fonts;
    r = Row();
    r.id = id;
    if (id == "@retro") {
        r.label = familyLabel(Family::Retro);
        r.retro = true;
    } else if (id == "@system" || id == "@mono") {
        const FontFile& f = systemFace(id == "@mono" ? Role::Mono : Role::Ui);
        r.label = id == "@mono" ? "Monospace (system)" : "System UI";
        r.file = f;
        if (f.empty()) r.missing = true;
        else r.label += " - " + displayName(f);
    } else if (id.compare(0, 8, "@custom:") == 0) {
        r.label = "Custom: " + id.substr(8);
        r.file.path = customPath(customDir, id.substr(8));
        r.missing = r.file.path.empty() || !fileExists(r.file.path);
    } else {
        FontFile ff;
        if (!parseFontId(id, ff)) return false;
        r.file = ff;
        const SystemFont* sf = list ? findSystemFont(*list, id) : nullptr;
        r.label = sf ? sf->name : displayName(ff);
        r.missing = list && !sf && !fileExists(ff.path);
    }
    return true;
}

void headerRow(std::vector<Row>& v, const std::string& label) {
    Row h;
    h.header = true;
    h.label = label;
    v.push_back(h);
}

// Rows for the UI typeface list: built-ins, favorites, recents, then every installed face.
void buildUiRows(ListCache& c, const fonts::Settings& s, const std::string& lowFilter,
                 const std::shared_ptr<const std::vector<fonts::SystemFont>>& list, const std::string& customDir) {
    c.rows.clear();
    const std::vector<fonts::SystemFont>* L = list.get();
    std::vector<Row> sec;
    auto flush = [&](const char* title) {
        if (sec.empty()) return;
        headerRow(c.rows, title);
        c.rows.insert(c.rows.end(), sec.begin(), sec.end());
        sec.clear();
    };
    auto add = [&](const std::string& id) {
        Row r;
        if (rowForId(id, L, customDir, r) && matches(lowFilter, r.label)) sec.push_back(r);
    };
    add("@retro");
    add("@system");
    add("@mono");
    if (!s.custom.empty()) add("@custom:" + s.custom);
    flush("Built-in");
    for (const std::string& id : s.favorites) add(id);
    flush("Favorites");
    for (const std::string& id : s.recents) add(id);
    flush("Recent");
    if (L) {
        for (const fonts::SystemFont& f : *L) {
            Row r;
            r.id = f.id();
            r.label = f.name;
            r.file.path = f.path;
            r.file.index = f.index;
            if (matches(lowFilter, r.label)) sec.push_back(std::move(r));
        }
        char title[64];
        std::snprintf(title, sizeof title, "Installed fonts (%zu)", sec.size());
        flush(title);
    } else {
        headerRow(c.rows, "Scanning installed fonts...");
    }
}

// Rows for the debugger font list: automatic, the pixel font, favorites that are monospace, every monospace face.
void buildDebugRows(ListCache& c, const fonts::Settings& s, const std::string& lowFilter,
                    const std::shared_ptr<const std::vector<fonts::SystemFont>>& list, const std::string& customDir) {
    c.rows.clear();
    const std::vector<fonts::SystemFont>* L = list.get();
    {
        Row r;
        r.id = "@auto";
        const fonts::FontFile& mf = systemFace(fonts::Role::Mono);
        r.file = mf;
        r.label = mf.empty() ? "Automatic (built-in pixel font)" : "Automatic - " + fonts::displayName(mf);
        r.retro = mf.empty();
        if (matches(lowFilter, r.label)) c.rows.push_back(r);
        Row p;
        p.id = "@retro";
        p.label = "Pixel font (built-in)";
        p.retro = true;
        if (matches(lowFilter, p.label)) c.rows.push_back(p);
    }
    std::vector<Row> sec;
    if (L) {
        for (const std::string& id : s.favorites) {
            const fonts::SystemFont* sf = fonts::findSystemFont(*L, id);
            Row r;
            if (sf && sf->mono && rowForId(id, L, customDir, r) && matches(lowFilter, r.label)) sec.push_back(r);
        }
    }
    if (!sec.empty()) {
        headerRow(c.rows, "Favorites");
        c.rows.insert(c.rows.end(), sec.begin(), sec.end());
        sec.clear();
    }
    if (L) {
        for (const fonts::SystemFont& f : *L) {
            if (!f.mono) continue;
            Row r;
            r.id = f.id();
            r.label = f.name;
            r.file.path = f.path;
            r.file.index = f.index;
            if (matches(lowFilter, r.label)) sec.push_back(std::move(r));
        }
        char title[64];
        std::snprintf(title, sizeof title, "Monospace fonts (%zu)", sec.size());
        headerRow(c.rows, title);
        c.rows.insert(c.rows.end(), sec.begin(), sec.end());
    } else {
        headerRow(c.rows, "Scanning installed fonts...");
    }
}

std::string listKey(const fonts::Settings& s, const std::string& lowFilter, size_t listSize, bool debug) {
    std::string k = lowFilter;
    k += debug ? "\x01""D" : "\x01""U";
    k += std::to_string(listSize);
    k += '\x01';
    for (const std::string& f : s.favorites) k += f + '\x02';
    if (!debug) {
        k += '\x01';
        for (const std::string& f : s.recents) k += f + '\x02';
        k += '\x01' + s.custom;
    }
    return k;
}

struct Pick {
    std::string id;          // a row was chosen
    std::string starId;      // a star was clicked
};

// Draws the rows (clipped) into the current child window.  Debug rows add a dim sample after the name.
void drawRows(const std::vector<Row>& rows, const std::string& selectedId, bool debug, Pick& pick,
              const fonts::Settings& s) {
    ImGuiStyle& st = ImGui::GetStyle();
    const float rowH = ImGui::GetFontSize() * 1.55f;
    const float starW = ImGui::GetFontSize() * 1.7f;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImGuiListClipper clip;
    clip.Begin((int)rows.size(), rowH + st.ItemSpacing.y);
    while (clip.Step()) {
        for (int i = clip.DisplayStart; i < clip.DisplayEnd; i++) {
            const Row& r = rows[(size_t)i];
            ImGui::PushID(i);
            const ImVec2 p = ImGui::GetCursorScreenPos();
            const float w = ImGui::GetContentRegionAvail().x;
            const ImVec2 rmax(p.x + w, p.y + rowH);
            if (r.header) {
                ImGui::Dummy(ImVec2(w, rowH));
                const ImVec2 ts = ImGui::CalcTextSize(r.label.c_str());
                dl->AddText(ImVec2(p.x + st.FramePadding.x, p.y + (rowH - ts.y) * 0.5f), ImGui::GetColorU32(ImGuiCol_TextDisabled),
                            r.label.c_str());
                ImGui::PopID();
                continue;
            }
            fonts::Preview pv;
            if (r.retro) pv = fonts::previewRetro();
            else if (r.file.empty()) { pv.state = fonts::PreviewState::Failed; pv.why = "not found"; pv.usable = false; }
            else pv = fonts::preview(r.id, r.file, debug ? r.label + kDebugSample : r.label);
            const bool disabled = r.missing || (pv.state == fonts::PreviewState::Failed && !pv.usable);
            const bool sel = r.id == selectedId;

            ImGui::BeginDisabled(disabled);
            const bool clicked = ImGui::Selectable("##row", sel, 0, ImVec2(w, rowH));
            ImGui::EndDisabled();
            const bool rowHover = ImGui::IsMouseHoveringRect(p, rmax) && ImGui::IsWindowHovered();
            const ImVec2 mouse = ImGui::GetIO().MousePos;
            const bool overStar = rowHover && mouse.x < p.x + starW;
            const bool canStar = !r.id.empty() && r.id[0] != '@';   // installed fonts; the built-ins are always listed
            if (clicked && !overStar) pick.id = r.id;
            if (overStar && canStar && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) pick.starId = r.id;

            // the star: the UI font, so it draws in every typeface
            if (canStar) {
                const bool fav = fonts::isFavorite(s, r.id);
                const ImU32 col = fav ? IM_COL32(255, 205, 60, 255)
                                      : ImGui::GetColorU32(ImGuiCol_Text, rowHover ? (overStar ? 0.9f : 0.5f) : 0.22f);
                const char* glyph = fav ? "\xE2\x98\x85" : "\xE2\x98\x86";
                const ImVec2 ts = ImGui::CalcTextSize(glyph);
                dl->AddText(ImVec2(p.x + (starW - ts.x) * 0.5f, p.y + (rowH - ts.y) * 0.5f), col, glyph);
            }

            // the label in its own face, then (debugger list) a sample line in the same face
            const bool ownFont = pv.state == fonts::PreviewState::Ready && pv.font;
            const ImU32 textCol = ImGui::GetColorU32(disabled ? ImGuiCol_TextDisabled : ImGuiCol_Text);
            dl->PushClipRect(ImVec2(p.x + starW, p.y), rmax, true);
            if (ownFont) ImGui::PushFont(pv.font, st.FontSizeBase);
            const ImVec2 lt = ImGui::CalcTextSize(r.label.c_str());
            const float ty = p.y + (rowH - lt.y) * 0.5f;
            dl->AddText(ImVec2(p.x + starW, ty), textCol, r.label.c_str());
            if (debug && ownFont) {
                dl->AddText(ImVec2(p.x + starW + lt.x + ImGui::GetFontSize() * 1.2f, ty),
                            ImGui::GetColorU32(ImGuiCol_Text, 0.6f), kDebugSample);
            }
            if (ownFont) ImGui::PopFont();
            dl->PopClipRect();
            // a face that cannot draw itself: the UI font, and say so
            if (pv.state == fonts::PreviewState::Failed && !r.missing) {
                const std::string note = "(" + pv.why + ")";
                const ImVec2 ns = ImGui::CalcTextSize(note.c_str());
                dl->AddText(ImVec2(rmax.x - ns.x - st.FramePadding.x, p.y + (rowH - ns.y) * 0.5f),
                            ImGui::GetColorU32(ImGuiCol_TextDisabled), note.c_str());
            } else if (r.missing) {
                const ImVec2 ns = ImGui::CalcTextSize("(not found)");
                dl->AddText(ImVec2(rmax.x - ns.x - st.FramePadding.x, p.y + (rowH - ns.y) * 0.5f),
                            ImGui::GetColorU32(ImGuiCol_TextDisabled), "(not found)");
            }
            if (rowHover && !overStar && !r.file.empty()) ImGui::SetTooltip("%s", r.file.path.c_str());
            else if (overStar && canStar) ImGui::SetTooltip(fonts::isFavorite(s, r.id) ? "Remove from favorites" : "Add to favorites");
            ImGui::PopID();
        }
    }
    clip.End();
}

char g_filterUi[96] = "";
char g_filterDebug[96] = "";
ListCache g_uiRows, g_debugRows;

// A search box + the clipped list in a bordered child, `visibleRows` tall.
void drawList(const char* id, char* filterBuf, size_t filterSize, ListCache& cache, bool debug, const fonts::Settings& s,
              const std::string& selectedId, float visibleRows, const std::string& customDir, Pick& pick) {
    ImGui::SetNextItemWidth(ui::fitWidth(0.55f, 12.0f, 22.0f));
    ImGui::InputTextWithHint((std::string("##filter") + id).c_str(), "Search fonts", filterBuf, filterSize);
    fonts::systemFontsStart();
    const std::shared_ptr<const std::vector<fonts::SystemFont>> list = fonts::systemFonts();
    const std::string low = lowered(filterBuf);
    const std::string key = listKey(s, low, list ? list->size() : 0, debug);
    if (key != cache.key) {
        cache.key = key;
        if (debug) buildDebugRows(cache, s, low, list, customDir);
        else buildUiRows(cache, s, low, list, customDir);
    }
    const float rowH = ImGui::GetFontSize() * 1.55f + ImGui::GetStyle().ItemSpacing.y;
    ImGui::BeginChild(id, ImVec2(0, rowH * visibleRows + ImGui::GetStyle().WindowPadding.y * 2.0f), ImGuiChildFlags_Borders);
    drawRows(cache.rows, selectedId, debug, pick, s);
    if (cache.rows.empty() || (cache.rows.size() == 1 && cache.rows[0].header)) ImGui::TextDisabled("No fonts match.");
    ImGui::EndChild();
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
                noteRecent(s, choiceId(s));
                changed = true;
                g_message = "Using " + stored + ".";
                g_messageIsError = false;
            } else {
                g_message = err;
                g_messageIsError = true;
            }
        }
    }

    // A typeface that was chosen but could not be loaded (uninstalled, damaged): stop offering it as a favorite.
    if (s.family != Family::Retro && active().family == Family::Retro && !warning().empty() &&
        (s.family == Family::Installed || s.family == Family::Custom)) {
        forgetChoice(s, choiceId(s));
        changed = true;
    }

    // ---- the typeface list -------------------------------------------------------------------------------------
    std::string current;
    {
        Row r;
        std::shared_ptr<const std::vector<SystemFont>> list = systemFonts();
        if (rowForId(choiceId(s), list.get(), customDir(), r)) current = r.label;
    }
    ImGui::Text("Typeface: %s", current.c_str());
    ImGui::SameLine();
    if (ImGui::SmallButton("Choose file...")) openFontDialog(app);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Use a .ttf / .otf / .ttc file. A copy is kept in the app's data folder (fonts/),\n"
                          "so moving or deleting the original is safe.");
    {
        Pick pick;
        drawList("##fontlist", g_filterUi, sizeof g_filterUi, g_uiRows, false, s, choiceId(s), 9.0f, customDir(), pick);
        if (!pick.starId.empty()) {
            setFavorite(s, pick.starId, !isFavorite(s, pick.starId));
            changed = true;
        }
        if (!pick.id.empty() && applyChoice(s, pick.id)) {
            noteRecent(s, pick.id);
            changed = true;
        }
    }
    ui::hint("Click a name to use it; the star keeps a font at the top of the list. Every name is drawn in its own "
             "typeface. Fonts installed on this computer are listed after the built-in ones.");

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
    ui::hint("Debug windows (memory, disassembly) always use a monospace face; choose which one below.");

    // ---- the debugger font -----------------------------------------------------------------------------------
    ImGui::SetNextItemOpen(true, ImGuiCond_Once);
    if (ImGui::TreeNode("Debugger font (hex dumps, disassembly)")) {
        Pick pick;
        const std::string sel = s.debugFont.empty() ? "@auto" : s.debugFont;
        drawList("##debugfontlist", g_filterDebug, sizeof g_filterDebug, g_debugRows, true, s, sel, 6.0f, customDir(), pick);
        if (!pick.starId.empty()) {
            setFavorite(s, pick.starId, !isFavorite(s, pick.starId));
            changed = true;
        }
        if (!pick.id.empty()) {
            s.debugFont = pick.id == "@auto" ? std::string() : pick.id;
            changed = true;
        }
        ImGui::TreePop();
    }

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
        Settings d;   // typeface, size and rasteriser go back; what the user collected stays
        d.custom = s.custom;
        d.installed = s.installed;
        d.debugFont = s.debugFont;
        d.favorites = s.favorites;
        d.recents = s.recents;
        s = d;
        changed = true;
    }

    if (changed) {
        s = sanitized(s);
        app.fontSettings = s;
        request(s);
    }
}

}  // namespace vj
