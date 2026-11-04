// Text to speech on macOS and iOS (port/settings/features/speech.cpp):
// AVSpeechSynthesizer, a voice of the game's language, the game's text
// decoded from its own encoding (Shift-JIS on the Japanese release,
// Windows-1252 on the others). Each new line cuts off the last.
#import <AVFoundation/AVFoundation.h>

static AVSpeechSynthesizer* sSynth;

void portSpeechAppleSay(const char* text, int length, int japanese, const char* language, float rate) {
    @autoreleasepool {
        NSString* words = [[NSString alloc] initWithBytes:text
                                                   length:(NSUInteger)length
                                                 encoding:japanese ? NSShiftJISStringEncoding
                                                                   : NSWindowsCP1252StringEncoding];
        AVSpeechUtterance* utterance;
        float r;
        if (words == nil || words.length == 0) {
            return;
        }
        if (sSynth == nil) {
            sSynth = [[AVSpeechSynthesizer alloc] init];
        }
        [sSynth stopSpeakingAtBoundary:AVSpeechBoundaryImmediate];
        utterance = [AVSpeechUtterance speechUtteranceWithString:words];
        utterance.voice = [AVSpeechSynthesisVoice voiceWithLanguage:[NSString stringWithUTF8String:language]];
        r = AVSpeechUtteranceDefaultSpeechRate * rate;
        utterance.rate = r < AVSpeechUtteranceMinimumSpeechRate   ? AVSpeechUtteranceMinimumSpeechRate
                         : r > AVSpeechUtteranceMaximumSpeechRate ? AVSpeechUtteranceMaximumSpeechRate
                                                                  : r;
        [sSynth speakUtterance:utterance];
    }
}

void portSpeechAppleStop(void) {
    if (sSynth != nil) {
        [sSynth stopSpeakingAtBoundary:AVSpeechBoundaryImmediate];
    }
}
