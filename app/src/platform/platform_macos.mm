// macOS: the real menu bar, the Dock menu, the system recent-documents list, App Nap.  Compiled only
// on Apple platforms (-fobjc-arc, see app/CMakeLists.txt).
//
// How this coexists with SDL3 (read before changing):
//   * SDL installs ITS app delegate when the app has none, and that delegate is what turns Finder's
//     "open this file" into SDL_EVENT_DROP_FILE, Cmd+Q / Dock Quit into SDL_EVENT_QUIT, and tray
//     clicks into callbacks.  We never replace it.
//   * SDL builds a default menu bar at SDL_Init only when [NSApp mainMenu] is nil.  We replace the
//     main menu AFTER init (windowCreated -> installMenus -> renderMenus) with one built from the
//     model in commands.h, so there is exactly one description of the menus for every platform surface.
//   * The Dock menu is a method added at run time to the delegate's class (applicationDockMenu:),
//     guarded by respondsToSelector, so it also works with a shared system SDL (no category symbol to link).
//   * Menu items have an explicit target and autoenablesItems = NO: enabled/checked come from the model.
//     Only Cmd-modified key equivalents are bound; F5/F7/F8/Tab stay with the app's own hotkeys.
//   * A click posts a command event (platform.h) instead of calling the app: the action runs in the
//     main loop, never inside Cocoa's menu-tracking run loop.
#import <Cocoa/Cocoa.h>
#import <objc/runtime.h>

#include <cstdio>
#include <cstring>
#include <set>
#include <string>

#include "platform/platform.h"
#include "platform/platform_os.h"

using vj::platform::Cmd;
using vj::platform::MenuBar;
using vj::platform::MenuItem;

@interface VJMenuTarget : NSObject
- (void)invoke:(id)sender;
@end

@implementation VJMenuTarget
- (void)invoke:(id)sender {
    NSMenuItem* it = (NSMenuItem*)sender;
    NSNumber* arg = [it.representedObject isKindOfClass:[NSNumber class]] ? (NSNumber*)it.representedObject : @0;
    vj::platform::postCommand((Cmd)(int)it.tag, arg.intValue);
}
@end

namespace vj {
namespace platform {
namespace os {

namespace {

VJMenuTarget* g_target = nil;
NSMenu* g_dockMenu = nil;
bool g_active = false;
bool g_dockHooked = false;
id g_activity = nil;

NSString* ns(const std::string& s) { return [NSString stringWithUTF8String:s.c_str()]; }

// "cmd+shift+s" -> key + modifier mask.  Returns false for an unusable string.
bool parseKey(const std::string& spec, NSString** key, NSEventModifierFlags* mods) {
    NSEventModifierFlags m = 0;
    std::string rest = spec, last;
    size_t pos;
    while ((pos = rest.find('+')) != std::string::npos && pos + 1 < rest.size()) {
        std::string tok = rest.substr(0, pos);
        rest = rest.substr(pos + 1);
        if (tok == "cmd") m |= NSEventModifierFlagCommand;
        else if (tok == "shift") m |= NSEventModifierFlagShift;
        else if (tok == "alt") m |= NSEventModifierFlagOption;
        else if (tok == "ctrl") m |= NSEventModifierFlagControl;
        else return false;
    }
    last = rest;
    if (last.empty() || last.size() > 1) return false;   // letters, digits and punctuation only (no F-keys: see above)
    *key = ns(last);
    *mods = m;
    return true;
}

NSMenuItem* makeItem(const MenuItem& m);

void fillMenu(NSMenu* menu, const std::vector<MenuItem>& items) {
    menu.autoenablesItems = NO;
    for (const MenuItem& m : items) [menu addItem:makeItem(m)];
}

NSMenuItem* makeItem(const MenuItem& m) {
    if (m.separator) return [NSMenuItem separatorItem];
    NSString* title = ns(m.title);
    NSMenuItem* it = [[NSMenuItem alloc] initWithTitle:title action:(m.submenu ? nil : @selector(invoke:)) keyEquivalent:@""];
    it.enabled = m.enabled;
    if (m.submenu) {
        NSMenu* sub = [[NSMenu alloc] initWithTitle:title];
        fillMenu(sub, m.sub);
        it.submenu = sub;
        return it;
    }
    it.target = g_target;
    it.tag = (NSInteger)(int)m.cmd;
    it.representedObject = @(m.arg);
    it.state = m.checked ? NSControlStateValueOn : NSControlStateValueOff;
    if (!m.key.empty()) {
        NSString* k = nil;
        NSEventModifierFlags mods = 0;
        if (parseKey(m.key, &k, &mods)) {
            it.keyEquivalent = k;
            it.keyEquivalentModifierMask = mods;
        }
    }
    return it;
}

NSMenuItem* stdItem(NSString* title, SEL action, NSString* key, NSEventModifierFlags mods = NSEventModifierFlagCommand) {
    NSMenuItem* it = [[NSMenuItem alloc] initWithTitle:title action:action keyEquivalent:key];
    it.keyEquivalentModifierMask = mods;
    return it;
}

NSMenu* makeAppMenu(const std::vector<MenuItem>& items) {
    NSMenu* app = [[NSMenu alloc] initWithTitle:@""];
    app.autoenablesItems = NO;
    for (const MenuItem& m : items) {
        if (m.cmd == Cmd::Quit) {
            // Services / Hide / Hide Others / Show All sit between Settings and Quit, as in every Mac app.
            NSMenu* services = [[NSMenu alloc] initWithTitle:@"Services"];
            NSMenuItem* si = [[NSMenuItem alloc] initWithTitle:@"Services" action:nil keyEquivalent:@""];
            si.submenu = services;
            [app addItem:si];
            [NSApp setServicesMenu:services];
            [app addItem:[NSMenuItem separatorItem]];
            for (NSMenuItem* s in @[ stdItem(@"Hide Virtual Jaguar", @selector(hide:), @"h"),
                                     stdItem(@"Hide Others", @selector(hideOtherApplications:), @"h",
                                             NSEventModifierFlagCommand | NSEventModifierFlagOption),
                                     stdItem(@"Show All", @selector(unhideAllApplications:), @"", 0) ]) {
                s.target = NSApp;
                [app addItem:s];
            }
            [app addItem:[NSMenuItem separatorItem]];
        }
        [app addItem:makeItem(m)];
    }
    return app;
}

NSMenu* makeWindowMenu() {
    NSMenu* w = [[NSMenu alloc] initWithTitle:@"Window"];   // autoenables: Minimize / Zoom validate against the window
    [w addItem:stdItem(@"Minimize", @selector(performMiniaturize:), @"m")];
    [w addItem:stdItem(@"Zoom", @selector(performZoom:), @"", 0)];
    [w addItem:[NSMenuItem separatorItem]];
    [w addItem:stdItem(@"Bring All to Front", @selector(arrangeInFront:), @"", 0)];
    return w;
}

NSMenu* makeDockMenu(const std::vector<MenuItem>& dock) {
    NSMenu* m = [[NSMenu alloc] initWithTitle:@"Dock"];
    fillMenu(m, dock);
    return m;
}

void hookDockMenu() {
    if (g_dockHooked) return;
    id delegate = [NSApp delegate];
    if (!delegate) return;
    Class cls = [delegate class];
    SEL sel = @selector(applicationDockMenu:);
    if (![delegate respondsToSelector:sel]) {
        IMP imp = imp_implementationWithBlock(^NSMenu*(id /*self*/, NSApplication* /*app*/) { return g_dockMenu; });
        class_addMethod(cls, sel, imp, "@@:@");
    }
    g_dockHooked = true;
}

}  // namespace

void beforeWindow() {}
void windowCreated(SDL_Window*) {}
void setDarkChrome(bool) {}   // the title bar follows the system appearance

bool installMenus() {
    const char* drv = SDL_GetCurrentVideoDriver();
    if (!drv || std::strcmp(drv, "cocoa") != 0 || NSApp == nil) return false;
    if (!g_target) g_target = [[VJMenuTarget alloc] init];
    hookDockMenu();
    g_active = true;
    return true;
}

void renderMenus(const MenuBar& bar, const std::vector<MenuItem>& dock) {
    if (!g_active) return;
    @autoreleasepool {
        NSMenu* main = [[NSMenu alloc] initWithTitle:@"MainMenu"];
        main.autoenablesItems = NO;

        NSMenuItem* appItem = [[NSMenuItem alloc] initWithTitle:@"" action:nil keyEquivalent:@""];
        appItem.submenu = makeAppMenu(bar.app);
        [main addItem:appItem];

        NSMenu* windowMenu = nil;
        NSMenu* helpMenu = nil;
        for (const MenuItem& top : bar.menus) {
            NSMenuItem* ti = nil;
            if (top.title == "Window") {
                windowMenu = makeWindowMenu();
                ti = [[NSMenuItem alloc] initWithTitle:@"Window" action:nil keyEquivalent:@""];
                ti.submenu = windowMenu;
            } else {
                ti = makeItem(top);
                if (top.title == "Help") helpMenu = ti.submenu;
            }
            [main addItem:ti];
        }
        [NSApp setMainMenu:main];
        if (windowMenu) [NSApp setWindowsMenu:windowMenu];
        if (helpMenu) [NSApp setHelpMenu:helpMenu];
        g_dockMenu = makeDockMenu(dock);
    }
}

void noteRecent(const std::string& path) {
    if (path.empty()) return;
    NSURL* url = [NSURL fileURLWithPath:ns(path)];
    if (url) [[NSDocumentController sharedDocumentController] noteNewRecentDocumentURL:url];
}

void clearRecents() { [[NSDocumentController sharedDocumentController] clearRecentDocuments:nil]; }

void setActivity(bool running) {
    NSProcessInfo* pi = [NSProcessInfo processInfo];
    if (running && !g_activity) {
        // Keeps App Nap from slowing the frame loop when the window is covered or on another desktop.
        g_activity = [pi beginActivityWithOptions:(NSActivityUserInitiatedAllowingIdleSystemSleep | NSActivityLatencyCritical)
                                           reason:@"Emulating an Atari Jaguar game"];
    } else if (!running && g_activity) {
        [pi endActivity:g_activity];
        g_activity = nil;
    }
}

bool revealFile(const std::string& path) {
    NSURL* url = [NSURL fileURLWithPath:ns(path)];
    if (!url) return false;
    [[NSWorkspace sharedWorkspace] activateFileViewerSelectingURLs:@[ url ]];
    return true;
}

const char* fileManagerName() { return "Finder"; }

void shutdown() {
    setActivity(false);
    g_dockMenu = nil;
}

// ---- --dump-native-menu --------------------------------------------------------------------------------------
namespace {

std::string modsText(NSEventModifierFlags f) {
    std::string s;
    if (f & NSEventModifierFlagControl) s += "ctrl+";
    if (f & NSEventModifierFlagOption) s += "alt+";
    if (f & NSEventModifierFlagShift) s += "shift+";
    if (f & NSEventModifierFlagCommand) s += "cmd+";
    return s;
}

struct DumpStats {
    int items = 0, shortcuts = 0, problems = 0;
    std::set<std::string> keys;
};

void dumpMenu(NSMenu* menu, int depth, DumpStats& st) {
    for (NSMenuItem* it in menu.itemArray) {
        std::string pad((size_t)depth * 2, ' ');
        if (it.separatorItem) {
            std::printf("%s---\n", pad.c_str());
            continue;
        }
        st.items++;
        std::string key;
        if (it.keyEquivalent.length) {
            key = modsText(it.keyEquivalentModifierMask) + std::string(it.keyEquivalent.UTF8String);
            st.shortcuts++;
            // Mac shortcuts are per menu bar, and the system's Window / Hide items own cmd+m, cmd+h.
            if (!st.keys.insert(key).second) {
                std::printf("native-menu: FAIL duplicate shortcut %s\n", key.c_str());
                st.problems++;
            }
        }
        std::printf("%s%s%s%s%s%s\n", pad.c_str(), it.title.UTF8String, key.empty() ? "" : "  [", key.c_str(),
                    key.empty() ? "" : "]", it.enabled ? "" : "  (disabled)");
        if (it.submenu) {
            dumpMenu(it.submenu, depth + 1, st);
        } else if (it.target == g_target) {
            if (it.action != @selector(invoke:) || it.tag <= 0 || it.tag >= (NSInteger)Cmd::Count_) {
                std::printf("native-menu: FAIL '%s' has no command\n", it.title.UTF8String);
                st.problems++;
            }
        }
    }
}

}  // namespace

int dumpMenus() {
    // No GUI session (ssh without a login, a headless CI worker): SDL's Cocoa init would not return cleanly.
    CFDictionaryRef session = CGSessionCopyCurrentDictionary();
    if (!session) {
        std::printf("native-menu: SKIP no window-server session\n");
        return 77;
    }
    CFRelease(session);
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::printf("native-menu: SKIP SDL video init failed: %s\n", SDL_GetError());
        return 77;
    }
    int rc = 0;
    if (!installMenus()) {
        std::printf("native-menu: SKIP video driver is %s, not cocoa\n", SDL_GetCurrentVideoDriver());
        rc = 77;
    } else {
        vj::platform::MenuState s;
        s.gameLoaded = true;
        s.canLoadState = true;
        s.usedSlots = 0x5;
        s.recents = {"/games/Tempest 2000.j64", "/games/Doom.jag", "/other/Doom.jag.bak"};
        s.disc.available = true;
        s.disc.count = 2;
        s.disc.labels = {"Disc 1", "Disc 2"};
        s.achAvailable = true;
        s.netplayConfigured = true;
        s.fileManager = "Finder";
        renderMenus(buildMenuBar(s), buildDockMenu(s));
        NSMenu* main = [NSApp mainMenu];
        DumpStats st;
        std::printf("native-menu: main menu\n");
        dumpMenu(main, 1, st);
        std::printf("native-menu: dock menu\n");
        dumpMenu(g_dockMenu, 1, st);
        if (![[NSApp delegate] respondsToSelector:@selector(applicationDockMenu:)]) {
            std::printf("native-menu: FAIL the app delegate does not answer applicationDockMenu:\n");
            st.problems++;
        }
        if (NSApp.windowsMenu == nil || NSApp.helpMenu == nil || NSApp.servicesMenu == nil) {
            std::printf("native-menu: FAIL Window / Help / Services menu not registered\n");
            st.problems++;
        }
        std::printf("native-menu: %d items, %d shortcuts, %d problems\n", st.items, st.shortcuts, st.problems);
        rc = st.problems ? 1 : 0;
    }
    SDL_Quit();
    return rc;
}

}  // namespace os
}  // namespace platform
}  // namespace vj
