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
#include "ra_dash.h"
#include "ra_images.h"
#include "secret_store.h"
#include "ui_util.h"
#include "rc_client.h"

namespace vj {

namespace {

const char* const kTokenInFileHint = "Token stored in the settings file because no system keyring is available";

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
                       "is saved (in your system keychain where there is one); the password is wiped as soon as you "
                       "press Sign in.");
    if (a.tokenInSettingsFile() || !secret::active().available()) ui::hint("%s", kTokenInFileHint);
    ImGui::Separator();
    if (!AchHttp::available()) ui::coloredWrapped(ui::vec4(ui::pal().warning), "This build has no HTTP support.");
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

// ---- pictures ---------------------------------------------------------------------------------

// Theme colours (theme.h): functions, because the scheme can change while the app runs.
ImVec4 kGold() { return ui::vec4(ui::pal().gold); }
ImVec4 kGrey() { return ui::vec4(ui::pal().textSecondary); }
ImVec4 kAmber() { return ui::vec4(ui::pal().warning); }

// The picture at `url`, `size` pixels square, or a quiet placeholder while it loads (or when it is
// not available: offline, never downloaded).  Only pictures that are on screen are requested.
void picture(Achievements& a, const std::string& url, float size, const std::string& version = "", bool dim = false) {
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const ImVec2 sz(size, size);
    ImGui::Dummy(sz);
    if (url.empty() || !ImGui::IsRectVisible(p, ImVec2(p.x + size, p.y + size))) return;
    const ra::ImageView v = a.images.get(url, version);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (v.state == ra::ImageView::Ready) {
        dl->AddImage(ImTextureRef((ImTextureID)(intptr_t)v.tex), p, ImVec2(p.x + size, p.y + size), ImVec2(0, 0), ImVec2(1, 1),
                     dim ? IM_COL32(255, 255, 255, 150) : IM_COL32_WHITE);
        return;
    }
    dl->AddRectFilled(p, ImVec2(p.x + size, p.y + size), ui::u32(ui::pal().pictureBg, v.state == ra::ImageView::Loading ? 90.0f / 255.0f : 50.0f / 255.0f), 4.0f);
}

std::string clock(long long t) {
    char when[48] = "";
    std::tm tmv;
    std::time_t tt = (std::time_t)t;
#ifdef _WIN32
    if (localtime_s(&tmv, &tt) == 0) std::strftime(when, sizeof when, "%Y-%m-%d %H:%M", &tmv);
#else
    if (localtime_r(&tt, &tmv)) std::strftime(when, sizeof when, "%Y-%m-%d %H:%M", &tmv);
#endif
    return when;
}

std::string day(long long t) {
    char when[48] = "";
    std::tm tmv;
    std::time_t tt = (std::time_t)t;
#ifdef _WIN32
    if (localtime_s(&tmv, &tt) == 0) std::strftime(when, sizeof when, "%Y-%m-%d", &tmv);
#else
    if (localtime_r(&tt, &tmv)) std::strftime(when, sizeof when, "%Y-%m-%d", &tmv);
#endif
    return when;
}

// One line saying which rule set is in force.  Hardcore is gold; wanted-but-blocked says what blocks it.
void modeChip(Achievements& a) {
    if (a.clientSnapshot().hardcore) {
        ui::coloredWrapped(kGold(), "Hardcore: unlocks count for the hardcore ranking");
    } else if (a.hardcoreWanted() && !a.hardcoreBlocker().empty()) {
        char buf[200];
        std::snprintf(buf, sizeof buf, "Softcore (hardcore is paused: %s)", a.hardcoreBlocker().c_str());
        ui::coloredWrapped(kAmber(), buf);
    } else if (a.hardcoreWanted()) {
        ui::coloredWrapped(kAmber(), "Hardcore turns on when a game is running");
    } else {
        ImGui::TextDisabled("Softcore");
    }
}

template <class T, size_t N>
bool labelCombo(const char* id, float width, T& value, const T (&all)[N], const char* (*label)(T)) {
    bool changed = false;
    ImGui::SetNextItemWidth(width);
    if (ImGui::BeginCombo(id, label(value))) {
        for (size_t i = 0; i < N; i++) {
            const bool sel = all[i] == value;
            if (ImGui::Selectable(label(all[i]), sel)) {
                value = all[i];
                changed = true;
            }
            if (sel) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    return changed;
}

const ra::AchFilter kAchFilters[] = {ra::AchFilter::All, ra::AchFilter::Unlocked, ra::AchFilter::Locked, ra::AchFilter::Hardcore,
                                     ra::AchFilter::Missable};
const ra::AchSort kAchSorts[] = {ra::AchSort::Default, ra::AchSort::Title, ra::AchSort::Points,
                                 ra::AchSort::Rarity,  ra::AchSort::Recent, ra::AchSort::Type};
const ra::GameFilter kGameFilters[] = {ra::GameFilter::All, ra::GameFilter::Owned, ra::GameFilter::InProgress,
                                       ra::GameFilter::Completed, ra::GameFilter::NotStarted};
const ra::GameSort kGameSorts[] = {ra::GameSort::Title, ra::GameSort::Points, ra::GameSort::Achievements,
                                   ra::GameSort::Progress, ra::GameSort::OwnedFirst};

void signInPrompt(Achievements& a, const char* why) {
    ui::hint("%s", why);
    if (a.loginBusy()) ImGui::TextDisabled("Signing in...");
    else if (ImGui::Button("Sign in...")) a.showLogin = true;
}

// ---- This game ------------------------------------------------------------------------------------

void drawThisGame(App& app) {
    Achievements& a = app.ach;
    const auto snapshot = a.clientSnapshot();
    if (snapshot.gameId == 0) {
        ImGui::TextWrapped("%s", a.statusLine().empty() ? "No tracked game is loaded." : a.statusLine().c_str());
        if (!a.signedIn()) signInPrompt(a, "Sign in to track achievements while you play.");
        ImGui::Spacing();
        ui::hint("The Jaguar games tab lists every game that has achievements, with your progress.");
        return;
    }
    const auto& s = snapshot.summary;
    const float icon = ui::em(4.0f);
    picture(a, a.gameIconUrl(), icon);
    ImGui::SameLine();
    ImGui::BeginGroup();
    ImGui::TextWrapped("%s", snapshot.gameTitle.c_str());
    modeChip(a);
    ImGui::Text("%u of %u achievements   %u of %u points", s.num_unlocked_achievements, s.num_core_achievements, s.points_unlocked,
                s.points_core);
    if (s.num_core_achievements)
        ImGui::ProgressBar((float)s.num_unlocked_achievements / (float)s.num_core_achievements, ImVec2(-1, 0));
    ImGui::EndGroup();
    const std::string rp = a.richPresence();   // computed on the emulation thread, published every 60 frames (#967)
    if (!rp.empty()) ImGui::TextWrapped("Now: %s", rp.c_str());
    ImGui::Separator();

    const float w = ImGui::GetContentRegionAvail().x;
    labelCombo("##af", std::min(ui::em(11), w * 0.3f), a.achFilter, kAchFilters, &ra::achFilterLabel);
    ImGui::SameLine();
    labelCombo("##as", std::min(ui::em(11), w * 0.3f), a.achSort, kAchSorts, &ra::achSortLabel);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(std::max(ui::em(6), ImGui::GetContentRegionAvail().x));
    ImGui::InputTextWithHint("##at", "Search achievements", a.achText, sizeof a.achText);

    std::vector<ra::AchRow> rows;
    a.achievementRows(rows);
    const std::vector<size_t> view = ra::viewAchievements(rows, a.achFilter, a.achSort, a.achText);
    ImGui::BeginChild("##ach", ImVec2(0, 0));
    if (view.empty()) ImGui::TextDisabled("%s", rows.empty() ? "This game has no achievements yet." : "Nothing matches the filter.");
    const float badge = ui::em(3.2f);
    for (size_t vi : view) {
        const ra::AchRow& r = rows[vi];
        ImGui::PushID((int)r.id);
        picture(a, r.unlocked() ? r.badgeUrl : r.badgeLockedUrl, badge);
        ImGui::SameLine();
        ImGui::BeginGroup();
        {
            char line[320];
            std::snprintf(line, sizeof line, "%s  (%u)", r.title.c_str(), r.points);
            ui::coloredWrapped(r.unlocked() ? kGold() : kGrey(), line);
        }
        if (!r.description.empty()) ImGui::TextWrapped("%s", r.description.c_str());
        // The status line: earned how and when, or how far along, plus the tags.
        std::string st;
        if (r.unlockedHard) st = "Unlocked in hardcore";
        else if (r.unlockedSoft) st = "Unlocked (softcore)";
        else st = "Locked";
        if (r.unlocked() && r.unlockTime) st += "  " + clock(r.unlockTime);
        if (!r.unlocked() && !r.progress.empty()) st += "  Progress: " + r.progress;
        if (r.rarity > 0) {
            char pct[48];
            std::snprintf(pct, sizeof pct, "  %.1f%% of players", r.rarity);
            st += pct;
        }
        if (const char* t = ra::achTypeLabel(r.type); *t) st += std::string("  [") + t + "]";
        if (r.unofficial) st += "  [unofficial]";
        if (r.unsupported) st += "  [not supported by this version]";
        ImGui::TextDisabled("%s", st.c_str());
        ImGui::EndGroup();
        ImGui::Spacing();
        ImGui::PopID();
    }
    ImGui::EndChild();
}

// ---- Jaguar games -----------------------------------------------------------------------------------

void drawGames(App& app) {
    Achievements& a = app.ach;
    const std::vector<ra::GameEntry>& games = a.games();
    size_t owned = 0;
    const size_t withSets = ra::countWithSets(games, owned);
    if (games.empty()) {
        ImGui::TextWrapped("%s", a.gamesBusy() ? "Loading the list of Atari Jaguar and Jaguar CD games on RetroAchievements..."
                                              : "No games list yet.");
    } else {
        ImGui::Text("%zu games have achievements on RetroAchievements; %zu are in your library.", withSets, owned);
    }
    if (a.gamesFetchedAt()) ImGui::TextDisabled("List updated %s%s", day(a.gamesFetchedAt()).c_str(), a.gamesFromCache() ? " (saved copy)" : "");
    if (!a.gamesError().empty()) ui::coloredWrapped(ui::vec4(ui::pal().error), a.gamesError().c_str());
    if (a.libraryHashing()) ImGui::TextDisabled("Matching your cartridges against RetroAchievements...");
    if (!a.signedIn()) signInPrompt(a, "Sign in to see your own progress in each game.");
    else if (!a.progressKnown() && !a.gamesBusy()) ImGui::TextDisabled("Your progress has not loaded yet.");

    ImGui::BeginDisabled(a.gamesBusy());
    if (ImGui::Button(a.gamesBusy() ? "Refreshing..." : "Refresh")) a.refreshGames(true);
    ImGui::EndDisabled();
    ImGui::SameLine();
    const float w = ImGui::GetContentRegionAvail().x;
    labelCombo("##gf", std::min(ui::em(11), w * 0.3f), a.gameFilter, kGameFilters, &ra::gameFilterLabel);
    ImGui::SameLine();
    labelCombo("##gs", std::min(ui::em(13), w * 0.34f), a.gameSort, kGameSorts, &ra::gameSortLabel);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(std::max(ui::em(6), ImGui::GetContentRegionAvail().x));
    ImGui::InputTextWithHint("##gt", "Search games", a.gameText, sizeof a.gameText);

    const std::vector<size_t> view = ra::viewGames(games, a.gameFilter, a.gameSort, a.gameText);
    const uint32_t curId = a.clientSnapshot().gameId;

    const float icon = ui::em(2.6f);
    const float rowH = icon + ImGui::GetStyle().CellPadding.y * 2;
    if (ImGui::BeginTable("##games", 5,
                          ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                              ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_Resizable)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Game", ImGuiTableColumnFlags_WidthStretch, 3.0f);
        ImGui::TableSetupColumn("Achievements", ImGuiTableColumnFlags_WidthFixed, ui::em(6.5f));
        ImGui::TableSetupColumn("Points", ImGuiTableColumnFlags_WidthFixed, ui::em(4.0f));
        ImGui::TableSetupColumn("My progress", ImGuiTableColumnFlags_WidthStretch, 1.6f);
        ImGui::TableSetupColumn("##play", ImGuiTableColumnFlags_WidthFixed, ui::em(5.0f));
        ImGui::TableHeadersRow();
        ImGuiListClipper clip;
        clip.Begin((int)view.size(), rowH);
        while (clip.Step()) {
            for (int row = clip.DisplayStart; row < clip.DisplayEnd; row++) {
                const ra::GameEntry& g = games[view[(size_t)row]];
                ImGui::PushID((int)g.id);
                ImGui::TableNextRow(0, rowH);
                ImGui::TableSetColumnIndex(0);
                picture(a, g.imageUrl, icon);
                ImGui::SameLine();
                ImGui::AlignTextToFramePadding();
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (icon - ImGui::GetTextLineHeight() * 2) * 0.5f);
                ImGui::BeginGroup();
                ImGui::TextUnformatted(g.title.c_str());
                ImGui::TextDisabled("%s", g.console == ra::kConsoleJaguarCD ? "Jaguar CD" : "Cartridge");
                ImGui::EndGroup();
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%u", g.achievements);
                ImGui::TableSetColumnIndex(2);
                ImGui::Text("%u", g.points);
                ImGui::TableSetColumnIndex(3);
                if (g.haveProgress && g.achievements) {
                    const unsigned got = std::max(g.unlocked, g.unlockedHardcore);
                    char ov[48];
                    std::snprintf(ov, sizeof ov, "%u / %u", got, g.achievements);
                    const bool done = got >= g.achievements;
                    if (done) {
                        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ui::vec4(ui::pal().goldFill));
                        ImGui::PushStyleColor(ImGuiCol_Text, ui::vec4(ui::pal().goldFillText));   // the "n / n" drawn on the gold
                    }
                    ImGui::ProgressBar((float)got / (float)g.achievements, ImVec2(-1, 0), ov);
                    if (done) ImGui::PopStyleColor(2);
                    if (ImGui::IsItemHovered()) {
                        ImGui::BeginTooltip();
                        ImGui::Text("%u unlocked, %u of them in hardcore", got, g.unlockedHardcore);
                        ImGui::EndTooltip();
                    }
                } else {
                    ImGui::TextDisabled("-");
                }
                ImGui::TableSetColumnIndex(4);
                if (g.id == curId) {
                    ImGui::TextColored(kGold(), "Playing");
                } else if (g.libIndex >= 0) {
                    if (ImGui::Button("Play")) a.playGame(g);
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", g.matchedByHash ? "In your library (matched by checksum)" : "In your library (matched by title)");
                }
                ImGui::PopID();
            }
        }
        ImGui::EndTable();
    }
}

// ---- Profile ----------------------------------------------------------------------------------------

void drawProfile(App& app) {
    Achievements& a = app.ach;
    const auto snapshot = a.clientSnapshot();
    if (!snapshot.signedIn) {
        signInPrompt(a, "Sign in to see your RetroAchievements profile.");
    } else {
        std::string ver;
        const std::string url = a.avatarUrl(ver);
        picture(a, url, ui::em(6.0f), ver);
        ImGui::SameLine();
        ImGui::BeginGroup();
        ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.4f);
        ImGui::TextUnformatted(snapshot.displayName.c_str());
        ImGui::PopFont();
        ImGui::Text("Hardcore points: %u", snapshot.score);
        ImGui::Text("Softcore points: %u", snapshot.softcoreScore);
        if (snapshot.unreadMessages) ImGui::TextColored(kAmber(), "%u unread message%s on retroachievements.org", snapshot.unreadMessages, snapshot.unreadMessages == 1 ? "" : "s");
        modeChip(a);
        ImGui::EndGroup();
        ImGui::Spacing();
        if (ImGui::Button("Sign out")) a.logout();
        if (a.tokenInSettingsFile()) ui::hint("%s", kTokenInFileHint);
    }
    ImGui::Separator();
    bool img = a.imagesWanted();
    if (ImGui::Checkbox("Download pictures (avatar, game icons, achievement badges)", &img)) a.setImagesWanted(img);
    ui::hint("Pictures are saved in the app's data folder (ra-cache) and kept between runs. They follow the Library's "
             "\"download art\" switch: with it off, only pictures already saved are shown.");
    ImGui::Separator();
    ImGui::Text("Unlocked this session");
    if (a.sessionUnlocks.empty()) {
        ui::hint("Nothing yet. The profile page on retroachievements.org has your full history.");
        return;
    }
    ImGui::BeginChild("##sess", ImVec2(0, 0));
    for (const SessionUnlock& s : a.sessionUnlocks) {
        ImGui::PushID(&s);
        picture(a, s.badgeUrl, ui::em(2.6f));
        ImGui::SameLine();
        ImGui::BeginGroup();
        ImGui::TextColored(kGold(), "%s  (%u)", s.title.c_str(), s.points);
        ImGui::TextDisabled("%s  %s", s.hardcore ? "Hardcore" : "Softcore", clock(s.when).c_str());
        ImGui::EndGroup();
        ImGui::PopID();
    }
    ImGui::EndChild();
}

// ---- the window ----------------------------------------------------------------------------------------

void drawList(App& app) {
    Achievements& a = app.ach;
    ui::nextWindow(42.0f, 36.0f, 26.0f, 18.0f);
    bool shown = ImGui::Begin("Achievements", &a.showList);
    ui::keepOnScreen();
    if (!shown) {
        ImGui::End();
        return;
    }
    static int applied = -1;   // the tab last forced open (a menu item picks a tab)
    const int want = a.dashTab;
    const bool pending = applied != want;
    if (ImGui::BeginTabBar("##racheck")) {
        const char* names[3] = {"This game", "Jaguar games", "Profile"};
        for (int i = 0; i < 3; i++) {
            ImGuiTabItemFlags fl = (pending && want == i) ? ImGuiTabItemFlags_SetSelected : 0;
            if (ImGui::BeginTabItem(names[i], nullptr, fl)) {
                if (!pending) a.dashTab = i;   // follow the user's clicks; a pending request is applied first
                ImGui::BeginChild("##tabbody", ImVec2(0, 0));
                if (i == 0) drawThisGame(app);
                else if (i == 1) drawGames(app);
                else drawProfile(app);
                ImGui::EndChild();
                ImGui::EndTabItem();
            }
        }
        ImGui::EndTabBar();
    }
    applied = want;
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
    const float iconPx = fs * 3.0f;
    float y = areaPos.y + pad;
    for (const AchToast& x : a.toasts) {
        const float age = (float)(t - x.born), left = (float)x.life - age;
        const float al = std::min(1.0f, std::min(left / 0.5f, age / 0.15f + 0.2f));
        // A picture takes its place from the start (while loading) unless it cannot arrive at all.
        ra::ImageView iv;
        if (!x.iconUrl.empty()) iv = a.images.get(x.iconUrl);
        const bool hasIcon = iv.state == ra::ImageView::Ready || iv.state == ra::ImageView::Loading;
        const float textX = hasIcon ? pad * 2 + iconPx : pad;
        const float textW = w - textX - pad;
        const ImVec2 tt = ImGui::CalcTextSize(x.title.c_str(), nullptr, false, textW);
        const ImVec2 tb = ImGui::CalcTextSize(x.body.c_str(), nullptr, false, textW);
        const float textH = tt.y + (x.body.empty() ? 0 : tb.y + pad * 0.3f);
        const float h = std::max(textH, hasIcon ? iconPx : 0.0f) + pad * 2;
        const ImVec2 p0(areaPos.x + areaSize.x - w - pad, y), p1(p0.x + w, y + h);
        // On the game picture: the overlay palette, dark-translucent in both themes.
        const theme::Overlay& ov = ui::overlay();
        ImU32 edge = ui::u32(x.kind == 0 ? ov.toastGold : x.kind == 2 ? ov.toastRed : ov.toastBlue, al);
        dl->AddRectFilled(p0, p1, ui::u32(ov.toastBg, al), 6.0f);
        dl->AddRect(p0, p1, edge, 6.0f, 0, 2.0f);
        if (hasIcon) {
            const ImVec2 i0(p0.x + pad, p0.y + pad), i1(i0.x + iconPx, i0.y + iconPx);
            if (iv.state == ra::ImageView::Ready)
                dl->AddImage(ImTextureRef((ImTextureID)(intptr_t)iv.tex), i0, i1, ImVec2(0, 0), ImVec2(1, 1),
                             IM_COL32(255, 255, 255, (int)(255 * al)));
            else
                dl->AddRectFilled(i0, i1, ui::u32(ov.toastIconBg, al), 4.0f);
        }
        dl->AddText(nullptr, 0.0f, ImVec2(p0.x + textX, p0.y + pad), edge, x.title.c_str(), nullptr, textW);
        if (!x.body.empty())
            dl->AddText(nullptr, 0.0f, ImVec2(p0.x + textX, p0.y + pad + tt.y + pad * 0.3f),
                        ui::u32(ov.toastBody, al), x.body.c_str(), nullptr, textW);
        y += h + pad * 0.5f;
    }
}

void drawIndicators(App& app, ImVec2 areaPos, ImVec2 areaSize) {
    Achievements& a = app.ach;
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const float fs = ImGui::GetFontSize();
    const theme::Overlay& ov = ui::overlay();
    float y = areaPos.y + areaSize.y - fs * 2.6f;
    // Leaderboard trackers (bottom right) and active challenges (bottom centre).
    for (const auto& kv : a.trackers) {
        const ImVec2 ts = ImGui::CalcTextSize(kv.second.c_str());
        const ImVec2 p0(areaPos.x + areaSize.x - ts.x - fs * 1.4f, y);
        dl->AddRectFilled(p0, ImVec2(p0.x + ts.x + fs, y + ts.y + fs * 0.5f), ui::u32(ov.hudBg), 4.0f);
        dl->AddText(ImVec2(p0.x + fs * 0.5f, y + fs * 0.25f), ui::u32(ov.hudText), kv.second.c_str());
        y -= ts.y + fs * 0.8f;
    }
    float cx = areaPos.x + fs;
    for (const auto& kv : a.challenges) {
        const std::string label = "! " + kv.second;
        const ImVec2 ts = ImGui::CalcTextSize(label.c_str());
        const float yy = areaPos.y + areaSize.y - fs * 2.6f;
        dl->AddRectFilled(ImVec2(cx, yy), ImVec2(cx + ts.x + fs, yy + ts.y + fs * 0.5f), ui::u32(ov.challengeBg), 4.0f);
        dl->AddText(ImVec2(cx + fs * 0.5f, yy + fs * 0.25f), ui::u32(ov.challengeText), label.c_str());
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
                              ui::u32(ov.progressBg), 4.0f);
            dl->AddText(ImVec2(p0.x, p0.y + fs * 0.25f), ui::u32(ov.progressText), a.progress.c_str());
        }
    }
}

}  // namespace

void drawAchievementsMenu(App& app) {
    Achievements& a = app.ach;
    if (!a.available()) return;
    if (!ImGui::BeginMenu("Achievements")) return;
    if (a.signedIn()) {
        std::string ver;
        const std::string url = a.avatarUrl(ver);
        const float px = ImGui::GetFontSize() * 1.6f;
        if (!url.empty()) {
            picture(a, url, px, ver);
            ImGui::SameLine();
            ImGui::AlignTextToFramePadding();
        }
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
    bool img = a.imagesWanted();
    if (ImGui::MenuItem("Download pictures", nullptr, &img)) a.setImagesWanted(img);
    ImGui::Separator();
    if (ImGui::MenuItem("Achievement list...", nullptr, false, app.host.isLoaded())) {
        a.showList = true;
        a.dashTab = 0;
    }
    if (ImGui::MenuItem("Jaguar games on RetroAchievements...")) {
        a.showList = true;
        a.dashTab = 1;
    }
    if (ImGui::MenuItem("Profile...", nullptr, false, a.signedIn())) {
        a.showList = true;
        a.dashTab = 2;
    }
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
