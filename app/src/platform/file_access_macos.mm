// macOS Store only. Keep access alive while library workers/core streams use plain paths.
// Restored scopes are balanced at shutdown, after those workers and streams have stopped.
#import <Cocoa/Cocoa.h>
#include "platform/file_access.h"
#include "config.h"
#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

namespace vj { namespace files {
namespace {
NSString* const kBookmarks = @"VJStoreFileBookmarksV1";
NSMutableDictionary<NSString*, NSURL*>* active;
NSMutableDictionary<NSString*, NSData*>* bookmarks;
bool restored = false;

void init() {
    if (!active) active = [NSMutableDictionary dictionary];
    if (!bookmarks) {
        NSDictionary* saved = [[NSUserDefaults standardUserDefaults] dictionaryForKey:kBookmarks];
        bookmarks = saved ? [saved mutableCopy] : [NSMutableDictionary dictionary];
    }
}
void persist() { [[NSUserDefaults standardUserDefaults] setObject:bookmarks forKey:kBookmarks]; }
void report(NSString* message) { std::fprintf(stderr, "[sandbox] %s\n", message.UTF8String); }

bool retainURL(NSURL* url) {
    init();
    NSString* path = url.path;
    if (!path) return false;
    if (active[path]) return true; // never start the same scope twice
    BOOL started = [url startAccessingSecurityScopedResource];
    // Open/save panels already grant process access. A false start isn't a denial for
    // such a URL (or a file in our own container); bookmark creation still may succeed.
    NSError* error = nil;
    NSData* data = [url bookmarkDataWithOptions:NSURLBookmarkCreationWithSecurityScope
                 includingResourceValuesForKeys:nil relativeToURL:nil error:&error];
    if (!data) {
        if (started) [url stopAccessingSecurityScopedResource];
        report(error.localizedDescription ?: @"Could not retain file access.");
        return false;
    }
    if (started) active[path] = url;
    bookmarks[path] = data;
    persist();
    return true;
}

// CUE/M3U and shader presets reference adjacent files. A file grant doesn't grant
// its parent: ask the user to select the containing collection/preset folder.
bool relatedFiles(NSURL* url) {
    NSString* ext = url.pathExtension.lowercaseString;
    if (![@[@"cue", @"m3u", @"slangp"] containsObject:ext]) return true;
    for (NSString* path in active) {
        NSNumber* directory = nil;
        [active[path] getResourceValue:&directory forKey:NSURLIsDirectoryKey error:nil];
        if (directory.boolValue && [url.path hasPrefix:[path stringByAppendingString:@"/"]]) return true;
    }
    NSOpenPanel* panel = [NSOpenPanel openPanel];
    panel.canChooseFiles = NO;
    panel.canChooseDirectories = YES;
    panel.directoryURL = url.URLByDeletingLastPathComponent;
    panel.message = @"Select the folder containing this file and its referenced files. Virtual Jaguar remembers access for future launches.";
    panel.prompt = @"Grant Folder Access";
    if ([panel runModal] != NSModalResponseOK) return false;
    NSURL* root = panel.URL;
    if (![url.path hasPrefix:[root.path stringByAppendingString:@"/"]]) {
        report(@"The selected folder does not contain the file. Select its containing folder and try again.");
        return false;
    }
    return retainURL(root);
}

void deliver(SDL_DialogFileCallback cb, void* ud, NSArray<NSURL*>* urls, bool related) {
    std::vector<std::string> paths;
    for (NSURL* url in urls) {
        if (!retainURL(url) || (related && !relatedFiles(url))) {
            SDL_SetError("File access could not be saved; select the containing folder and try again.");
            cb(ud, nullptr, -1);
            return;
        }
        paths.emplace_back(url.path.UTF8String);
    }
    std::vector<const char*> result;
    for (const auto& p : paths) result.push_back(p.c_str());
    result.push_back(nullptr);
    cb(ud, result.data(), 0);
}
void location(NSSavePanel* panel, const char* path, bool save) {
    if (!path || !*path) return;
    NSString* text = [NSString stringWithUTF8String:path];
    BOOL directory = NO;
    if ([[NSFileManager defaultManager] fileExistsAtPath:text isDirectory:&directory] && directory)
        panel.directoryURL = [NSURL fileURLWithPath:text isDirectory:YES];
    else {
        panel.directoryURL = [NSURL fileURLWithPath:text.stringByDeletingLastPathComponent isDirectory:YES];
        if (save) panel.nameFieldStringValue = text.lastPathComponent;
    }
}
void show(NSSavePanel* panel, SDL_Window* parent, void (^complete)(NSModalResponse)) {
    NSWindow* window = parent ? (__bridge NSWindow*)SDL_GetPointerProperty(SDL_GetWindowProperties(parent),
                     SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, nullptr) : nil;
    if (window) [panel beginSheetModalForWindow:window completionHandler:complete];
    else [panel beginWithCompletionHandler:complete];
}
void filters(NSSavePanel* panel, const SDL_DialogFileFilter* f, int count) {
    NSMutableArray<NSString*>* extensions = [NSMutableArray array];
    for (int i = 0; i < count; ++i) {
        NSString* pattern = [NSString stringWithUTF8String:f[i].pattern];
        if ([pattern isEqualToString:@"*"]) return; // the original picker offers All files
        [extensions addObjectsFromArray:[pattern componentsSeparatedByString:@";"]];
    }
    if (extensions.count) panel.allowedFileTypes = extensions;
}
} // namespace

void restore(Config& cfg) {
    if (restored) return;
    restored = true;
    init();
    std::vector<std::pair<std::string, std::string>> moved;
    for (NSString* oldPath in [bookmarks.allKeys copy]) {
        id data = bookmarks[oldPath];
        if (![data isKindOfClass:[NSData class]]) continue;
        BOOL stale = NO;
        NSError* error = nil;
        NSURL* url = [NSURL URLByResolvingBookmarkData:data
                    options:NSURLBookmarkResolutionWithSecurityScope | NSURLBookmarkResolutionWithoutUI
                    relativeToURL:nil bookmarkDataIsStale:&stale error:&error];
        if (url && active[url.path]) continue;
        if (!url || ![url startAccessingSecurityScopedResource]) {
            report(@"A saved location is unavailable. Reconnect its volume or choose the folder again in Library.");
            continue; // keep its bookmark: removable/network volumes may return later
        }
        active[url.path] = url;
        if (![oldPath isEqualToString:url.path]) moved.emplace_back(oldPath.UTF8String, url.path.UTF8String);
        if (stale || ![oldPath isEqualToString:url.path]) {
            NSData* fresh = [url bookmarkDataWithOptions:NSURLBookmarkCreationWithSecurityScope
                          includingResourceValuesForKeys:nil relativeToURL:nil error:&error];
            if (fresh) { [bookmarks removeObjectForKey:oldPath]; bookmarks[url.path] = fresh; }
        }
    }
    // Longest match first, so an individually selected file overrides a containing folder.
    std::sort(moved.begin(), moved.end(), [](const auto& a, const auto& b) { return a.first.size() > b.first.size(); });
    for (const auto& kv : cfg.withPrefix("")) {
        for (const auto& move : moved) {
            if (kv.second == move.first || (kv.second.size() > move.first.size() &&
                kv.second.compare(0, move.first.size(), move.first) == 0 && kv.second[move.first.size()] == '/')) {
                cfg.set(kv.first, move.second + kv.second.substr(move.first.size()));
                break;
            }
        }
    }
    persist();
    cfg.save();
}
void shutdown() {
    for (NSString* path in active) [active[path] stopAccessingSecurityScopedResource];
    active = nil;
    bookmarks = nil;
    restored = false;
}
void remember(const char* path) {
    if (!path || !*path) return;
    NSURL* url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:path]];
    if (retainURL(url)) relatedFiles(url);
}
void forget(const char* path) {
    init();
    NSString* key = path ? [NSString stringWithUTF8String:path] : nil;
    if (!key) return;
    // Workers may still read a removed library until their rescan completes. Keep its
    // current scope until shutdown, but stop restoring it on subsequent launches.
    [bookmarks removeObjectForKey:key];
    persist();
}
void openFile(SDL_DialogFileCallback cb, void* ud, SDL_Window* parent, const SDL_DialogFileFilter* f,
              int count, const char* path, bool multiple) {
    NSOpenPanel* panel = [NSOpenPanel openPanel];
    panel.canChooseFiles = YES;
    panel.canChooseDirectories = NO;
    panel.allowsMultipleSelection = multiple;
    location(panel, path, false);
    filters(panel, f, count);
    show(panel, parent, ^(NSModalResponse result) {
        deliver(cb, ud, result == NSModalResponseOK ? panel.URLs : @[], true);
    });
}
void openFolder(SDL_DialogFileCallback cb, void* ud, SDL_Window* parent, const char* path, bool multiple) {
    NSOpenPanel* panel = [NSOpenPanel openPanel];
    panel.canChooseFiles = NO;
    panel.canChooseDirectories = YES;
    panel.allowsMultipleSelection = multiple;
    location(panel, path, false);
    show(panel, parent, ^(NSModalResponse result) {
        deliver(cb, ud, result == NSModalResponseOK ? panel.URLs : @[], false);
    });
}
void saveFile(SDL_DialogFileCallback cb, void* ud, SDL_Window* parent, const SDL_DialogFileFilter* f,
              int count, const char* path) {
    NSSavePanel* panel = [NSSavePanel savePanel];
    location(panel, path, true);
    filters(panel, f, count);
    show(panel, parent, ^(NSModalResponse result) {
        // A new export may not exist yet, so no bookmark is needed: the Save panel
        // grants writing it for this session, and theme export doesn't retain its path.
        const char* choice[] = {result == NSModalResponseOK ? panel.URL.path.UTF8String : nullptr, nullptr};
        cb(ud, choice, 0);
    });
}
} } // namespace vj::files
