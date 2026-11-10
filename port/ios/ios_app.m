#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/utsname.h>
/* iOS glue: the app's folders and the disc picker (port/include/port/ios_pc.h).
 *
 * Built on its own (port/ios/ios.cmake), without the game's prefix header:
 * Objective-C's BOOL is not the Dolphin SDK's.
 *
 * The disc is imported, not opened in place: the document picker copies the
 * chosen file (asCopy) and it is moved into Documents. A file opened in place
 * would need a security-scoped bookmark kept across launches, kept access
 * for every read nod makes, and can live in a file provider (iCloud Drive)
 * that evicts or downloads it on demand under a reader that expects a plain
 * local file. A copy in Documents is a plain file that survives app updates
 * (whose container path changes, so no absolute path is remembered), is found
 * by the scan at startup, and can be deleted from the Files app. Cost: one
 * copy of the image on first use (1.4 GB for an ISO, less for RVZ/CISO). */
#import <Foundation/Foundation.h>
#import <UIKit/UIKit.h>
#import <Photos/Photos.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>
#import <objc/runtime.h>
#include <SDL3/SDL.h>
#include <aurora/aurora.h>
#include <stdio.h>
#include "port/ios_pc.h"

static char sDocuments[1024];
static char sSaves[1024];
static char sCaches[1024];

static const char* folder(char* out, size_t size, NSSearchPathDirectory dir, NSString* sub) {
    if (out[0] == '\0') {
        NSFileManager* fm = NSFileManager.defaultManager;
        NSURL* url = [fm URLsForDirectory:dir inDomains:NSUserDomainMask].firstObject;
        if (url == nil) {
            return NULL;
        }
        if (sub != nil) {
            url = [url URLByAppendingPathComponent:sub isDirectory:YES];
        }
        NSError* error = nil;
        if (![fm createDirectoryAtURL:url withIntermediateDirectories:YES attributes:nil error:&error]) {
            NSLog(@"ttyd: cannot create %@: %@", url.path, error);
        }
        snprintf(out, size, "%s", url.fileSystemRepresentation);
    }
    return out;
}

const char* portIosDocumentsDir(void) {
    return folder(sDocuments, sizeof(sDocuments), NSDocumentDirectory, nil);
}

void portIosLoadEnv(void) {
    char path[1200], line[1024];
    const char* docs = portIosDocumentsDir();
    FILE* f;
    snprintf(path, sizeof(path), "%s/env.txt", docs);
    f = fopen(path, "r");
    if (f == NULL) {
        return;
    }
    while (fgets(line, sizeof(line), f) != NULL) {
        char value[2048];
        char* eq;
        const char* v;
        size_t n = strlen(line), o = 0;
        while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r' || line[n - 1] == ' ')) {
            line[--n] = '\0';
        }
        eq = strchr(line, '=');
        if (line[0] == '\0' || line[0] == '#' || eq == NULL || eq == line) {
            continue;
        }
        *eq = '\0';
        for (v = eq + 1; *v != '\0' && o + 1 < sizeof(value); v++) {
            if (strncmp(v, "{docs}", 6) == 0) {
                o += (size_t)snprintf(value + o, sizeof(value) - o, "%s", docs);
                if (o >= sizeof(value)) {
                    o = sizeof(value) - 1;
                }
                v += 5;
            } else {
                value[o++] = *v;
            }
        }
        value[o] = '\0';
        setenv(line, value, 0);
        printf("env.txt: %s=%s\n", line, value);
    }
    fclose(f);
}

const char* portIosSavesDir(void) {
    return folder(sSaves, sizeof(sSaves), NSDocumentDirectory, @"Saves");
}

const char* portIosCacheDir(void) {
    return folder(sCaches, sizeof(sCaches), NSCachesDirectory, @"ttyd");
}

/* portIosStorePath / portIosResolvePath (port/include/port/ios_pc.h): the container's path today,
 * without /private (the picker's URLs may carry it, Documents doesn't) */
static NSString* bare(NSString* path) {
    return [path hasPrefix:@"/private/var/"] ? [path substringFromIndex:8] : path;
}

static void putPath(NSString* path, const char* given, char* out, size_t size) {
    const char* fs = path.fileSystemRepresentation;
    if (fs == NULL || strlen(fs) >= size) {
        fs = given;
    }
    snprintf(out, size, "%s", fs);
}

void portIosStorePath(const char* path, char* out, size_t size) {
    if (size == 0) {
        return;
    }
    NSFileManager* fm = NSFileManager.defaultManager;
    NSString* p = bare([fm stringWithFileSystemRepresentation:path length:strlen(path)]);
    NSString* home = bare(NSHomeDirectory());
    if ([p hasPrefix:[home stringByAppendingString:@"/"]]) {
        p = [@"~" stringByAppendingString:[p substringFromIndex:home.length]];
    }
    putPath(p, path, out, size);
}

void portIosResolvePath(const char* stored, char* out, size_t size) {
    if (size == 0) {
        return;
    }
    NSFileManager* fm = NSFileManager.defaultManager;
    NSString* p = bare([fm stringWithFileSystemRepresentation:stored length:strlen(stored)]);
    NSString* home = bare(NSHomeDirectory());
    if ([p hasPrefix:@"~/"]) {
        p = [home stringByAppendingString:[p substringFromIndex:1]];
    } else {
        /* an older build's absolute path: .../Containers/Data/Application/<UUID>/<rest>, under today's */
        NSString* marker = @"/Containers/Data/Application/";
        NSRange at = [p rangeOfString:marker];
        if (at.location != NSNotFound) {
            NSUInteger from = at.location + marker.length;
            NSRange slash = [p rangeOfString:@"/" options:0 range:NSMakeRange(from, p.length - from)];
            if (slash.location != NSNotFound) {
                p = [home stringByAppendingString:[p substringFromIndex:slash.location]];
            }
        }
    }
    if (![fm fileExistsAtPath:p] && p.lastPathComponent.length > 0) {
        /* moved since (tmp/'s copy, a reinstall): the same name in Documents */
        const char* docs = portIosDocumentsDir();
        if (docs != NULL) {
            NSString* there = [@(docs) stringByAppendingPathComponent:p.lastPathComponent];
            if ([fm fileExistsAtPath:there]) {
                p = there;
            }
        }
    }
    putPath(p, stored, out, size);
}

void portIosExcludeFromBackup(const char* path) {
    if (path == NULL || path[0] == '\0') {
        return;
    }
    NSURL* url = [NSURL fileURLWithPath:[NSFileManager.defaultManager stringWithFileSystemRepresentation:path
                                                                                                 length:strlen(path)]];
    NSError* error = nil;
    NSNumber* excluded = nil;
    if ([url getResourceValue:&excluded forKey:NSURLIsExcludedFromBackupKey error:nil] && excluded.boolValue) {
        return;
    }
    if (![url setResourceValue:@YES forKey:NSURLIsExcludedFromBackupKey error:&error]) {
        NSLog(@"ttyd: %@ not left out of backups: %@", url.path, error);
    }
}

/* Every picker here (disc, file, folder, export) shares these. A picker that answered a moment ago, or
 * any presented controller still on its way in or out, means a new request now is the tap that closed the
 * picker reaching the game too, or a second tap: presenting over a controller going away fails (UIKit
 * refuses quietly), and its error would overwrite the pick's own outcome. Such a request is answered as a
 * cancel (an empty list), never an error. */
static CFAbsoluteTime sPickerAnsweredAt; /* when the last picker answered (pickerBusy) */
static const CFAbsoluteTime kPickerRest = 0.75;
/* how long a picker may take to show before it's given up on (presentPicker): the document picker's
 * browser is another process, and UIKit holds its presentation until that's up: no presentingViewController,
 * isBeingPresented or window for 2.5 s on the Simulator (iOS 26), while presentViewController returned at once */
static const CFAbsoluteTime kPickerShowWait = 10.0;
static char kPickerDelegateKey; /* the delegate, kept alive by its picker (the picker's own is weak) */

static void pickerAnswered(void) {
    sPickerAnsweredAt = CFAbsoluteTimeGetCurrent();
}

/* the game window's root controller */
static UIViewController* rootController(void) {
    SDL_Window* window = aurora_get_window();
    UIWindow* uiWindow = window == NULL ? nil
                                        : (__bridge UIWindow*)SDL_GetPointerProperty(
                                              SDL_GetWindowProperties(window), SDL_PROP_WINDOW_UIKIT_WINDOW_POINTER, NULL);
    return uiWindow.rootViewController;
}

/* the controller a picker is presented over: the game window's, or what it already shows */
static UIViewController* topController(void) {
    UIViewController* root = rootController();
    while (root.presentedViewController != nil) {
        root = root.presentedViewController;
    }
    return root;
}

/* something presented over the game is still coming or going (a picker's sheet, an alert just tapped) */
static BOOL presentationMoving(void) {
    for (UIViewController* c = rootController(); c != nil; c = c.presentedViewController) {
        if (c.isBeingDismissed || c.isBeingPresented) {
            return YES;
        }
    }
    return NO;
}

static CFAbsoluteTime sPickerOpenedAt; /* when the last picker was asked to show (presentPicker) */

static BOOL pickerBusy(void) {
    const CFAbsoluteTime now = CFAbsoluteTimeGetCurrent();
    const CFAbsoluteTime answered = now - sPickerAnsweredAt, opened = now - sPickerOpenedAt;
    return (answered >= 0 && answered < kPickerRest) || (opened >= 0 && opened < kPickerShowWait) ||
           presentationMoving();
}

/* A picker still on screen (or on its way there). One that isn't, with its latch still set, went
 * without an answer (UIKit refused to show it, or it was taken away): the latch is stale. */
static BOOL pickerShown(UIViewController* picker) {
    return picker != nil && (picker.presentingViewController != nil || picker.isBeingPresented ||
                             picker.viewIfLoaded.window != nil);
}

/* The picker shown over root, its delegate kept alive with it (so an answer always reaches it). UIKit
 * refuses quietly (only a line in the log, the delegate never called) when root is busy presenting or
 * going away: checked after kPickerShowWait (not the next turn: the picker isn't presented yet then, and one
 * given up on while it was still coming lost its pick), and then failed runs, which clears the
 * caller's one-at-a-time latch and answers its callback with an error, so a later pick isn't refused for
 * ever by a picker that never showed. */
static void presentPicker(UIViewController* root, UIViewController* picker, id delegate, void (^failed)(void)) {
    objc_setAssociatedObject(picker, &kPickerDelegateKey, delegate, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    sPickerOpenedAt = CFAbsoluteTimeGetCurrent();
    [root presentViewController:picker animated:YES completion:nil];
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(kPickerShowWait * NSEC_PER_SEC)),
                   dispatch_get_main_queue(), ^{
                       if (!pickerShown(picker)) {
                           NSLog(@"ttyd: the picker couldn't be shown over %@", root);
                           failed();
                       }
                   });
}

@interface PortDiscPicker : NSObject <UIDocumentPickerDelegate>
@property(nonatomic) int state; /* -1 open, 0 cancelled, 1 picked */
@property(nonatomic, copy) NSString* path;
@end

@implementation PortDiscPicker

- (void)documentPicker:(UIDocumentPickerViewController*)controller didPickDocumentsAtURLs:(NSArray<NSURL*>*)urls {
    pickerAnswered();
    NSURL* src = urls.firstObject;
    if (src == nil) {
        self.state = 0;
        return;
    }
    /* asCopy: src is the picker's copy in the app's tmp/ inbox. */
    NSURL* docs = [NSURL fileURLWithPath:@(portIosDocumentsDir()) isDirectory:YES];
    NSURL* dst = [docs URLByAppendingPathComponent:src.lastPathComponent];
    NSFileManager* fm = NSFileManager.defaultManager;
    if (![src.URLByStandardizingPath.path isEqualToString:dst.URLByStandardizingPath.path]) {
        NSError* error = nil;
        /* Only an earlier disc image of the same name is replaced: a folder
         * (Saves: every memory card) or any other file (env.txt) of that name
         * stays, and the disc gets a free name ("name (2).iso") instead. */
        static NSString* const kDiscExtensions[] = {@"iso", @"gcm", @"ciso", @"gcz", @"rvz", @"wbfs", @"wia", @"nfs"};
        BOOL isDir = NO;
        if ([fm fileExistsAtPath:dst.path isDirectory:&isDir]) {
            BOOL disc = NO;
            for (size_t i = 0; !isDir && i < sizeof(kDiscExtensions) / sizeof(kDiscExtensions[0]); i++) {
                disc = disc || [dst.pathExtension.lowercaseString isEqualToString:kDiscExtensions[i]];
            }
            if (disc) {
                [fm removeItemAtURL:dst error:nil];
            } else {
                NSString* stem = src.lastPathComponent.stringByDeletingPathExtension;
                NSString* ext = src.lastPathComponent.pathExtension;
                for (int n = 2; n < 1000 && [fm fileExistsAtPath:dst.path]; n++) {
                    NSString* name = [NSString stringWithFormat:@"%@ (%d)", stem, n];
                    if (ext.length > 0) {
                        name = [name stringByAppendingPathExtension:ext];
                    }
                    dst = [docs URLByAppendingPathComponent:name];
                }
            }
        }
        /* (moved, never copied: one copy of a 1.4 GB image, not two) */
        if (![fm moveItemAtURL:src toURL:dst error:&error]) {
            NSLog(@"ttyd: cannot move %@ into Documents: %@", src.path, error);
            dst = src; /* use the copy where it is (tmp/, may be purged later) */
        }
    }
    portIosExcludeFromBackup(dst.fileSystemRepresentation);
    self.path = dst.path;
    self.state = 1;
}

- (void)documentPickerWasCancelled:(UIDocumentPickerViewController*)controller {
    pickerAnswered();
    self.state = 0;
}

@end

static PortDiscPicker* sPicker; /* the polled answer (the picker keeps its delegate alive: presentPicker) */

int portIosPickDiscBegin(void) {
    if (aurora_get_window() == NULL) {
        return 0;
    }
    /* asked from the disc prompt's Choose File: its alert may still be going away (a picker presented over
     * it is refused), so wait for it, a second at most */
    for (int i = 0; i < 100 && presentationMoving(); i++) {
        [[NSRunLoop mainRunLoop] runMode:NSDefaultRunLoopMode beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.01]];
    }
    UIViewController* root = topController();
    if (root == nil) {
        return 0;
    }
    UIDocumentPickerViewController* picker =
        [[UIDocumentPickerViewController alloc] initForOpeningContentTypes:@[ UTTypeData ] asCopy:YES];
    PortDiscPicker* delegate = [[PortDiscPicker alloc] init];
    delegate.state = -1;
    sPicker = delegate;
    picker.delegate = delegate;
    picker.allowsMultipleSelection = NO;
    presentPicker(root, picker, delegate, ^{
        if (delegate.state < 0) { /* never shown: as cancelled (the disc's prompt asks again), not waited on for ever */
            SDL_SetError("The file picker couldn't be shown");
            delegate.state = 0;
        }
    });
    return 1;
}

int portIosPickDiscPoll(char* out, size_t size) {
    int state;
    if (sPicker == nil) {
        return 0;
    }
    state = sPicker.state;
    if (state < 0) {
        return -1;
    }
    if (state == 1) {
        snprintf(out, size, "%s", sPicker.path.fileSystemRepresentation);
    }
    sPicker = nil;
    return state;
}

/* portShowOpenFileDialog on iOS (port/include/port/offscreen.h): SDL's dialogs are not built
 * for iOS (its dummy backend answers "unsupported" at once), so the PM64 ROM, save import,
 * mods, presets, replays and seeds pickers did nothing. The system's document picker, any
 * file (public.data, as the disc picker: an N64 ROM's .z64 has no type of its own, and the
 * callers check what they're given), imported as a copy in the app's tmp/ inbox; the
 * callback gets their paths, or an empty list when cancelled, as SDL would give them. */
@interface PortFilePicker : NSObject <UIDocumentPickerDelegate>
@property(nonatomic) SDL_DialogFileCallback callback;
@property(nonatomic) void* userdata;
@property(nonatomic, weak) UIViewController* controller; /* the picker shown (pickerShown) */
@property(nonatomic) BOOL gaveUp; /* answered with an error by presentPicker's failed (a cancel after: dropped) */
@end

static PortFilePicker* sFilePicker; /* one picker at a time (the picker keeps its delegate alive: presentPicker) */

@implementation PortFilePicker

- (void)answer:(NSArray<NSURL*>*)urls {
    NSMutableArray<NSString*>* paths = [NSMutableArray array];
    for (NSURL* url in urls) {
        if (url.isFileURL) {
            [paths addObject:url.path];
        }
    }
    const char** files = (const char**)calloc(paths.count + 1, sizeof(char*));
    for (NSUInteger i = 0; files != NULL && i < paths.count; i++) {
        files[i] = paths[i].fileSystemRepresentation;
    }
    SDL_DialogFileCallback callback = self.callback;
    void* userdata = self.userdata;
    pickerAnswered();
    if (sFilePicker == self) {
        sFilePicker = nil; /* (a picker opened from the callback is a new one) */
    }
    if (self.gaveUp && paths.count == 0) {
        free(files);
        return; /* (already answered: failed) */
    }
    if (files == NULL) {
        SDL_SetError("Out of memory for the picked files");
        callback(userdata, NULL, -1);
        return;
    }
    callback(userdata, files, -1);
    free(files);
}

- (void)documentPicker:(UIDocumentPickerViewController*)controller didPickDocumentsAtURLs:(NSArray<NSURL*>*)urls {
    [self answer:urls];
}

- (void)documentPickerWasCancelled:(UIDocumentPickerViewController*)controller {
    [self answer:@[]];
}

@end

void portIosPickedDone(const char* path) {
    if (path == NULL || path[0] == '\0') {
        return;
    }
    NSString* file = [[NSString stringWithUTF8String:path] stringByResolvingSymlinksInPath];
    NSString* tmp = [NSTemporaryDirectory() stringByResolvingSymlinksInPath];
    if (file == nil || tmp.length == 0 || ![file hasPrefix:[tmp stringByAppendingString:@"/"]]) {
        return; /* (only the picker's copies: never a file of the player's own) */
    }
    NSError* error = nil;
    if (![[NSFileManager defaultManager] removeItemAtPath:file error:&error]) {
        NSLog(@"[picker] the picked copy stays: %@", error.localizedDescription);
    }
}

/* the answer to a request made while a picker is open, going away or just answered: a cancel (pickerBusy) */
static void answerAsCancel(SDL_DialogFileCallback callback, void* userdata) {
    const char* none[1] = {NULL};
    puts("[picker] a request as the last picker closes: answered as a cancel");
    callback(userdata, none, -1);
}

void portIosShowOpenFileDialog(SDL_DialogFileCallback callback, void* userdata, bool allow_many) {
    if ((sFilePicker != nil && pickerShown(sFilePicker.controller)) || pickerBusy()) {
        answerAsCancel(callback, userdata); /* (an open one answers its own) */
        return;
    }
    if (sFilePicker != nil) {
        PortFilePicker* stale = sFilePicker; /* gone without an answer: answered now, and a new one */
        sFilePicker = nil;
        SDL_SetError("The file picker closed without an answer");
        stale.callback(stale.userdata, NULL, -1);
    }
    UIViewController* root = topController();
    if (root == nil) {
        SDL_SetError("No window to show the file picker over");
        callback(userdata, NULL, -1);
        return;
    }
    UIDocumentPickerViewController* picker =
        [[UIDocumentPickerViewController alloc] initForOpeningContentTypes:@[ UTTypeData ] asCopy:YES];
    PortFilePicker* delegate = [[PortFilePicker alloc] init];
    delegate.callback = callback;
    delegate.userdata = userdata;
    delegate.controller = picker;
    sFilePicker = delegate;
    picker.delegate = delegate;
    picker.allowsMultipleSelection = allow_many ? YES : NO;
    presentPicker(root, picker, delegate, ^{
        if (sFilePicker == delegate) {
            sFilePicker = nil;
            delegate.gaveUp = YES;
            SDL_SetError("The file picker couldn't be shown");
            callback(userdata, NULL, -1);
        }
    });
}

/* portShowOpenFolderDialog on iOS: a folder can't be imported as a copy, so it is opened in place, the
 * picker's URL under security-scoped access from the pick until the caller releases it
 * (portIosReleasePickedFolder: the mods installer, when its copy is done); the callback gets its path. */
static NSMutableArray<NSURL*>* sScopedFolders; /* the picked folders under access; @synchronized */

@interface PortFolderPicker : NSObject <UIDocumentPickerDelegate>
@property(nonatomic) SDL_DialogFileCallback callback;
@property(nonatomic) void* userdata;
@property(nonatomic, weak) UIViewController* controller; /* the picker shown (pickerShown) */
@property(nonatomic) BOOL gaveUp; /* answered with an error by presentPicker's failed (a cancel after: dropped) */
@end

static PortFolderPicker* sFolderPicker; /* one at a time (the picker keeps its delegate alive: presentPicker) */

@implementation PortFolderPicker

- (void)answer:(NSArray<NSURL*>*)urls {
    NSMutableArray<NSString*>* paths = [NSMutableArray array];
    for (NSURL* url in urls) {
        if (!url.isFileURL) {
            continue;
        }
        if ([url startAccessingSecurityScopedResource]) {
            @synchronized(NSFileManager.class) {
                if (sScopedFolders == nil) {
                    sScopedFolders = [NSMutableArray array];
                }
                [sScopedFolders addObject:url];
            }
        } /* (no scope to start: a folder of the app's own, readable as it is) */
        [paths addObject:url.path];
    }
    const char** files = (const char**)calloc(paths.count + 1, sizeof(char*));
    for (NSUInteger i = 0; files != NULL && i < paths.count; i++) {
        files[i] = paths[i].fileSystemRepresentation;
    }
    SDL_DialogFileCallback callback = self.callback;
    void* userdata = self.userdata;
    pickerAnswered();
    if (sFolderPicker == self) {
        sFolderPicker = nil;
    }
    if (self.gaveUp && paths.count == 0) {
        free(files);
        return; /* (already answered: failed) */
    }
    if (files == NULL) {
        SDL_SetError("Out of memory for the picked folders");
        callback(userdata, NULL, -1);
        return;
    }
    callback(userdata, files, -1);
    free(files);
}

- (void)documentPicker:(UIDocumentPickerViewController*)controller didPickDocumentsAtURLs:(NSArray<NSURL*>*)urls {
    [self answer:urls];
}

- (void)documentPickerWasCancelled:(UIDocumentPickerViewController*)controller {
    [self answer:@[]];
}

@end

void portIosShowOpenFolderDialog(SDL_DialogFileCallback callback, void* userdata, bool allow_many) {
    if ((sFolderPicker != nil && pickerShown(sFolderPicker.controller)) || pickerBusy()) {
        answerAsCancel(callback, userdata); /* (an open one answers its own) */
        return;
    }
    if (sFolderPicker != nil) {
        PortFolderPicker* stale = sFolderPicker; /* gone without an answer: answered now, and a new one */
        sFolderPicker = nil;
        SDL_SetError("The folder picker closed without an answer");
        stale.callback(stale.userdata, NULL, -1);
    }
    UIViewController* root = topController();
    if (root == nil) {
        SDL_SetError("No window to show the folder picker over");
        callback(userdata, NULL, -1);
        return;
    }
    UIDocumentPickerViewController* picker =
        [[UIDocumentPickerViewController alloc] initForOpeningContentTypes:@[ UTTypeFolder ] asCopy:NO];
    PortFolderPicker* delegate = [[PortFolderPicker alloc] init];
    delegate.callback = callback;
    delegate.userdata = userdata;
    delegate.controller = picker;
    sFolderPicker = delegate;
    picker.delegate = delegate;
    picker.allowsMultipleSelection = allow_many ? YES : NO;
    presentPicker(root, picker, delegate, ^{
        if (sFolderPicker == delegate) {
            sFolderPicker = nil;
            delegate.gaveUp = YES;
            SDL_SetError("The folder picker couldn't be shown");
            callback(userdata, NULL, -1);
        }
    });
}

/* (any thread: the mods installer's worker) */
void portIosReleasePickedFolder(const char* path) {
    if (path == NULL) {
        return;
    }
    NSString* wanted = [NSFileManager.defaultManager stringWithFileSystemRepresentation:path length:strlen(path)];
    @synchronized(NSFileManager.class) {
        for (NSUInteger i = 0; i < sScopedFolders.count; i++) {
            NSURL* url = sScopedFolders[i];
            if ([url.path isEqualToString:wanted]) {
                [url stopAccessingSecurityScopedResource];
                [sScopedFolders removeObjectAtIndex:i];
                return; /* (picked twice: each pick started its own, each release ends one) */
            }
        }
    }
}

/* portfile::exportFile on iOS (port/settings/file_export.h): the bytes written as tmp/Export-<uuid>/<name>,
 * then the export picker (asCopy: the copy goes where the player picks, Files or iCloud Drive or another
 * app's folder); the callback gets where it went, an empty list when cancelled, or nothing (SDL_GetError)
 * when the file couldn't be written; the tmp/ copy is removed after. */
@interface PortFileExporter : NSObject <UIDocumentPickerDelegate>
@property(nonatomic) SDL_DialogFileCallback callback;
@property(nonatomic) void* userdata;
@property(nonatomic, strong) NSURL* folder; /* tmp/Export-<uuid> */
@property(nonatomic, weak) UIViewController* controller; /* the picker shown (pickerShown) */
@property(nonatomic) BOOL gaveUp; /* answered with an error by presentPicker's failed (a cancel after: dropped) */
@end

static PortFileExporter* sExporter; /* one at a time (the picker keeps its delegate alive: presentPicker) */

@implementation PortFileExporter

- (void)answer:(NSArray<NSURL*>*)urls {
    NSString* where = nil;
    for (NSURL* url in urls) {
        if (url.isFileURL) {
            where = url.path;
            break;
        }
    }
    const char* files[2] = {where != nil ? where.fileSystemRepresentation : NULL, NULL};
    SDL_DialogFileCallback callback = self.callback;
    void* userdata = self.userdata;
    [NSFileManager.defaultManager removeItemAtURL:self.folder error:nil];
    pickerAnswered();
    if (sExporter == self) {
        sExporter = nil;
    }
    if (self.gaveUp && where == nil) {
        return; /* (already answered: failed) */
    }
    callback(userdata, files, -1);
}

- (void)documentPicker:(UIDocumentPickerViewController*)controller didPickDocumentsAtURLs:(NSArray<NSURL*>*)urls {
    [self answer:urls];
}

- (void)documentPickerWasCancelled:(UIDocumentPickerViewController*)controller {
    [self answer:@[]];
}

@end

void portIosExportFile(SDL_DialogFileCallback callback, void* userdata, const char* name, const void* data,
                       size_t size) {
    if ((sExporter != nil && pickerShown(sExporter.controller)) || pickerBusy()) {
        answerAsCancel(callback, userdata); /* (an open one answers its own) */
        return;
    }
    if (sExporter != nil) {
        PortFileExporter* stale = sExporter; /* gone without an answer: answered now, and a new one */
        sExporter = nil;
        [NSFileManager.defaultManager removeItemAtURL:stale.folder error:nil];
        SDL_SetError("The export picker closed without an answer");
        stale.callback(stale.userdata, NULL, -1);
    }
    UIViewController* root = topController();
    if (root == nil) {
        SDL_SetError("No window to show the export picker over");
        callback(userdata, NULL, -1);
        return;
    }
    NSFileManager* fm = NSFileManager.defaultManager;
    NSString* file = name != NULL ? [fm stringWithFileSystemRepresentation:name length:strlen(name)] : nil;
    file = file.lastPathComponent; /* (a name, never a path out of the folder) */
    if (file.length == 0 || [file isEqualToString:@"/"] || [file isEqualToString:@".."]) {
        file = @"Paper Cut export";
    }
    NSURL* folder = [NSURL fileURLWithPath:[NSTemporaryDirectory()
                                               stringByAppendingPathComponent:
                                                   [@"Export-" stringByAppendingString:NSUUID.UUID.UUIDString]]
                               isDirectory:YES];
    NSURL* url = [folder URLByAppendingPathComponent:file isDirectory:NO];
    NSError* error = nil;
    if (![fm createDirectoryAtURL:folder withIntermediateDirectories:YES attributes:nil error:&error] ||
        ![[NSData dataWithBytes:data length:size] writeToURL:url options:NSDataWritingAtomic error:&error]) {
        [fm removeItemAtURL:folder error:nil];
        SDL_SetError("%s", error.localizedDescription.UTF8String ?: "the file couldn't be written");
        callback(userdata, NULL, -1);
        return;
    }
    UIDocumentPickerViewController* picker =
        [[UIDocumentPickerViewController alloc] initForExportingURLs:@[ url ] asCopy:YES];
    PortFileExporter* delegate = [[PortFileExporter alloc] init];
    delegate.callback = callback;
    delegate.userdata = userdata;
    delegate.folder = folder;
    delegate.controller = picker;
    sExporter = delegate;
    picker.delegate = delegate;
    presentPicker(root, picker, delegate, ^{
        if (sExporter == delegate) {
            sExporter = nil;
            delegate.gaveUp = YES; /* (its tmp/ folder stays for a sheet still coming: its answer removes it) */
            SDL_SetError("The export picker couldn't be shown");
            callback(userdata, NULL, -1);
        }
    });
}

/* The screen's model, kind and scales (port/include/port/ios_pc.h): the game's thread is the main
 * one (SDL's iOS main), where UIKit is read. */
int portIosScreenInfo(char* model, size_t size, int* pad, int* scale, float* nativeScale, float* shortSidePts) {
    UIScreen* screen = UIScreen.mainScreen;
    const char* sim = getenv("SIMULATOR_MODEL_IDENTIFIER");
    struct utsname u;
    CGSize b;
    if (screen == nil || size == 0) {
        return 0;
    }
    if (sim != NULL) {
        snprintf(model, size, "%s", sim);
    } else if (uname(&u) == 0) {
        snprintf(model, size, "%s", u.machine);
    } else {
        model[0] = '\0';
    }
    b = screen.bounds.size;
    *pad = UIDevice.currentDevice.userInterfaceIdiom == UIUserInterfaceIdiomPad;
    *scale = (int)(screen.scale + 0.5);
    *nativeScale = (float)screen.nativeScale;
    *shortSidePts = (float)(b.width < b.height ? b.width : b.height);
    return 1;
}

/* A light tap for an on-screen button press (port/touch/touch_overlay.cpp,
 * the touch_haptics setting). */
void portIosHapticTap(void) {
    static UIImpactFeedbackGenerator* sGen;
    if (sGen == nil) {
        sGen = [[UIImpactFeedbackGenerator alloc] initWithStyle:UIImpactFeedbackStyleLight];
    }
    [sGen impactOccurred];
    [sGen prepare];
}

/* the PNG added to the photo library; its answer to portShotPhotosDone (any thread: PhotoKit's queue) */
static void addToPhotos(NSData* data) {
    [[PHPhotoLibrary sharedPhotoLibrary]
        performChanges:^{
            [[PHAssetCreationRequest creationRequestForAsset] addResourceWithType:PHAssetResourceTypePhoto
                                                                             data:data
                                                                          options:nil];
        }
        completionHandler:^(BOOL success, NSError* error) {
            if (!success) {
                NSLog(@"ttyd: the picture wasn't added to Photos: %@", error);
            }
            portShotPhotosDone(success ? 1 : 0);
        }];
}

/* A screenshot (PNG bytes) into the photo library (add-only access: the
 * system asks the first time). 1: under way, its outcome told later to
 * portShotPhotosDone (saved, or refused: the access denied when asked, the
 * library's error); 0 at once when it can't be (not an image, Photos access
 * denied or restricted in Settings). Never waits (the game's thread may call it). */
int portIosSaveToPhotos(const unsigned char* png, size_t len) {
    @autoreleasepool {
        NSData* data = [NSData dataWithBytes:png length:len];
        if ([UIImage imageWithData:data] == nil) {
            return 0;
        }
        const PHAuthorizationStatus status = [PHPhotoLibrary authorizationStatusForAccessLevel:PHAccessLevelAddOnly];
        if (status == PHAuthorizationStatusNotDetermined) {
            [PHPhotoLibrary requestAuthorizationForAccessLevel:PHAccessLevelAddOnly
                                                       handler:^(PHAuthorizationStatus answer) {
                                                           if (answer == PHAuthorizationStatusAuthorized ||
                                                               answer == PHAuthorizationStatusLimited) {
                                                               addToPhotos(data);
                                                           } else {
                                                               NSLog(@"ttyd: Photos access not given (%ld)", (long)answer);
                                                               portShotPhotosDone(0);
                                                           }
                                                       }];
            return 1;
        }
        if (status != PHAuthorizationStatusAuthorized && status != PHAuthorizationStatusLimited) {
            NSLog(@"ttyd: no access to Photos (%ld): Settings > Privacy > Photos", (long)status);
            return 0;
        }
        addToPhotos(data);
        return 1;
    }
}

/* The orientations allowed changed (SDL_HINT_ORIENTATIONS, which SDL reads
 * whenever UIKit asks): 0 any, 1 landscape, 2 portrait. UIKit is told to ask
 * again, and to turn the screen now if it is held a way no longer allowed. */
void portIosRefreshOrientation(int orientation) {
    if (@available(iOS 16.0, *)) {
        UIInterfaceOrientationMask mask = orientation == 1   ? UIInterfaceOrientationMaskLandscape
                                          : orientation == 2 ? UIInterfaceOrientationMaskPortrait
                                                             : (UIInterfaceOrientationMaskLandscape |
                                                                UIInterfaceOrientationMaskPortrait);
        for (UIScene* scene in UIApplication.sharedApplication.connectedScenes) {
            if (![scene isKindOfClass:UIWindowScene.class]) {
                continue;
            }
            UIWindowScene* ws = (UIWindowScene*)scene;
            for (UIWindow* w in ws.windows) {
                [w.rootViewController setNeedsUpdateOfSupportedInterfaceOrientations];
            }
            if (orientation != 0) {
                UIWindowSceneGeometryPreferencesIOS* prefs =
                    [[UIWindowSceneGeometryPreferencesIOS alloc] initWithInterfaceOrientations:mask];
                [ws requestGeometryUpdateWithPreferences:prefs
                                            errorHandler:^(NSError* error) {
                                              (void)error;
                                            }];
            }
        }
    }
}
