// ImGui widgets for the typed option values (typed_options.h): a combo holding the core's own
// presets plus a "Custom..." entry, and an inline box that appears for a custom value.
//
// Used by the Settings option rows (ui.cpp) and the Netplay window's port box (netplay_ui.cpp).
// A value is reported only when it validates; an invalid entry shows why, in red, and changes
// nothing.  The box applies on Enter or when it loses focus, never per keystroke: typing 50123
// must not retarget the link at 5, 50, 501 and 5012 on the way.
#pragma once

#include <cstdio>
#include <string>
#include <vector>

#include "core_host.h"
#include "imgui.h"
#include "typed_options.h"
#include "ui_util.h"

namespace vj {
namespace ui {

struct TypedState {
    bool custom = false;     // the "Custom..." box is open
    bool focus = false;      // give the box keyboard focus on its next draw
    bool autoOpened = false; // the box opened because the value was no preset (not because the user asked)
    char buf[160] = "";
};

inline const ImVec4& typedRed() {
    static const ImVec4 c(1.0f, 0.5f, 0.5f, 1.0f);
    return c;
}

// The combo.  `current` is the value in force.  Returns true with `picked` set when the user chose a
// preset other than `current`.  Choosing "Custom..." (or a current value that is no preset) opens the
// box, which typedInput() draws.
inline bool typedCombo(const char* label, float width, const TypedOption& t, const std::vector<OptionValue>& presets,
                       const std::string& current, TypedState& st, std::string& picked) {
    bool isPreset = false;
    const char* curLabel = nullptr;
    for (const OptionValue& v : presets)
        if (v.value == current) {
            isPreset = true;
            curLabel = v.label.empty() ? v.value.c_str() : v.label.c_str();
            break;
        }
    // A box that opened by itself (the value was no preset) closes once the value is a preset again --
    // a session override ended, or a reset.  A box the user opened stays until they pick a preset.
    if (isPreset && st.custom && st.autoOpened) {
        st.custom = false;
        st.autoOpened = false;
    }
    // A saved custom value comes back as an open box showing it.
    if (!isPreset && !st.custom) {
        st.custom = true;
        st.autoOpened = true;
        std::snprintf(st.buf, sizeof st.buf, "%s", current.c_str());
    }
    std::string shown;
    if (st.custom) shown = isPreset ? "Custom..." : "Custom: " + current;
    else shown = curLabel ? curLabel : current;

    bool changed = false;
    ImGui::SetNextItemWidth(width);
    if (ImGui::BeginCombo(label, shown.c_str())) {
        for (const OptionValue& v : presets) {
            const bool sel = !st.custom && v.value == current;
            if (ImGui::Selectable(v.label.empty() ? v.value.c_str() : v.label.c_str(), sel)) {
                st.custom = false;
                if (v.value != current) {
                    picked = v.value;
                    changed = true;
                }
            }
            if (sel) ImGui::SetItemDefaultFocus();
        }
        ImGui::Separator();
        if (ImGui::Selectable("Custom...", st.custom)) {
            st.custom = true;
            st.autoOpened = false;
            st.focus = true;
            // Start from the current value when it is a number / address (an "off" or "1x" preset is not).
            std::snprintf(st.buf, sizeof st.buf, "%s", validTypedValue(t, current) ? current.c_str() : "");
        }
        ImGui::EndCombo();
    }
    return changed;
}

// The custom box (call only while st.custom).  Returns true with `picked` set (canonical form) when a
// valid value different from `current` was entered.
inline bool typedInput(const TypedOption& t, const std::string& current, TypedState& st, std::string& picked) {
    bool applied = false;
    if (st.focus) {
        ImGui::SetKeyboardFocusHere();
        st.focus = false;
    }
    ImGui::SetNextItemWidth(fitWidth(0.34f, 8.0f, 16.0f));
    const bool enter = ImGui::InputTextWithHint("##custom", t.hint, st.buf, sizeof st.buf,
                                                ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
    const bool left = ImGui::IsItemDeactivatedAfterEdit();
    std::string why;
    const bool ok = validTypedValue(t, st.buf, &why);
    if ((enter || left) && ok) {
        std::string v = canonicalTypedValue(t, st.buf);
        if (v != current) {
            picked = v;
            applied = true;
        }
    }
    if (t.unit && t.unit[0]) {
        ImGui::SameLine();
        ImGui::TextDisabled("%s", t.unit);
    }
    if (!ok) {
        ImGui::PushStyleColor(ImGuiCol_Text, typedRed());
        ImGui::TextWrapped("Not applied: %s", why.c_str());
        ImGui::PopStyleColor();
    } else if (canonicalTypedValue(t, st.buf) != current) {
        hint("Press Enter to apply.");
    }
    return applied;
}

}  // namespace ui
}  // namespace vj
