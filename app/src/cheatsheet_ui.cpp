// The controls overlay window.  Informational only: NoInputs | NoNav | NoFocusOnAppearing, so it
// never takes the pointer or the keyboard from the game and never counts as an open menu.
// Sizes are fractions of the viewport and the text is wrapped / scaled down to fit, so it stays
// readable in a small window and on a HiDPI display (the font is already scaled by the UI).
#include "app.h"
#include "devices_ui.h"
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

    // Shrink the text until the sheet fits the area's height (floor 60%).
    float rows = (float)sheet.lines.size() / (float)columns + 3.0f;
    float need = rows * (f + ImGui::GetStyle().ItemSpacing.y);
    float scale = std::max(0.6f, std::min(1.0f, areaSize.y * 0.92f / std::max(need, 1.0f)));

    ImGui::SetNextWindowPos(ImVec2(areaPos.x + areaSize.x * 0.5f, areaPos.y + f * 0.6f), ImGuiCond_Always, ImVec2(0.5f, 0.0f));
    ImGui::SetNextWindowBgAlpha(0.74f);
    ImGui::SetNextWindowSizeConstraints(ImVec2(f * 12.0f, 0), ImVec2(maxW, std::max(f * 6.0f, areaSize.y * 0.96f)));
    ImGuiWindowFlags wf = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav |
                          ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoSavedSettings |
                          ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoBringToFrontOnFocus;
    (void)io;
    if (!ImGui::Begin("##controls_overlay", nullptr, wf)) {
        ImGui::End();
        return;
    }
    ImGui::PushFont(nullptr, f * scale);
    ImGui::PushTextWrapPos(maxW - f);
    ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.45f, 1.0f), "%s", sheet.title.c_str());
    ImGui::Separator();
    if (columns == 2 && ImGui::BeginTable("##cs", 2, ImGuiTableFlags_SizingStretchSame)) {
        size_t half = (sheet.lines.size() + 1) / 2;
        // Never split a section in half: move the break to the next heading.
        while (half < sheet.lines.size() && !sheet.lines[half].heading) half++;
        ImGui::TableNextRow();
        for (int col = 0; col < 2; col++) {
            ImGui::TableSetColumnIndex(col);
            size_t a = col == 0 ? 0 : half, b = col == 0 ? half : sheet.lines.size();
            for (size_t i = a; i < b; i++) {
                const SheetLine& l = sheet.lines[i];
                if (l.heading) ImGui::TextColored(ImVec4(0.6f, 0.85f, 1.0f, 1.0f), "%s", l.label.c_str());
                else ImGui::TextWrapped("%s: %s", l.label.c_str(), l.keys.c_str());
            }
        }
        ImGui::EndTable();
    } else {
        for (const SheetLine& l : sheet.lines) {
            if (l.heading) ImGui::TextColored(ImVec4(0.6f, 0.85f, 1.0f, 1.0f), "%s", l.label.c_str());
            else ImGui::TextWrapped("%s: %s", l.label.c_str(), l.keys.c_str());
        }
    }
    ImGui::Separator();
    ImGui::TextDisabled("Hide: the Controls overlay key. Rebind: Settings > Input.");
    ImGui::PopTextWrapPos();
    ImGui::PopFont();
    ImGui::End();
}

}  // namespace vj
