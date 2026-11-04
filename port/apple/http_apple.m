/* HTTPS on iOS (port/include/port/http.h, port/settings/http.cpp), where
 * there is no curl to run: NSURLSession, its body handed to the caller's
 * callback chunk by chunk as it arrives, on the session's own queue while
 * the caller's thread waits. Redirects are followed only to https://. The
 * Mac uses curl like Linux, so this is iOS only.
 *
 * Built in ttyd_cloud (CMakeLists.txt), without the game's prefix header. */
#import <Foundation/Foundation.h>
#include <TargetConditionals.h>
#include <stdio.h>

#if TARGET_OS_IPHONE

typedef int (*PortHttpData)(void* user, const void* bytes, size_t size, unsigned long long total);

@interface PortHttpTask : NSObject <NSURLSessionDataDelegate>
@property(nonatomic) PortHttpData data;
@property(nonatomic) void* user;
@property(nonatomic) unsigned long long total;
@property(nonatomic, copy) NSString* error; /* why it failed, or nil */
@property(nonatomic) BOOL stopped;          /* the callback said stop */
@property(nonatomic, strong) dispatch_semaphore_t done;
@end

@implementation PortHttpTask

- (void)URLSession:(NSURLSession*)session
              dataTask:(NSURLSessionDataTask*)task
    didReceiveResponse:(NSURLResponse*)response
     completionHandler:(void (^)(NSURLSessionResponseDisposition))completionHandler {
    const NSInteger status =
        [response isKindOfClass:[NSHTTPURLResponse class]] ? ((NSHTTPURLResponse*)response).statusCode : 0;
    if (status != 200) {
        self.error = [NSString stringWithFormat:@"the server answered %ld", (long)status];
        completionHandler(NSURLSessionResponseCancel);
        return;
    }
    self.total = response.expectedContentLength > 0 ? (unsigned long long)response.expectedContentLength : 0;
    completionHandler(NSURLSessionResponseAllow);
}

- (void)URLSession:(NSURLSession*)session dataTask:(NSURLSessionDataTask*)task didReceiveData:(NSData*)data {
    if (self.stopped) {
        return;
    }
    __block BOOL keep = YES;
    [data enumerateByteRangesUsingBlock:^(const void* bytes, NSRange range, BOOL* stop) {
        if (!self.data(self.user, bytes, range.length, self.total)) {
            keep = NO;
            *stop = YES;
        }
    }];
    if (!keep) {
        self.stopped = YES;
        [task cancel];
    }
}

/* A redirect is followed only to another https:// address; refused, the
 * redirect itself is the answer (not 200: an error). */
- (void)URLSession:(NSURLSession*)session
                          task:(NSURLSessionTask*)task
    willPerformHTTPRedirection:(NSHTTPURLResponse*)response
                    newRequest:(NSURLRequest*)request
             completionHandler:(void (^)(NSURLRequest*))completionHandler {
    completionHandler([request.URL.scheme.lowercaseString isEqualToString:@"https"] ? request : nil);
}

- (void)URLSession:(NSURLSession*)session task:(NSURLSessionTask*)task didCompleteWithError:(NSError*)error {
    if (error != nil && self.error == nil && !self.stopped) {
        self.error = error.localizedDescription;
    }
    dispatch_semaphore_signal(self.done);
}

@end

int portHttpAppleGet(const char* url, const char* accept, PortHttpData data, void* user, char* error,
                     size_t errorSize) {
    @autoreleasepool {
        NSURL* address = [NSURL URLWithString:[NSString stringWithUTF8String:url]];
        if (address == nil || ![address.scheme.lowercaseString isEqualToString:@"https"]) {
            snprintf(error, errorSize, "not an https:// address");
            return 0;
        }
        NSMutableURLRequest* request = [NSMutableURLRequest requestWithURL:address];
        request.timeoutInterval = 30;
        [request setValue:@"PaperCut" forHTTPHeaderField:@"User-Agent"];
        if (accept != NULL) {
            [request setValue:[NSString stringWithUTF8String:accept] forHTTPHeaderField:@"Accept"];
        }
        PortHttpTask* task = [PortHttpTask new];
        task.data = data;
        task.user = user;
        task.done = dispatch_semaphore_create(0);
        NSURLSessionConfiguration* config = [NSURLSessionConfiguration ephemeralSessionConfiguration];
        config.requestCachePolicy = NSURLRequestReloadIgnoringLocalCacheData;
        /* the session keeps its delegate until it is invalidated */
        NSURLSession* session = [NSURLSession sessionWithConfiguration:config delegate:task delegateQueue:nil];
        [[session dataTaskWithRequest:request] resume];
        dispatch_semaphore_wait(task.done, DISPATCH_TIME_FOREVER);
        [session finishTasksAndInvalidate];
        if (task.stopped) {
            snprintf(error, errorSize, "stopped");
            return 0;
        }
        if (task.error != nil) {
            snprintf(error, errorSize, "%s", task.error.UTF8String);
            return 0;
        }
        return 1;
    }
}

#endif
