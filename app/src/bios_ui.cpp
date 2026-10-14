// The BIOS & boot panel (#838, #833): which boot ROM and CD BIOS the core runs, how CD games and cartridges
// boot, importing a file, and booting a BIOS on its own.  Drawn in Settings > BIOS and above the library's
// "BIOS & system" tiles.  The decisions live in bios.h; this file is layout.
#include "app.h"
#include "bios.h"

#include "imgui.h"
#include "paths.h"
#include "ui_util.h"

#include <algorithm>
#include <string>
#include <vector>

namespace vj {

namespace {

std::string baseOf(const std::string& path) {
    size_t sl = path.find_last_of("/\\");
    return sl == std::string::npos ? path : path.substr(sl + 1);
}

// What the combo shows for the current choice.
std::string choiceLabel(const std::string& chosen, const std::string& chosenId, const std::vector<bios::Pick>& picks,
                        const std::string& autoText) {
    if (chosen.empty()) return "Auto: " + autoText;
    for (const bios::Pick& p : picks)
        if (p.id == chosenId) return p.label;
    return baseOf(chosen) + (fileExists(chosen) ? "" : "  (file is gone: Auto is used)");
}

// A strict-list core option as a combo that works before any game has been loaded.
void optionCombo(App& app, const char* label, const char* key, const char* const* values, const char* const* labels, int n,
                 const char* fallback, const char* tip) {
    CoreHost& host = app.host;
    const std::string cur = host.optionValueFor(key, fallback);
    int sel = -1;
    for (int i = 0; i < n; i++)
        if (cur == values[i]) sel = i;
    const bool pinned = host.optionOverridden(key);
    ImGui::BeginDisabled(pinned);
    ImGui::SetNextItemWidth(ui::fitWidth(0.5f, 14.0f, 26.0f));
    if (ImGui::BeginCombo(label, sel >= 0 ? labels[sel] : cur.c_str())) {
        for (int i = 0; i < n; i++)
            if (ImGui::Selectable(labels[i], i == sel)) host.setOptionValue(key, values[i]);
        ImGui::EndCombo();
    }
    ImGui::EndDisabled();
    if (tip && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort | ImGuiHoveredFlags_AllowWhenDisabled)) {
        ui::beginWrappedTooltip();
        ImGui::TextUnformatted(pinned ? "Set for this run on the command line (--option); it cannot be changed here." : tip);
        ui::endWrappedTooltip();
    }
}

void drawRole(App& app, bios::Role role) {
    CoreHost& host = app.host;
    const bool cart = role == bios::CartBoot;
    ImGui::PushID((int)role);
    ImGui::SeparatorText(cart ? "Console boot ROM (cartridges)" : "Jaguar CD BIOS");

    // ---- which file ----
    const std::vector<bios::Pick> picks = bios::picksFor(app.library.entries(), role);
    const std::string chosen = app.biosChoice.of(role);
    const std::string chosenId = chosen.empty() ? std::string() : bios::fileIdentity(chosen);   // once per frame, not per row
    std::string autoText;
    if (cart) {
        const std::string t = host.optionValueFor("virtualjaguar_bios_type", "k");
        autoText = t == "m" ? "built-in Model M" : t == "custom" ? "system folder file, else Series K" : "built-in Series K";
    } else {
        autoText = host.optionValueFor("virtualjaguar_cd_bios_type", "retail") == "dev"
                       ? "system folder, else built-in developer"
                       : "system folder, else built-in retail";
    }
    ImGui::SetNextItemWidth(ui::fitWidth(0.7f, 18.0f, 40.0f));
    if (ImGui::BeginCombo(cart ? "Boot ROM" : "CD BIOS", choiceLabel(chosen, chosenId, picks, autoText).c_str())) {
        // Every row has its own ID scope (#932): the same dump in two folders must not share one.
        ImGui::PushID("auto");
        if (ImGui::Selectable(("Auto: " + autoText).c_str(), chosen.empty())) app.setBiosChoice(role, "");
        ImGui::PopID();
        for (const bios::Pick& p : picks) {
            ImGui::PushID(p.path.c_str());
            if (ImGui::Selectable(p.label.c_str(), p.id == chosenId)) app.setBiosChoice(role, p.path);
            ImGui::PopID();
        }
        if (picks.empty()) ImGui::TextDisabled("No usable files found yet: Import BIOS file... or add a folder.");
        ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
        ui::beginWrappedTooltip();
        ImGui::TextWrapped("%s",
                           cart ? "Auto lets the core decide (the built-in Series K or Model M image, or a jagboot file in the "
                                  "system folder with Auto picks = Custom). Picking a file runs exactly that 128 KiB boot ROM, "
                                  "whatever it is called and wherever it lives."
                                : "Auto lets the core decide (a CD BIOS file in the system folder, else the built-in image). "
                                  "Picking a file runs exactly that 256 KiB CD BIOS, whatever it is called and wherever it lives.");
        ui::endWrappedTooltip();
    }

    // ---- what Auto picks ----
    if (cart) {
        static const char* const v[] = {"k", "m", "custom"};
        static const char* const l[] = {"Series K (built in)", "Model M (built in)", "Custom: a jagboot file in the system folder"};
        optionCombo(app, "Auto picks", "virtualjaguar_bios_type", v, l, 3, "k",
                    "Which boot ROM Auto means. Applied when the game starts.");
    } else {
        static const char* const v[] = {"retail", "dev"};
        static const char* const l[] = {"Retail", "Developer (less strict disc checks)"};
        optionCombo(app, "Auto picks", "virtualjaguar_cd_bios_type", v, l, 2, "retail",
                    "Which built-in CD BIOS Auto means. Applied when the game starts.");
    }

    // ---- when it is used ----
    if (cart) {
        static const char* const v[] = {"disabled", "enabled"};
        static const char* const l[] = {"HLE (no boot ROM; fast)", "Real boot ROM"};
        optionCombo(app, "Cartridges boot through", "virtualjaguar_bios", v, l, 2, "disabled",
                    "HLE skips the boot animation and starts the game directly; Real runs the boot ROM above first. "
                    "GPU-only demos and 'Boot' below always run the boot ROM.");
    } else {
        static const char* const v[] = {"hle", "bios", "auto"};
        static const char* const l[] = {"HLE (recommended)", "Real BIOS", "Auto (Real BIOS when one is available)"};
        optionCombo(app, "CD games boot through", "virtualjaguar_cd_boot_mode", v, l, 3, "hle",
                    "HLE emulates the CD BIOS services and never runs a BIOS; Real BIOS runs the CD BIOS above first (its logo, "
                    "disc check and menus). Audio CDs and 'Boot' below always run the CD BIOS. Applied when the game starts.");
    }

    // ---- what is in use ----
    const CoreHost::StagedBios& sb = host.stagedBios(role);
    if (sb.seen && host.isLoaded())
        ui::hint("Running now: %s", bios::describeStaged(sb.source, sb.where, sb.crc).c_str());
    else
        ui::hint("In use at the next start: %s", chosen.empty() ? "Auto" : choiceLabel(chosen, chosenId, picks, autoText).c_str());
    if (sb.rejected)
        ui::coloredWrapped(ui::vec4(ui::pal().warning),
                           (std::string("The core refused ") + baseOf(sb.rejectedPath) + " (missing, the wrong size or not a " +
                            (cart ? "boot ROM" : "CD BIOS") + "), so it used its own choice.")
                               .c_str());
    if (!cart && host.optionValueFor("virtualjaguar_cd_boot_mode", "hle") == "hle" && !chosen.empty())
        ui::hint("CD games currently boot through HLE, so this file is used only for audio CDs and when you Boot the CD BIOS.");
    if (cart && host.optionValueFor("virtualjaguar_bios", "disabled") == "disabled" && !chosen.empty())
        ui::hint("Cartridges currently boot through HLE, so this file is used only by GPU-only demos and when you Boot the boot ROM.");

    // ---- boot it ----
    if (ImGui::Button(cart ? "Boot this ROM (empty slot)" : "Boot the CD BIOS (no disc)")) app.bootBios(role);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
        ui::beginWrappedTooltip();
        ImGui::TextWrapped("%s", cart ? "Starts the console through the boot ROM above with nothing in the cartridge slot: the Jaguar "
                                        "logo, then it waits for a cartridge."
                                      : "Starts the Jaguar CD player through the CD BIOS above with no disc: its logo and the screen "
                                        "that asks for a disc, with the Memory Track plugged in.");
        ui::endWrappedTooltip();
    }
    ImGui::PopID();
}

}  // namespace

void drawBiosPanel(App& app) {
    CoreHost& host = app.host;
    ui::hint("The core has every BIOS it needs built in, so nothing here is required. Choose a file to run a different "
             "revision, or boot a BIOS on its own.");
    drawRole(app, bios::CartBoot);
    drawRole(app, bios::Cd);
    ImGui::SeparatorText("Files");
    if (ImGui::Button("Import BIOS file...")) app.showImportBiosDialog();
    ImGui::SameLine();
    ui::hint("Or drop a BIOS file on the window. It is copied to %sbios/ and chosen.", app.paths.system.c_str());
    if (host.isLoaded()) {
        if (ImGui::Button("Restart game to apply")) app.reloadContent();
        ImGui::SameLine();
        ui::hint("BIOS choices apply when the game starts.");
    }
}

}  // namespace vj
