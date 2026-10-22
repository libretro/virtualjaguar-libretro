// The app's side of the native-OS integration (platform/platform.h): the state the native menus are
// drawn from, the dispatcher that runs their clicks, the tray, and the two settings that go with it.
#include "app.h"

#include "imgui.h"
#include "platform/platform.h"
#include "platform/viewports.h"
#include "theme.h"
#include "tv/tv_shell.h"
#include "ui_util.h"

namespace vj {

// ---- the state native menus are drawn from ------------------------------------------------------
platform::MenuState App::menuState() {
    platform::MenuState s;
    s.gameLoaded = host.isLoaded();
    s.paused = paused;
    s.fastForward = fastForward;
    s.fullscreen = fullscreen;
    s.stateSlot = stateSlot;
    for (int n = 0; n < 10; n++)
        if (slotExists(n)) s.usedSlots |= 1u << n;
    s.canLoadState = slotExists(stateSlot);
    s.mute = mute;
    s.integerScale = present.integerScale;
    s.smoothFilter = present.linearFilter;
    s.shader = video.shader.enabled;
    s.showFps = showFps;
    s.keypadOverlay = input.overlay.open;
    s.controlsOverlay = input.controls.show;
    s.vsync = vsync;
    s.homeLibrary = homeLibrary;
    s.tvMode = tvMode.active();
    s.tvAvailable = tvSetting != tv::TvSetting::Off;
    s.libraryOpen = homeVisible() || library.show || (tvMode.active() && tvShell().showingLibrary());
    if (s.gameLoaded) {
        DiscInfo d = host.discInfo();
        s.disc.available = d.available;
        s.disc.ejected = d.ejected;
        s.disc.count = d.count;
        s.disc.index = d.index;
        s.disc.labels = d.labels;
    }
    s.recents = recents;
    s.achAvailable = ach.available();
    s.achSignedIn = ach.signedIn();
    s.achBusy = ach.loginBusy();
    s.achEnabled = ach.enabled();
    s.achHardcore = ach.hardcoreWanted();
    s.achUnofficial = ach.unofficialWanted();
    s.netplayConfigured = netplay.configured();
    s.toolsOn = debug.enabled();
    s.halted = debug.halted();
    s.attached = debug.attached();
    s.toolsWhy = debug.disabledReason();
    for (const DebugTool& t : debug.tools()) {
        platform::ToolEntry e;
        e.title = t.label;
        e.enabled = debug.toolAvailable(t);
        e.checked = t.isOpen && t.isOpen();
        s.tools.push_back(e);
    }
    return s;
}

// ---- running a click ------------------------------------------------------------------------------------
void App::runNativeCommand(platform::Cmd cmd, int arg) {
    // Local class: same access to App's private members as this function has.
    struct Run : platform::Actions {
        App& a;
        explicit Run(App& app) : a(app) {}
        void open() override { a.showOpenDialog(); }
        void openRecent(int i) override {
            std::vector<std::string> list = a.recents;   // opening reorders the list
            if (i >= 0 && i < (int)list.size()) a.handleDrop(list[(size_t)i]);
        }
        void clearRecents() override {
            a.recents.clear();
            platform::clearRecents();
        }
        void bootBiosCd() override { a.bootBios(bios::Cd); }
        void bootBiosCart() override { a.bootBios(bios::CartBoot); }
        void showBiosSettings() override { a.openBiosSettings(); }
        void newWindow() override { a.newWindow(); }
        void toggleLibrary() override { a.openLibrary(); }   // (the TV Library while TV mode is on)
        void closeGame() override { a.closeContent(); }
        void revealGame() override { platform::revealFile(a.host.contentPath()); }
        void quit() override { a.quit = true; }
        void togglePause() override { a.togglePause(); }
        void reset() override { a.reset(); }
        void toggleFastForward() override { a.fastForward = !a.fastForward; }
        void saveState() override { a.saveState(a.stateSlot); }
        void loadState() override { a.loadState(a.stateSlot); }
        void setSlot(int n) override { a.stateSlot = n; }
        void showStateManager() override { a.showStates = true; }
        void showCheats() override { a.showCheats = true; }
        void toggleDiscTray() override {
            DiscInfo d = a.host.discInfo();
            if (d.available && d.count > 0) {   // an empty list (the CD BIOS with no disc) has nothing to insert
                const bool eject = !d.ejected;
                a.emu.call([&app = a, eject] { app.host.discSetEject(eject); }, a.detachForCall());
            }
        }
        void selectDisc(int i) override { a.emu.call([&app = a, idx = (unsigned)i] { app.host.discSetIndex(idx); }, a.detachForCall()); }
        void addDisc() override { a.showDiscDialog(); }
        void toggleFullscreen() override { a.setFullscreen(!a.fullscreen); }
        void toggleIntegerScale() override { a.present.integerScale = !a.present.integerScale; }
        void toggleSmoothFilter() override { a.present.linearFilter = !a.present.linearFilter; }
        void toggleShader() override {
            a.video.shader.enabled = !a.video.shader.enabled;
            a.video.shader.retry();
        }
        void toggleFps() override { a.showFps = !a.showFps; }
        void toggleKeypadOverlay() override { a.input.overlay.open = !a.input.overlay.open; }
        void toggleControlsOverlay() override {
            overlayEvent(a.input.controls, OverlayEvent::Toggle);
        }
        void toggleVsync() override {
            a.vsync = !a.vsync;
            a.applyVsync();
        }
        void toggleHomeLibrary() override { a.homeLibrary = !a.homeLibrary; }
        void toggleTvMode() override { a.toggleTvMode(); }
        void showSettings() override { a.openSettings(); }
        void toggleMute() override {
            a.mute = !a.mute;
            a.applyAudioSettings();
        }
        void showAbout() override { a.showAbout = true; }
        void showDataFolder() override { platform::openFolder(a.paths.root); }
        void openUrl(const char* url) override { SDL_OpenURL(url); }
        void achSignIn() override { a.ach.showLogin = true; }
        void achSignOut() override { a.ach.logout(); }
        void achToggleEnabled() override { a.ach.setEnabled(!a.ach.enabled()); }
        void achToggleHardcore() override { a.ach.setHardcoreWanted(!a.ach.hardcoreWanted()); }
        void achToggleUnofficial() override { a.ach.setUnofficialWanted(!a.ach.unofficialWanted()); }
        void achShowList() override { a.ach.showList = true; }
        void showNetplay() override { a.showNetplay = true; }
        void netplayDisconnect() override { a.netplay.stop(); }
        void toggleTool(int i) override {
            const std::vector<DebugTool>& tools = a.debug.tools();
            if (i < 0 || i >= (int)tools.size()) return;
            const DebugTool& t = tools[(size_t)i];
            if (a.debug.toolAvailable(t)) t.toggle();   // greyed (no game / hardcore / not halted) stays a no-op
        }
        void debugBreakContinue() override {
            if (a.debug.halted()) a.debug.resume();
            else if (a.debug.enabled()) a.debug.breakNow();
        }
        void debugStep() override {
            if (a.debug.halted()) a.debug.step();
        }
        void debugDetach() override {
            if (a.debug.attached()) a.debug.detach();
        }
        void showWindow() override {
            if (!a.window_) return;
            SDL_ShowWindow(a.window_);
            SDL_RestoreWindow(a.window_);
            SDL_RaiseWindow(a.window_);
        }
    } run(*this);
    const int discCount = host.isLoaded() ? (int)host.discInfo().count : 0;
    platform::dispatch(cmd, arg, run, (int)recents.size(), discCount, (int)debug.tools().size());
}

// ---- lifecycle ------------------------------------------------------------------------------------------------
void App::desktopInit() {
    platform::windowCreated(window_);
    platform::setDarkChrome(theme::isDark());   // Windows title bar; light themes get a light one
    theme::addListener([](theme::Scheme) { platform::setDarkChrome(theme::isDark()); });
    if (trayIcon && !applyTray()) trayIcon = false;
    platform::updateState(menuState());
}

void App::desktopFrame() {
    // Windows and Linux without a tray have nothing to refresh; the idle/screensaver edge is cheap.
    platform::setGameRunning(host.isLoaded() && !paused, keepAwake);
    if (trayIcon != platform::trayActive() && !applyTray()) trayIcon = false;   // Settings > Reset can flip it
    if (!platform::nativeMenuActive() && !platform::trayActive()) return;
    const uint64_t t = SDL_GetTicks();
    if (t - menuStateTick_ < 100) return;   // slot files are stat()ed: a few times a second is plenty
    menuStateTick_ = t;
    platform::updateState(menuState());
}

void App::desktopShutdown() { platform::shutdown(); }

bool App::applyTray() {
    if (!window_) return false;
    return platform::setTray(trayIcon, window_);
}

// ---- Settings > Interface > Desktop ------------------------------------------------------------------------------
void drawDesktopSettings(App& app) {
    ImGui::SeparatorText("Desktop");
#if defined(__APPLE__)
    const char* trayLabel = "Show an icon in the menu bar";
#elif defined(_WIN32)
    const char* trayLabel = "Show an icon in the notification area";
#else
    const char* trayLabel = "Show a system tray icon";
#endif
    if (ImGui::Checkbox(trayLabel, &app.trayIcon)) {
        if (!app.applyTray()) {
            app.trayIcon = false;
            app.toast("No system tray is available on this desktop", 4.0, 2);
        }
    }
    ui::hint("Pause, reset, recent games and quit from the tray, and a way back to the window. Off by default.");
    if (platform::nativeMenuActive()) {
        ImGui::Checkbox("Show the menu bar inside the window", &app.showInWindowMenu);
        ui::hint("Keep the menu bar at the top of the window as well as the one at the top of the screen. Both work "
                 "the same. Off by default; F1 flips it too.");
    }
    ImGui::Checkbox("Auto-hide the menu bar in fullscreen", &app.fullscreenAutoHideMenu);
    ui::hint("Move the pointer to the top edge to reveal it, or press F1 to pin it open. Off by default.");
    ImGui::Checkbox("Let windows move outside the main window", &app.windowsOutside);
    ui::hint("Settings, Achievements, Cheats and the tools can be dragged out of the main window and become windows of "
             "their own, for example on a second monitor. Drag one past the edge of the window, or back in to put it back. "
             "Off: they stay inside. Always inside while the window is fullscreen.");
    if (const char* why = app.viewportsNote(); *why) ui::coloredWrapped(ui::vec4(ui::pal().warning), why);
    ImGui::Checkbox("Keep the display awake while a game is running", &app.keepAwake);
    ui::hint("No screensaver or display sleep while a game is running and not paused. Controllers do not count as "
             "activity to the OS, so without this a long game with no keyboard use would blank the screen.");
}

}  // namespace vj
