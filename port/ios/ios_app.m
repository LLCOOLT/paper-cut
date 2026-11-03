#include <string.h>
#include <stdio.h>
#include <stdlib.h>
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

/* A screenshot (PNG bytes) into the photo library (add-only access: the
 * system asks the first time). 0 if it could not be read as an image. */
int portIosSaveToPhotos(const unsigned char* png, size_t len) {
    @autoreleasepool {
        NSData* data = [NSData dataWithBytes:png length:len];
        UIImage* image = [UIImage imageWithData:data];
        if (image == nil) {
            return 0;
        }
        UIImageWriteToSavedPhotosAlbum(image, nil, NULL, NULL);
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
