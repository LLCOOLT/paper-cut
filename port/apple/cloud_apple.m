/* Save sync through iCloud Drive (port/include/port/cloud.h; Apple only).
 *
 * The memory card is a folder of .gci files (Aurora's GCI folder card,
 * <user>/<region>/Card A). The same layout is kept in a folder in iCloud
 * Drive: on the Mac, iCloud Drive's own TTYD folder; on iOS, a folder the
 * player picks once (the document picker grants lasting access to it through
 * a security-scoped bookmark, so no iCloud entitlement is needed). Each file
 * goes the way of its newer copy: from the cloud when the game starts (before
 * the card is mounted), to the cloud after the game writes one and when the
 * app goes to the background. A copy takes its source's modification time, so
 * a synced pair is equal and nothing goes back and forth. A local file about to
 * be replaced is kept in <user>/Backups first; nothing is ever deleted.
 *
 * Built on its own (port/apple/apple.cmake), without the game's prefix header:
 * Objective-C's BOOL is not the Dolphin SDK's. */
#import <Foundation/Foundation.h>
#include <TargetConditionals.h>
#if TARGET_OS_IPHONE
#import <UIKit/UIKit.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "port/cloud.h"
#include "port/tr.h"
#include <stdarg.h>

static NSString* const kBookmarkKey = @"cloud_folder_bookmark";
static NSURL* sFolder;      /* the cloud folder (TTYD), when there is one */
static BOOL sAccessing;     /* iOS: security-scoped access started */
static char sStatus[160];

/* english: a format, shown in the menu language (port/tr.h: the format looked up, then filled in) */
static void setStatus(const char* english, ...) {
    va_list args;
    va_start(args, english);
    vsnprintf(sStatus, sizeof(sStatus), portTr(english), args);
    va_end(args);
    fprintf(stderr, "[cloud] %s\n", sStatus);
}

const char* portCloudStatus(void) {
    return sStatus;
}

/* The cloud folder: on the Mac iCloud Drive/TTYD (made if iCloud Drive is on);
 * on iOS the picked folder (its bookmark resolved, access started). */
static NSURL* folder(void) {
    if (sFolder != nil) {
        return sFolder;
    }
    {
        /* $TTYD_CLOUD_DIR (tests): this folder instead of iCloud Drive's */
        const char* dir = getenv("TTYD_CLOUD_DIR");
        if (dir != NULL && dir[0] != '\0') {
            sFolder = [NSURL fileURLWithPath:[NSString stringWithUTF8String:dir] isDirectory:YES];
            return sFolder;
        }
    }
#if TARGET_OS_IPHONE
    NSData* bookmark = [NSUserDefaults.standardUserDefaults dataForKey:kBookmarkKey];
    if (bookmark == nil) {
        return nil;
    }
    BOOL stale = NO;
    NSError* error = nil;
    NSURL* url = [NSURL URLByResolvingBookmarkData:bookmark options:0 relativeToURL:nil
                               bookmarkDataIsStale:&stale error:&error];
    if (url == nil) {
        setStatus("The iCloud Drive folder can't be opened (%s): choose it again",
                  error.localizedDescription.UTF8String);
        return nil;
    }
    sAccessing = [url startAccessingSecurityScopedResource];
    if (stale) {
        NSData* fresh = [url bookmarkDataWithOptions:0 includingResourceValuesForKeys:nil relativeToURL:nil error:nil];
        if (fresh != nil) {
            [NSUserDefaults.standardUserDefaults setObject:fresh forKey:kBookmarkKey];
        }
    }
    sFolder = url;
#else
    NSURL* drive = [NSFileManager.defaultManager.homeDirectoryForCurrentUser
        URLByAppendingPathComponent:@"Library/Mobile Documents/com~apple~CloudDocs" isDirectory:YES];
    BOOL isDir = NO;
    if (![NSFileManager.defaultManager fileExistsAtPath:drive.path isDirectory:&isDir] || !isDir) {
        setStatus("iCloud Drive is off on this Mac (System Settings > Apple Account > iCloud)");
        return nil;
    }
    sFolder = [drive URLByAppendingPathComponent:@"TTYD" isDirectory:YES];
    [NSFileManager.defaultManager createDirectoryAtURL:sFolder withIntermediateDirectories:YES attributes:nil
                                                 error:nil];
#endif
    return sFolder;
}

int portCloudAvailable(void) {
    return folder() != nil;
}

const char* portCloudFolderName(void) {
    static char name[256];
    NSURL* url = folder();
    snprintf(name, sizeof(name), "%s", url != nil ? url.lastPathComponent.UTF8String : "");
    return name;
}

static NSDate* modified(NSURL* url) {
    NSDate* date = nil;
    [url getResourceValue:&date forKey:NSURLContentModificationDateKey error:nil];
    return date;
}

/* The .gci files of a card folder; in the cloud, files not downloaded yet
 * (".name.icloud") are asked for and listed by their real name. */
static NSArray<NSString*>* gciNames(NSURL* dir, BOOL cloud) {
    NSMutableArray<NSString*>* names = [NSMutableArray array];
    NSArray<NSString*>* entries = [NSFileManager.defaultManager contentsOfDirectoryAtPath:dir.path error:nil];
    for (NSString* entry in entries) {
        NSString* name = entry;
        if (cloud && [name hasPrefix:@"."] && [name hasSuffix:@".icloud"]) {
            name = [[name substringFromIndex:1] stringByDeletingPathExtension];
            [NSFileManager.defaultManager startDownloadingUbiquitousItemAtURL:[dir URLByAppendingPathComponent:name]
                                                                        error:nil];
        }
        if ([name.pathExtension caseInsensitiveCompare:@"gci"] == NSOrderedSame && ![names containsObject:name]) {
            [names addObject:name];
        }
    }
    return names;
}

/* src over dst (coordinated with iCloud), dst taking src's modification time */
static BOOL copyFile(NSURL* src, NSURL* dst, BOOL toCloud) {
    __block BOOL ok = NO;
    NSError* error = nil;
    NSFileCoordinator* fc = [[NSFileCoordinator alloc] initWithFilePresenter:nil];
    NSURL* coordinated = toCloud ? dst : src;
    void (^copy)(NSURL*) = ^(NSURL* url) {
        NSURL* from = toCloud ? src : url;
        NSURL* to = toCloud ? url : dst;
        NSData* data = [NSData dataWithContentsOfURL:from];
        if (data == nil) {
            return;
        }
        if ([data writeToURL:to atomically:YES]) {
            NSDate* date = modified(from);
            if (date != nil) {
                [to setResourceValue:date forKey:NSURLContentModificationDateKey error:nil];
            }
            ok = YES;
        }
    };
    if (toCloud) {
        [fc coordinateWritingItemAtURL:coordinated options:NSFileCoordinatorWritingForReplacing error:&error
                            byAccessor:copy];
    } else {
        [fc coordinateReadingItemAtURL:coordinated options:0 error:&error byAccessor:copy];
    }
    return ok;
}

static void backup(NSURL* local, const char* userPath) {
    NSString* dirPath = [[NSString stringWithUTF8String:userPath] stringByAppendingPathComponent:@"Backups"];
    [NSFileManager.defaultManager createDirectoryAtPath:dirPath withIntermediateDirectories:YES attributes:nil
                                                  error:nil];
    NSDateFormatter* f = [[NSDateFormatter alloc] init];
    f.dateFormat = @"yyyy-MM-dd HH.mm.ss";
    NSString* name = [NSString stringWithFormat:@"%@ (%@)", local.lastPathComponent, [f stringFromDate:[NSDate date]]];
    [NSFileManager.defaultManager copyItemAtURL:local toURL:[NSURL fileURLWithPath:[dirPath stringByAppendingPathComponent:name]]
                                          error:nil];
}

static int syncNow(const char* userPath, const char* region, int pull) {
    @autoreleasepool {
        NSURL* cloudRoot = folder();
        if (cloudRoot == nil) {
            return 0;
        }
        NSString* sub = [NSString stringWithFormat:@"%s/Card A", region];
        NSURL* cloud = [cloudRoot URLByAppendingPathComponent:sub isDirectory:YES];
        NSURL* local = [[NSURL fileURLWithPath:[NSString stringWithUTF8String:userPath] isDirectory:YES]
            URLByAppendingPathComponent:sub isDirectory:YES];
        [NSFileManager.defaultManager createDirectoryAtURL:cloud withIntermediateDirectories:YES attributes:nil error:nil];
        [NSFileManager.defaultManager createDirectoryAtURL:local withIntermediateDirectories:YES attributes:nil error:nil];
        int copied = 0;
        if (pull) {
            NSArray<NSString*>* names = gciNames(cloud, YES);
            /* a moment for files iCloud is still downloading */
            for (int wait = 0; wait < 50; wait++) {
                BOOL pending = NO;
                for (NSString* name in names) {
                    if (![NSFileManager.defaultManager fileExistsAtPath:[cloud URLByAppendingPathComponent:name].path]) {
                        pending = YES;
                    }
                }
                if (!pending) {
                    break;
                }
                [NSThread sleepForTimeInterval:0.1];
            }
            for (NSString* name in names) {
                NSURL* c = [cloud URLByAppendingPathComponent:name];
                NSURL* l = [local URLByAppendingPathComponent:name];
                NSDate* cd = modified(c);
                NSDate* ld = modified(l);
                if (cd == nil || (ld != nil && [cd timeIntervalSinceDate:ld] < 1.0)) {
                    continue;
                }
                if (ld != nil) {
                    backup(l, userPath);
                }
                if (copyFile(c, l, NO)) {
                    copied++;
                }
            }
            if (copied) {
                setStatus("%d save file(s) brought from iCloud Drive", copied);
            } else {
                setStatus("Saves are the same as in iCloud Drive");
            }
        } else {
            for (NSString* name in gciNames(local, NO)) {
                NSURL* c = [cloud URLByAppendingPathComponent:name];
                NSURL* l = [local URLByAppendingPathComponent:name];
                NSDate* cd = modified(c);
                NSDate* ld = modified(l);
                if (ld == nil || (cd != nil && [ld timeIntervalSinceDate:cd] < 1.0)) {
                    continue;
                }
                if (copyFile(l, c, YES)) {
                    copied++;
                }
            }
            if (copied) {
                setStatus("%d save file(s) copied to iCloud Drive", copied);
            }
        }
        return copied;
    }
}

static dispatch_queue_t sQueue; /* pushes, one at a time */

int portCloudSync(const char* userPath, const char* region, int pull) {
    if (pull) {
        return syncNow(userPath, region, 1); /* now: the card is mounted next */
    }
    /* a push in the background: iCloud's coordination may wait */
    if (sQueue == nil) {
        sQueue = dispatch_queue_create("ttyd.cloud", DISPATCH_QUEUE_SERIAL);
    }
    NSString* user = [NSString stringWithUTF8String:userPath];
    NSString* reg = [NSString stringWithUTF8String:region];
    dispatch_async(sQueue, ^{
        syncNow(user.UTF8String, reg.UTF8String, 0);
    });
    return 0;
}

/* Waits for the pushes queued so far (going to the background, quitting:
 * the app may be suspended or gone next). */
void portCloudFlush(void) {
    if (sQueue != nil) {
        dispatch_sync(sQueue, ^{
        });
    }
}

#if TARGET_OS_IPHONE
/* The folder picker (iOS): the chosen folder's bookmark is kept. */
@interface TTYDCloudPicker : NSObject <UIDocumentPickerDelegate>
@property(nonatomic) int state; /* 1 open, 2 chosen, 3 cancelled */
@end

@implementation TTYDCloudPicker
- (void)documentPicker:(UIDocumentPickerViewController*)controller didPickDocumentsAtURLs:(NSArray<NSURL*>*)urls {
    NSURL* url = urls.firstObject;
    BOOL access = [url startAccessingSecurityScopedResource];
    NSData* bookmark = [url bookmarkDataWithOptions:0 includingResourceValuesForKeys:nil relativeToURL:nil error:nil];
    if (access) {
        [url stopAccessingSecurityScopedResource];
    }
    if (bookmark != nil) {
        [NSUserDefaults.standardUserDefaults setObject:bookmark forKey:kBookmarkKey];
        if (sAccessing && sFolder != nil) {
            [sFolder stopAccessingSecurityScopedResource];
        }
        sFolder = nil;
        sAccessing = NO;
        setStatus("Saves sync with %s", url.lastPathComponent.UTF8String);
    }
    self.state = 2;
}
- (void)documentPickerWasCancelled:(UIDocumentPickerViewController*)controller {
    self.state = 3;
}
@end

static TTYDCloudPicker* sPicker;

static UIViewController* topController(void) {
    for (UIScene* scene in UIApplication.sharedApplication.connectedScenes) {
        if ([scene isKindOfClass:UIWindowScene.class]) {
            for (UIWindow* w in ((UIWindowScene*)scene).windows) {
                if (w.isKeyWindow) {
                    UIViewController* vc = w.rootViewController;
                    while (vc.presentedViewController != nil) {
                        vc = vc.presentedViewController;
                    }
                    return vc;
                }
            }
        }
    }
    return nil;
}

int portCloudPickFolder(void) {
    UIViewController* top = topController();
    if (top == nil) {
        return 0;
    }
    if (sPicker == nil) {
        sPicker = [[TTYDCloudPicker alloc] init];
    }
    UIDocumentPickerViewController* picker =
        [[UIDocumentPickerViewController alloc] initForOpeningContentTypes:@[ UTTypeFolder ]];
    picker.delegate = sPicker;
    picker.directoryURL = nil;
    sPicker.state = 1;
    [top presentViewController:picker animated:YES completion:nil];
    return 1;
}

void portCloudForget(void) {
    [NSUserDefaults.standardUserDefaults removeObjectForKey:kBookmarkKey];
    if (sAccessing && sFolder != nil) {
        [sFolder stopAccessingSecurityScopedResource];
    }
    sFolder = nil;
    sAccessing = NO;
    setStatus("No iCloud Drive folder");
}
#else
int portCloudPickFolder(void) {
    return 0; /* the Mac uses iCloud Drive/TTYD */
}

void portCloudForget(void) {}
#endif
