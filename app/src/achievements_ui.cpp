// ImGui for RetroAchievements (#836): the menu, the sign-in window, the
// achievement list, unlock toasts, leaderboard trackers and challenge markers.
#include "imgui.h"
#include "imgui_internal.h"   // ClearActiveID

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>

#include "app.h"
#include "platform/platform.h"
#include "ui_util.h"
#include "rc_client.h"

namespace vj {

namespace {

void drawLogin(App& app) {
    Achievements& a = app.ach;
    ui::nextWindow(28.0f, 0.0f, 0.0f, 0.0f, ui::WinFit::FixedWidth);   // fixed width, height follows the text
    bool open = true;
    bool shown = ImGui::Begin("Sign in to RetroAchievements", &open, ImGuiWindowFlags_AlwaysAutoResize);
    ui::keepOnScreen();
    if (!shown) {
        ImGui::End();
        if (!open) a.showLogin = false;
        return;
    }
    ImGui::TextWrapped("Your password is sent once to retroachievements.org to get a login token. Only the token "
                       "is saved (in the app's config file); the password is wiped as soon as you press Sign in.");
    ImGui::Separator();
    if (!AchHttp::available()) ui::coloredWrapped(ImVec4(1, 0.6f, 0.4f, 1), "This build has no HTTP support.");
    const bool busy = a.loginBusy();
    ImGui::BeginDisabled(busy);
    if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
    ImGui::PushItemWidth(ui::fitWidth(0.62f, 10.0f, 24.0f));
    ImGui::InputText("User name", a.userBuf, sizeof a.userBuf);
    bool enter = ImGui::InputText("Password", a.passBuf, sizeof a.passBuf,
                                  ImGuiInputTextFlags_Password | ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::PopItemWidth();
    bool go = ImGui::Button("Sign in") || enter;
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) open = false;
    if (go && a.userBuf[0] && a.passBuf[0]) {
        a.login(a.userBuf, a.passBuf);
        // The request is built; the password is not needed any more.
        std::memset(a.passBuf, 0, sizeof a.passBuf);
        ImGui::ClearActiveID();   // ImGui keeps its own copy of the text while the box is active
    }
    if (!a.statusLine().empty()) ImGui::TextWrapped("%s", a.statusLine().c_str());
    ImGui::End();
    if (!open) {
        std::memset(a.passBuf, 0, sizeof a.passBuf);
        a.showLogin = false;
    }
}

const char* stateLabel(uint8_t unlocked) {
    if (unlocked & RC_CLIENT_ACHIEVEMENT_UNLOCKED_HARDCORE) return "Unlocked (hardcore)";
    if (unlocked & RC_CLIENT_ACHIEVEMENT_UNLOCKED_SOFTCORE) return "Unlocked";
    return "Locked";
}

void drawList(App& app) {
    Achievements& a = app.ach;
    rc_client_t* c = a.client();
    ui::nextWindow(34.0f, 30.0f, 22.0f, 14.0f);
    bool shown = ImGui::Begin("Achievements", &a.showList);
    ui::keepOnScreen();
    if (!shown) {
        ImGui::End();
        return;
    }
    const rc_client_game_t* g = c ? rc_client_get_game_info(c) : nullptr;
    if (!c || !rc_client_is_game_loaded(c) || !g || g->id == 0) {
        ImGui::TextWrapped("%s", a.statusLine().empty() ? "No tracked game is loaded." : a.statusLine().c_str());
        ImGui::End();
        return;
    }
    ImGui::TextWrapped("%s", g->title ? g->title : "");
    rc_client_user_game_summary_t s;
    rc_client_get_user_game_summary(c, &s);
    ui::hint("%u of %u achievements, %u of %u points   %s", s.num_unlocked_achievements, s.num_core_achievements,
             s.points_unlocked, s.points_core, rc_client_get_hardcore_enabled(c) ? "Hardcore" : "Softcore");
    if (s.num_core_achievements)
        ImGui::ProgressBar((float)s.num_unlocked_achievements / (float)s.num_core_achievements, ImVec2(-1, 0));
    if (rc_client_has_rich_presence(c)) {
        char rp[256];
        rc_client_get_rich_presence_message(c, rp, sizeof rp);
        ImGui::TextWrapped("Now: %s", rp);
    }
    ImGui::Separator();

    rc_client_achievement_list_t* list = rc_client_create_achievement_list(
        c, RC_CLIENT_ACHIEVEMENT_CATEGORY_CORE_AND_UNOFFICIAL, RC_CLIENT_ACHIEVEMENT_LIST_GROUPING_LOCK_STATE);
    if (list) {
        ImGui::BeginChild("##ach", ImVec2(0, 0));
        for (uint32_t b = 0; b < list->num_buckets; b++) {
            const rc_client_achievement_bucket_t& bk = list->buckets[b];
            if (bk.num_achievements == 0) continue;
            char head[96];
            std::snprintf(head, sizeof head, "%s (%u)###b%u", bk.label ? bk.label : "", bk.num_achievements, b);
            if (!ImGui::CollapsingHeader(head, ImGuiTreeNodeFlags_DefaultOpen)) continue;
            for (uint32_t i = 0; i < bk.num_achievements; i++) {
                const rc_client_achievement_t* ac = bk.achievements[i];
                ImGui::PushID((int)ac->id);
                const bool done = ac->unlocked != 0;
                {
                    char line[320];
                    std::snprintf(line, sizeof line, "%s  (%u)", ac->title ? ac->title : "", ac->points);
                    ui::coloredWrapped(done ? ImVec4(0.95f, 0.82f, 0.35f, 1) : ImVec4(0.85f, 0.85f, 0.85f, 1), line);
                }
                ImGui::Indent();
                if (ac->description && ac->description[0]) ImGui::TextWrapped("%s", ac->description);
                if (done && ac->unlock_time) {
                    char when[48] = "";
                    std::tm tmv;
                    std::time_t tt = ac->unlock_time;
#ifdef _WIN32
                    if (localtime_s(&tmv, &tt) == 0) std::strftime(when, sizeof when, "%Y-%m-%d %H:%M", &tmv);
#else
                    if (localtime_r(&tt, &tmv)) std::strftime(when, sizeof when, "%Y-%m-%d %H:%M", &tmv);
#endif
                    ImGui::TextDisabled("%s  %s", stateLabel(ac->unlocked), when);
                } else if (!done && ac->measured_progress[0]) {
                    ImGui::TextDisabled("Progress: %s", ac->measured_progress);
                }
                ImGui::Unindent();
                ImGui::Spacing();
                ImGui::PopID();
            }
        }
        ImGui::EndChild();
        rc_client_destroy_achievement_list(list);
    }
    ImGui::End();
}

void drawToasts(App& app, ImVec2 areaPos, ImVec2 areaSize) {
    Achievements& a = app.ach;
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const double t = app.now();
    a.toasts.erase(std::remove_if(a.toasts.begin(), a.toasts.end(), [&](const AchToast& x) { return t - x.born > x.life; }),
                   a.toasts.end());
    const float fs = ImGui::GetFontSize();
    const float pad = fs * 0.6f;
    const float w = std::min(areaSize.x - pad * 2, fs * 26.0f);
    float y = areaPos.y + pad;
    for (const AchToast& x : a.toasts) {
        const float age = (float)(t - x.born), left = (float)x.life - age;
        const float al = std::min(1.0f, std::min(left / 0.5f, age / 0.15f + 0.2f));
        const ImVec2 tt = ImGui::CalcTextSize(x.title.c_str(), nullptr, false, w - pad * 2);
        const ImVec2 tb = ImGui::CalcTextSize(x.body.c_str(), nullptr, false, w - pad * 2);
        const float h = tt.y + (x.body.empty() ? 0 : tb.y + pad * 0.3f) + pad * 2;
        const ImVec2 p0(areaPos.x + areaSize.x - w - pad, y), p1(p0.x + w, y + h);
        ImU32 edge = x.kind == 0 ? IM_COL32(240, 200, 60, (int)(255 * al))
                   : x.kind == 2 ? IM_COL32(240, 110, 110, (int)(255 * al))
                                 : IM_COL32(120, 170, 240, (int)(255 * al));
        dl->AddRectFilled(p0, p1, IM_COL32(18, 18, 24, (int)(225 * al)), 6.0f);
        dl->AddRect(p0, p1, edge, 6.0f, 0, 2.0f);
        dl->AddText(nullptr, 0.0f, ImVec2(p0.x + pad, p0.y + pad), edge, x.title.c_str(), nullptr, w - pad * 2);
        if (!x.body.empty())
            dl->AddText(nullptr, 0.0f, ImVec2(p0.x + pad, p0.y + pad + tt.y + pad * 0.3f),
                        IM_COL32(235, 235, 235, (int)(255 * al)), x.body.c_str(), nullptr, w - pad * 2);
        y += h + pad * 0.5f;
    }
}

void drawIndicators(App& app, ImVec2 areaPos, ImVec2 areaSize) {
    Achievements& a = app.ach;
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const float fs = ImGui::GetFontSize();
    float y = areaPos.y + areaSize.y - fs * 2.6f;
    // Leaderboard trackers (bottom right) and active challenges (bottom centre).
    for (const auto& kv : a.trackers) {
        const ImVec2 ts = ImGui::CalcTextSize(kv.second.c_str());
        const ImVec2 p0(areaPos.x + areaSize.x - ts.x - fs * 1.4f, y);
        dl->AddRectFilled(p0, ImVec2(p0.x + ts.x + fs, y + ts.y + fs * 0.5f), IM_COL32(0, 0, 0, 180), 4.0f);
        dl->AddText(ImVec2(p0.x + fs * 0.5f, y + fs * 0.25f), IM_COL32(255, 230, 120, 255), kv.second.c_str());
        y -= ts.y + fs * 0.8f;
    }
    float cx = areaPos.x + fs;
    for (const auto& kv : a.challenges) {
        const std::string label = "! " + kv.second;
        const ImVec2 ts = ImGui::CalcTextSize(label.c_str());
        const float yy = areaPos.y + areaSize.y - fs * 2.6f;
        dl->AddRectFilled(ImVec2(cx, yy), ImVec2(cx + ts.x + fs, yy + ts.y + fs * 0.5f), IM_COL32(60, 20, 20, 190), 4.0f);
        dl->AddText(ImVec2(cx + fs * 0.5f, yy + fs * 0.25f), IM_COL32(255, 150, 150, 255), label.c_str());
        cx += ts.x + fs * 1.4f;
        if (cx > areaPos.x + areaSize.x * 0.6f) break;
    }
    if (!a.progress.empty()) {
        if (app.now() - a.progressBorn > 4.0) {
            a.progress.clear();
        } else {
            const ImVec2 ts = ImGui::CalcTextSize(a.progress.c_str());
            const ImVec2 p0(areaPos.x + (areaSize.x - ts.x) * 0.5f, areaPos.y + areaSize.y - fs * 4.4f);
            dl->AddRectFilled(ImVec2(p0.x - fs * 0.5f, p0.y), ImVec2(p0.x + ts.x + fs * 0.5f, p0.y + ts.y + fs * 0.5f),
                              IM_COL32(0, 0, 0, 190), 4.0f);
            dl->AddText(ImVec2(p0.x, p0.y + fs * 0.25f), IM_COL32(220, 240, 255, 255), a.progress.c_str());
        }
    }
}

}  // namespace

void drawAchievementsMenu(App& app) {
    Achievements& a = app.ach;
    if (!a.available()) return;
    if (!ImGui::BeginMenu("Achievements")) return;
    if (a.signedIn()) {
        ImGui::TextDisabled("Signed in as %s", a.userName().c_str());
        if (ImGui::MenuItem("Sign out")) a.logout();
    } else if (a.loginBusy()) {
        ImGui::TextDisabled("Signing in...");
    } else {
        if (ImGui::MenuItem("Sign in...")) a.showLogin = true;
    }
    ImGui::Separator();
    bool en = a.enabled();
    if (ImGui::MenuItem("Enable RetroAchievements", nullptr, &en)) a.setEnabled(en);
    bool hc = a.hardcoreWanted();
    if (ImGui::MenuItem("Hardcore mode", nullptr, &hc)) a.setHardcoreWanted(hc);
    if (a.hardcoreWanted() && !a.hardcoreBlocker().empty())
        ImGui::TextDisabled("  paused: %s", a.hardcoreBlocker().c_str());
    else if (a.hardcoreActive())
        ImGui::TextDisabled("  active: no save-state loads, cheats, rewind or slow motion");
    bool un = a.unofficialWanted();
    if (ImGui::MenuItem("Include unofficial achievements", nullptr, &un)) a.setUnofficialWanted(un);
    ImGui::Separator();
    if (ImGui::MenuItem("Achievement list...", nullptr, false, app.host.isLoaded())) a.showList = true;
    if (!a.statusLine().empty()) {
        ImGui::Separator();
        ImGui::PushTextWrapPos(std::min(ImGui::GetFontSize() * 24.0f, ui::workSize().x * 0.6f));
        ImGui::TextDisabled("%s", a.statusLine().c_str());
        ImGui::PopTextWrapPos();
    }
    ImGui::EndMenu();
}

void drawAchievementsUi(App& app) {
    Achievements& a = app.ach;
    if (!a.available()) return;
    if (a.showLogin) drawLogin(app);
    if (a.showList) drawList(app);
    ImGuiViewport* vp = ImGui::GetMainViewport();
    const float menuH = app.inWindowMenuBar() ? ImGui::GetFrameHeight() : 0.0f;   // no in-window menu bar under a native one
    const ImVec2 pos(vp->Pos.x, vp->Pos.y + menuH), size(vp->Size.x, vp->Size.y - menuH);
    drawToasts(app, pos, size);
    drawIndicators(app, pos, size);
}

}  // namespace vj
