// Exercise the real bookmark bookkeeping with a URL double; no sandbox grants or panels.
#include "../src/platform/file_access_macos.mm"
#include <cstdlib>

@interface VJBookmarkURL : NSObject
@property(nonatomic, copy) NSString* path;
@property(nonatomic) int starts;
@property(nonatomic) int stops;
@property(nonatomic) int writes;
@property(nonatomic) BOOL failBookmark;
- (BOOL)startAccessingSecurityScopedResource;
- (void)stopAccessingSecurityScopedResource;
- (NSData*)bookmarkDataWithOptions:(NSURLBookmarkCreationOptions)options
    includingResourceValuesForKeys:(NSArray*)keys relativeToURL:(NSURL*)base error:(NSError**)error;
@end
@implementation VJBookmarkURL
- (BOOL)startAccessingSecurityScopedResource { self.starts++; return YES; }
- (void)stopAccessingSecurityScopedResource { self.stops++; }
- (NSData*)bookmarkDataWithOptions:(NSURLBookmarkCreationOptions)options
    includingResourceValuesForKeys:(NSArray*)keys relativeToURL:(NSURL*)base error:(NSError**)error {
    self.writes++;
    return self.failBookmark ? nil : [@"bookmark fixture" dataUsingEncoding:NSUTF8StringEncoding];
}
@end

static void check(bool condition, const char* message) {
    if (!condition) { std::fprintf(stderr, "FAIL %s\n", message); std::exit(1); }
}
int main() {
    @autoreleasepool {
        using namespace vj::files;
        // This CLI's defaults domain is separate from the installed app's bundle domain.
        NSUserDefaults* defaults = [NSUserDefaults standardUserDefaults];
        id previous = [defaults objectForKey:kBookmarks];
        [defaults removeObjectForKey:kBookmarks];
        VJBookmarkURL* url = [VJBookmarkURL new];
        url.path = @"/fixture/library";
        check(retainURL((NSURL*)url), "initial grant");
        check(url.starts == 1 && url.writes == 1, "initial scope and bookmark");
        check(retainURL((NSURL*)url), "repeated grant");
        check(url.starts == 1 && url.writes == 1, "no duplicate scope/bookmark");
        forget(url.path.UTF8String);
        check(![defaults dictionaryForKey:kBookmarks][url.path], "forget removes saved grant");
        check(url.stops == 0, "forget preserves scope for workers");
        url.failBookmark = YES;
        check(!retainURL((NSURL*)url), "failed re-add reports failure");
        check(url.starts == 1 && url.stops == 0, "failed re-add preserves active scope");
        url.failBookmark = NO;
        check(retainURL((NSURL*)url), "re-add succeeds");
        check([defaults dictionaryForKey:kBookmarks][url.path] != nil, "re-add persists next-launch grant");
        check(url.starts == 1 && url.stops == 0, "re-add does not restart existing scope");
        shutdown();
        check(url.stops == 1, "shutdown balances the original scope once");
        if (previous) [defaults setObject:previous forKey:kBookmarks];
        else [defaults removeObjectForKey:kBookmarks];
        std::puts("PASS bookmark remove/re-add persistence and scope lifetime");
    }
}
