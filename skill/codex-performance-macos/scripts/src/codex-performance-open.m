#import <AppKit/AppKit.h>
#include <limits.h>
#include <pthread/qos.h>
#include <sys/resource.h>

#include "performance-client.h"

#define PRIO_DARWIN_ROLE 6
#define PRIO_DARWIN_ROLE_USER_INIT 0x7

static BOOL nameMatches(NSRunningApplication *application, NSString *name) {
    if ([application.localizedName caseInsensitiveCompare:name] == NSOrderedSame) return YES;
    NSString *bundleName = application.bundleURL.lastPathComponent.stringByDeletingPathExtension;
    if ([bundleName caseInsensitiveCompare:name] == NSOrderedSame) return YES;
    return [application.executableURL.lastPathComponent caseInsensitiveCompare:name] ==
           NSOrderedSame;
}

static BOOL optionConsumesValue(NSString *argument) {
    return [@[@"-a", @"-b", @"-s", @"--arch", @"-i", @"-o", @"--stderr"]
        containsObject:argument];
}

int main(int argc, const char *argv[]) {
    @autoreleasepool {
        (void)setpriority(PRIO_DARWIN_ROLE, (id_t)getpid(), PRIO_DARWIN_ROLE_USER_INIT);
        (void)pthread_set_qos_class_self_np(QOS_CLASS_USER_INITIATED, 0);

        NSString *applicationName = nil;
        NSString *bundleIdentifier = nil;
        NSMutableArray<NSString *> *arguments = [NSMutableArray array];
        NSMutableArray<NSString *> *inputs = [NSMutableArray array];
        BOOL applicationArguments = NO;
        for (int index = 1; index < argc; ++index) {
            NSString *argument = [NSString stringWithUTF8String:argv[index]];
            [arguments addObject:argument];
            if (applicationArguments) continue;
            if ([argument isEqualToString:@"--args"]) {
                applicationArguments = YES;
            } else if ([argument isEqualToString:@"-a"] && index + 1 < argc) {
                applicationName = [NSString stringWithUTF8String:argv[index + 1]];
                [arguments addObject:applicationName];
                ++index;
            } else if ([argument isEqualToString:@"-b"] && index + 1 < argc) {
                bundleIdentifier = [NSString stringWithUTF8String:argv[index + 1]];
                [arguments addObject:bundleIdentifier];
                ++index;
            } else if ([argument isEqualToString:@"-u"] && index + 1 < argc) {
                NSString *input = [NSString stringWithUTF8String:argv[index + 1]];
                [arguments addObject:input];
                [inputs addObject:input];
                ++index;
            } else if (optionConsumesValue(argument) && index + 1 < argc) {
                [arguments addObject:[NSString stringWithUTF8String:argv[index + 1]]];
                ++index;
            } else if (![argument hasPrefix:@"-"]) {
                [inputs addObject:argument];
            }
        }

        NSWorkspace *workspace = NSWorkspace.sharedWorkspace;
        NSMutableSet<NSURL *> *targetBundles = [NSMutableSet set];
        NSURL *workingDirectory = [NSURL fileURLWithPath:NSFileManager.defaultManager.currentDirectoryPath
                                             isDirectory:YES];
        for (NSString *input in inputs) {
            NSURL *inputURL = [NSURL URLWithString:input];
            if (!inputURL.scheme) {
                NSString *expanded = input.stringByExpandingTildeInPath;
                inputURL = [NSURL fileURLWithPath:expanded relativeToURL:workingDirectory]
                               .URLByStandardizingPath;
            }
            NSURL *applicationURL = [workspace URLForApplicationToOpenURL:inputURL];
            if (applicationURL) [targetBundles addObject:applicationURL.URLByStandardizingPath];
        }

        NSTask *task = [[NSTask alloc] init];
        task.executableURL = [NSURL fileURLWithPath:@"/usr/bin/open"];
        task.arguments = arguments;
        NSError *error = nil;
        if (![task launchAndReturnError:&error]) {
            fprintf(stderr, "codex-performance-open: %s\n",
                    error.localizedDescription.UTF8String);
            return 126;
        }
        [task waitUntilExit];
        if (task.terminationStatus != 0) return task.terminationStatus;

        char socketPath[PATH_MAX];
        if (performance_default_socket_path(socketPath) != 0) return 0;
        for (int attempt = 0; attempt < 20; ++attempt) {
            BOOL matched = NO;
            for (NSRunningApplication *application in workspace.runningApplications) {
                NSURL *bundleURL = application.bundleURL.URLByStandardizingPath;
                if ((bundleIdentifier &&
                     [application.bundleIdentifier isEqualToString:bundleIdentifier]) ||
                    (applicationName && nameMatches(application, applicationName)) ||
                    (bundleURL && [targetBundles containsObject:bundleURL])) {
                    (void)performance_register_pid(socketPath, application.processIdentifier);
                    matched = YES;
                }
            }
            if (matched || (!applicationName && !bundleIdentifier && targetBundles.count == 0)) {
                break;
            }
            [NSThread sleepForTimeInterval:0.1];
        }
    }
    return 0;
}
