#import <AppKit/AppKit.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

#include "performance-client.h"

static char keeper_socket[PATH_MAX];

static BOOL trustedBundle(NSString *identifier) {
    return [identifier isEqualToString:@"com.openai.codex"] ||
           [identifier isEqualToString:@"com.qoder.ide"];
}

static void registerApplication(NSRunningApplication *application) {
    if (application.terminated || application.processIdentifier <= 1) return;
    if (trustedBundle(application.bundleIdentifier)) {
        (void)performance_register_pid(keeper_socket, application.processIdentifier);
    }
}

@interface PerformanceApplicationObserver : NSObject
@end

@implementation PerformanceApplicationObserver
- (void)applicationLaunched:(NSNotification *)notification {
    NSRunningApplication *application =
        notification.userInfo[NSWorkspaceApplicationKey];
    if (application) registerApplication(application);
}
@end

int main(int argc, const char *argv[]) {
    @autoreleasepool {
        if (argc == 3 && strcmp(argv[1], "--socket") == 0) {
            if (snprintf(keeper_socket, sizeof(keeper_socket), "%s", argv[2]) >=
                (int)sizeof(keeper_socket)) return 64;
        } else if (argc == 1 && performance_default_socket_path(keeper_socket) == 0) {
        } else {
            fprintf(stderr, "usage: codex-performance-app-watcher [--socket path]\n");
            return 64;
        }

        NSWorkspace *workspace = NSWorkspace.sharedWorkspace;
        for (NSRunningApplication *application in workspace.runningApplications) {
            registerApplication(application);
        }
        PerformanceApplicationObserver *observer =
            [[PerformanceApplicationObserver alloc] init];
        [workspace.notificationCenter addObserver:observer
                                         selector:@selector(applicationLaunched:)
                                             name:NSWorkspaceDidLaunchApplicationNotification
                                           object:nil];
        [NSRunLoop.currentRunLoop run];
        (void)observer;
    }
    return 0;
}
