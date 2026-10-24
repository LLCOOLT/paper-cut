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
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>
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

const char* portIosSavesDir(void) {
    return folder(sSaves, sizeof(sSaves), NSDocumentDirectory, @"Saves");
}

const char* portIosCacheDir(void) {
    return folder(sCaches, sizeof(sCaches), NSCachesDirectory, @"ttyd");
}

@interface PortDiscPicker : NSObject <UIDocumentPickerDelegate>
@property(nonatomic) int state; /* -1 open, 0 cancelled, 1 picked */
@property(nonatomic, copy) NSString* path;
@end

@implementation PortDiscPicker

- (void)documentPicker:(UIDocumentPickerViewController*)controller didPickDocumentsAtURLs:(NSArray<NSURL*>*)urls {
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
        [fm removeItemAtURL:dst error:nil];
        if (![fm moveItemAtURL:src toURL:dst error:&error]) {
            NSLog(@"ttyd: cannot move %@ into Documents: %@", src.path, error);
            dst = src; /* use the copy where it is (tmp/, may be purged later) */
        }
    }
    self.path = dst.path;
    self.state = 1;
}

- (void)documentPickerWasCancelled:(UIDocumentPickerViewController*)controller {
    self.state = 0;
}

@end

static PortDiscPicker* sPicker; /* the picker's delegate is weak */

int portIosPickDiscBegin(void) {
    SDL_Window* window = aurora_get_window();
    if (window == NULL) {
        return 0;
    }
    UIWindow* uiWindow = (__bridge UIWindow*)SDL_GetPointerProperty(SDL_GetWindowProperties(window),
                                                                    SDL_PROP_WINDOW_UIKIT_WINDOW_POINTER, NULL);
    UIViewController* root = uiWindow.rootViewController;
    while (root.presentedViewController != nil) {
        root = root.presentedViewController;
    }
    if (root == nil) {
        return 0;
    }
    UIDocumentPickerViewController* picker =
        [[UIDocumentPickerViewController alloc] initForOpeningContentTypes:@[ UTTypeData ] asCopy:YES];
    sPicker = [[PortDiscPicker alloc] init];
    sPicker.state = -1;
    picker.delegate = sPicker;
    picker.allowsMultipleSelection = NO;
    [root presentViewController:picker animated:YES completion:nil];
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
