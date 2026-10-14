// The controls overlay window.  Informational only: NoInputs | NoNav | NoFocusOnAppearing, so it
// never takes the pointer or the keyboard from the game and never counts as an open menu.
// Sizes are fractions of the viewport and the text is wrapped / scaled down to fit, so it stays
// readable in a small window and on a HiDPI display (the font is already scaled by the UI).
#include "app.h"
#include "devices_ui.h"
#include "ui_util.h"
#include "imgui.h"

#include <algorithm>

namespace vj {

Sheet currentControlsSheet(App& app) {
    InputManager& in = app.input;
    in.updateRoutes();
    const int pad = in.routes().pad[0];
    std::vector<InputManager::PadInfo> pads = in.pads();
    const bool usePad = pad >= 0 && (size_t)pad < pads.size() && in.controls.lastInputWasPad;
    const Profile& prof = usePad ? in.padProfile((size_t)pad) : in.keyboardProfile();
    return buildSheet(prof, in.portKind(0), usePad ? SheetSource::Gamepad : SheetSource::Keyboard,
                      usePad ? pads[(size_t)pad].name : std::string());
}

std::string controlsOverlayText(App& app) { return currentControlsSheet(app).text(); }

void drawControlsOverlay(App& app, ImVec2 areaPos, ImVec2 areaSize) {
    ControlsOverlayState& st = app.input.controls;
    if (!st.show || !app.host.isLoaded()) return;
    Sheet sheet = currentControlsSheet(app);

    ImGuiIO& io = ImGui::GetIO();
    const float f = ImGui::GetFontSize();
    // Width: up to 46% of the area (one column) or 80% when it splits into two; never wider than the area.
    const bool wide = areaSize.x >= f * 52.0f;
    const float maxW = std::max(f * 14.0f, std::min(areaSize.x * 0.96f, wide ? areaSize.x * 0.62f : areaSize.x * 0.94f));
    const int columns = wide && sheet.lines.size() > 16 ? 2 : 1;

    // Shrink the text until the sheet fits the area's height (floor 60%); when even that is not enough
    // (a very small window) the twelve keypad lines collapse into one hint.
    auto fitScale = [&](size_t lines) {
        float rows = (float)lines / (float)columns + 3.0f;
        float need = rows * (f + ImGui::GetStyle().ItemSpacing.y);
        return areaSize.y * 0.92f / std::max(need, 1.0f);
    };
    if (fitScale(sheet.lines.size()) < 0.6f) {
        std::vector<SheetLine> kept;
        bool inKeypad = false;
        for (const SheetLine& l : sheet.lines) {
            if (l.heading) {
                inKeypad = l.label.rfind("Keypad", 0) == 0;
                if (inKeypad) {
                    SheetLine hint;
                    hint.label = "Keypad";
                    hint.keys = "number row / numpad, the full list is in Settings > Input";
                    kept.push_back(hint);
                    continue;
                }
            }
            if (!inKeypad) kept.push_back(l);
        }
        sheet.lines = kept;
    }
    const float scale = std::max(0.5f, std::min(1.0f, fitScale(sheet.lines.size())));

    ImGui::SetNextWindowPos(ImVec2(areaPos.x + areaSize.x * 0.5f, areaPos.y + f * 0.6f), ImGuiCond_Always, ImVec2(0.5f, 0.0f));
    ImGui::SetNextWindowBgAlpha(ui::overlay().windowAlpha);
    // An explicit width (height auto-fits): wrapped text and auto-resize would otherwise chase each other.
    ImGui::SetNextWindowSize(ImVec2(columns == 2 ? maxW : std::min(maxW, f * 34.0f * scale + f), 0), ImGuiCond_Always);
    ImGuiWindowFlags wf = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav |
                          ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoSavedSettings |
                          ImGuiWindowFlags_NoBringToFrontOnFocus;
    (void)io;
    // The sheet sits on the game picture: the overlay palette (dark-translucent, legible on a white or a black
    // frame) in both themes, so the window's colours are pinned here rather than taken from the theme.
    const theme::Overlay& ov = ui::overlay();
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ui::vec4(ov.windowBg));
    ImGui::PushStyleColor(ImGuiCol_Text, ui::vec4(ov.text));
    ImGui::PushStyleColor(ImGuiCol_TextDisabled, ui::vec4(ov.textMuted));
    ImGui::PushStyleColor(ImGuiCol_Separator, ui::vec4(ov.separator));
    struct PopColors { ~PopColors() { ImGui::PopStyleColor(4); } } popColors;   // also on the early return
    if (!ImGui::Begin("##controls_overlay", nullptr, wf)) {
        ImGui::End();
        return;
    }
    auto drawLine = [](const SheetLine& l) {
        if (l.heading) {
            ImGui::PushStyleColor(ImGuiCol_Text, ui::vec4(ui::overlay().heading));
            ImGui::TextWrapped("%s", l.label.c_str());
            ImGui::PopStyleColor();
        } else {
            ImGui::TextWrapped("%s: %s", l.label.c_str(), l.keys.c_str());
        }
    };
    ImGui::PushFont(nullptr, f * scale);
    ImGui::PushStyleColor(ImGuiCol_Text, ui::vec4(ui::overlay().title));
    ImGui::TextWrapped("%s", sheet.title.c_str());
    ImGui::PopStyleColor();
    ImGui::Separator();
    if (columns == 2 && ImGui::BeginTable("##cs", 2, ImGuiTableFlags_SizingStretchSame)) {
        // Never split a section: break at the heading that balances the two columns best.
        size_t half = sheet.lines.size(), bestSpread = sheet.lines.size();
        for (size_t i = 1; i < sheet.lines.size(); i++) {
            if (!sheet.lines[i].heading) continue;
            size_t spread = i > sheet.lines.size() - i ? i - (sheet.lines.size() - i) : (sheet.lines.size() - i) - i;
            if (spread < bestSpread) {
                bestSpread = spread;
                half = i;
            }
        }
        ImGui::TableNextRow();
        for (int col = 0; col < 2; col++) {
            ImGui::TableSetColumnIndex(col);
            size_t a = col == 0 ? 0 : half, b = col == 0 ? half : sheet.lines.size();
            for (size_t i = a; i < b; i++) {
                const SheetLine& l = sheet.lines[i];
                drawLine(l);
            }
        }
        ImGui::EndTable();
    } else {
        for (const SheetLine& l : sheet.lines) drawLine(l);
    }
    ImGui::Separator();
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("Hide: the Controls overlay key. Rebind: Settings > Input.");
    ImGui::PopStyleColor();
    ImGui::PopFont();
    ImGui::End();
}

}  // namespace vj
