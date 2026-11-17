#import <Cocoa/Cocoa.h>
#import <Sparkle/Sparkle.h>

#include "sparkle_macos.h"

@interface VJSparkleDelegate : NSObject <SPUUpdaterDelegate>
@property(nonatomic) BOOL gameLoaded;
@property(nonatomic, copy) void (^pendingInstall)(void);
@end

@implementation VJSparkleDelegate
- (BOOL)updater:(SPUUpdater*)updater
    mayPerformUpdateCheck:(SPUUpdateCheck)check
                    error:(NSError**)error {
    if (!self.gameLoaded) return YES;
    if (error) {
        *error = [NSError errorWithDomain:@"io.github.libretro.VirtualJaguar.Sparkle"
                                    code:1
                                userInfo:@{NSLocalizedDescriptionKey:
                                    @"Close the current game before checking for updates."}];
    }
    return NO;
}

- (BOOL)updater:(SPUUpdater*)updater
    shouldPostponeRelaunchForUpdate:(SUAppcastItem*)item
               untilInvokingBlock:(void (^)(void))install {
    if (!self.gameLoaded) return NO;
    self.pendingInstall = install;
    return YES;
}
@end

namespace vj {
namespace sparkle {
namespace {
VJSparkleDelegate* delegate;
SPUStandardUpdaterController* controller;
}

bool start() {
    if (controller) return true;
    NSBundle* bundle = NSBundle.mainBundle;
    NSString* feed = [bundle objectForInfoDictionaryKey:@"SUFeedURL"];
    if (![feed isKindOfClass:NSString.class] ||
        ![[NSURL URLWithString:feed].scheme isEqualToString:@"https"] ||
        ![bundle objectForInfoDictionaryKey:@"SUPublicEDKey"]) return false;
    delegate = [[VJSparkleDelegate alloc] init];
    controller = [[SPUStandardUpdaterController alloc] initWithStartingUpdater:NO
                                                              updaterDelegate:delegate
                                                           userDriverDelegate:nil];
    NSError* error = nil;
    if (![controller.updater startUpdater:&error]) {
        NSLog(@"Sparkle could not start: %@", error.localizedDescription);
        controller = nil;
        delegate = nil;
        return false;
    }
    return true;
}

void setGameLoaded(bool loaded) {
    delegate.gameLoaded = loaded;
    if (!loaded && delegate.pendingInstall) {
        auto install = delegate.pendingInstall;
        delegate.pendingInstall = nil;
        install();
    }
}

void check() {
    if (controller.updater.canCheckForUpdates) [controller checkForUpdates:nil];
}

bool backgroundChecks() { return controller.updater.automaticallyChecksForUpdates; }

void setBackgroundChecks(bool enabled) {
    controller.updater.automaticallyChecksForUpdates = enabled;
}
}
}  // namespace vj
