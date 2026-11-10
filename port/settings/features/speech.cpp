// Text to speech (Accessibility > Read text aloud; port/include/port/speech.h).
// The game's message windows read page by page as they show (their text split
// at the game's own page breaks, <p>, its tags taken out), and with "and
// menus" the Start menu's help line and Settings rows as the cursor moves.
// The system's own voice, in the game's language:
//   Windows    SAPI (ISpVoice), a voice of that language if one is installed
//   Apple      AVSpeechSynthesizer (port/apple/speech_apple.m)
//   Android    TextToSpeech (TtydActivity.speechSay)
//   Linux      speech-dispatcher's spd-say
// The game's text is Shift-JIS on the Japanese release, Latin-1 (Windows-1252)
// on the others: each backend decodes it so. The pages come from
// message_text.cpp, which the message log shares.
#include "port/file_pc.h"
#include "port/savestate.h" // portSaveStateNotice: "No voice found"
#include "port/settings.h"
#include "port/speech.h"
#include "port/tr.h"
#include "../message_text.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#include <sapi.h>
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "sapi.lib")
#elif defined(__APPLE__)
extern "C" void portSpeechAppleSay(const char* text, int length, int japanese, const char* language, float rate);
extern "C" void portSpeechAppleStop(void);
#elif defined(__ANDROID__)
#include <jni.h>
#elif !defined(__EMSCRIPTEN__)
#include <iconv.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
#include <condition_variable>
#include <mutex>
#include <thread>
extern char** environ;
#endif

extern "C" int portSpeechGameLanguage(void); // speech_aurora.c

namespace {

enum { kOff, kMessages, kAll };
const char* kChoices[] = {"Off", "Message windows", "Messages and menus"};
int* sMode;
int* sRate; // -5..5: slower .. faster

using msgtext::japanese;

// the game's language as a BCP 47 tag ("en", "ja"...)
const char* language() {
  static const char* kEu[] = {"en", "de", "fr", "es", "it"};
  if (japanese()) {
    return "ja";
  }
  if (std::strncmp(PORT_GAME_ID, "G8MP", 4) == 0) {
    const int l = portSpeechGameLanguage();
    return l >= 0 && l < 5 ? kEu[l] : "en";
  }
  return "en";
}

// --- the voices ---

#if defined(_WIN32)
ISpVoice* sVoice;
const char* sVoiceLanguage;

ISpVoice* voice() {
  if (sVoice == nullptr) {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED); // (already so on this thread: fine)
    if (FAILED(CoCreateInstance(CLSID_SpVoice, nullptr, CLSCTX_ALL, IID_ISpVoice, reinterpret_cast<void**>(&sVoice)))) {
      sVoice = nullptr;
      return nullptr;
    }
  }
  // a voice of the game's language when one is installed (else the system's own)
  const char* lang = language();
  if (sVoiceLanguage != lang) {
    sVoiceLanguage = lang;
    struct {
      const char* tag;
      const wchar_t* attr;
    } static const kLcid[] = {{"en", L"Language=409"}, {"de", L"Language=407"}, {"fr", L"Language=40C"},
                              {"es", L"Language=C0A;40A"}, {"it", L"Language=410"}, {"ja", L"Language=411"}};
    ISpObjectTokenCategory* category = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_SpObjectTokenCategory, nullptr, CLSCTX_ALL, IID_ISpObjectTokenCategory,
                                   reinterpret_cast<void**>(&category))) &&
        SUCCEEDED(category->SetId(SPCAT_VOICES, FALSE))) {
      for (const auto& l : kLcid) {
        IEnumSpObjectTokens* tokens = nullptr;
        ISpObjectToken* token = nullptr;
        if (std::strcmp(l.tag, lang) == 0 && SUCCEEDED(category->EnumTokens(l.attr, nullptr, &tokens)) &&
            tokens->Next(1, &token, nullptr) == S_OK) {
          sVoice->SetVoice(token);
          token->Release();
        }
        if (tokens != nullptr) {
          tokens->Release();
        }
      }
    }
    if (category != nullptr) {
      category->Release();
    }
  }
  return sVoice;
}

void say(const std::string& text) {
  ISpVoice* v = voice();
  if (v == nullptr) {
    return;
  }
  const UINT page = japanese() ? 932 : 1252;
  const int n = MultiByteToWideChar(page, 0, text.data(), int(text.size()), nullptr, 0);
  std::wstring w(size_t(n), L'\0');
  MultiByteToWideChar(page, 0, text.data(), int(text.size()), w.data(), n);
  v->SetRate(std::clamp(*sRate * 2, -10, 10));
  v->Speak(w.c_str(), SPF_ASYNC | SPF_PURGEBEFORESPEAK | SPF_IS_NOT_XML, nullptr);
}

void stop() {
  if (sVoice != nullptr) {
    sVoice->Speak(L"", SPF_ASYNC | SPF_PURGEBEFORESPEAK, nullptr);
  }
}

bool haveVoice() { return voice() != nullptr; }

#elif defined(__APPLE__)
float appleRate() { return std::clamp(1.f + float(*sRate) * 0.15f, 0.3f, 1.8f); }
void say(const std::string& text) {
  portSpeechAppleSay(text.data(), int(text.size()), japanese() ? 1 : 0, language(), appleRate());
}
void stop() { portSpeechAppleStop(); }
bool haveVoice() { return true; } // (AVSpeechSynthesizer: the system always has one)

#elif defined(__ANDROID__)
// the activity's static methods (TtydActivity.java), as http.cpp reaches them
void callJava(const char* name, const std::string* text) {
  auto* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
  auto activity = static_cast<jobject>(SDL_GetAndroidActivity());
  if (env == nullptr || activity == nullptr) {
    return;
  }
  jclass cls = env->GetObjectClass(activity);
  if (text == nullptr) {
    jmethodID m = env->GetStaticMethodID(cls, name, "()V");
    if (m != nullptr) {
      env->CallStaticVoidMethod(cls, m);
    }
  } else {
    jmethodID m = env->GetStaticMethodID(cls, name, "([BZLjava/lang/String;F)V");
    if (m != nullptr) {
      jbyteArray bytes = env->NewByteArray(jsize(text->size()));
      env->SetByteArrayRegion(bytes, 0, jsize(text->size()), reinterpret_cast<const jbyte*>(text->data()));
      jstring lang = env->NewStringUTF(language());
      env->CallStaticVoidMethod(cls, m, bytes, japanese() ? JNI_TRUE : JNI_FALSE, lang,
                                std::clamp(1.f + float(*sRate) * 0.15f, 0.3f, 2.f));
      env->DeleteLocalRef(bytes);
      env->DeleteLocalRef(lang);
    }
  }
  if (env->ExceptionCheck()) {
    env->ExceptionClear();
  }
  env->DeleteLocalRef(cls);
}
void say(const std::string& text) { callJava("speechSay", &text); }
void stop() { callJava("speechStop", nullptr); }
bool haveVoice() { return true; } // (TextToSpeech: the system's engine; it says so itself when it has none)

#elif !defined(__EMSCRIPTEN__)
// Linux: speech-dispatcher's spd-say (no shell: its arguments as they are)
std::vector<pid_t>& sChildren = *new std::vector<pid_t>; // (the worker's, below: left allocated at exit)
std::atomic<bool> sNoSpdSay{false};                      // spd-say couldn't be started (the worker's)

// spd-say on $PATH (speech-dispatcher installed): looked for once
bool haveVoice() {
  static const bool sOnPath = [] {
    const char* path = std::getenv("PATH");
    std::string dirs = path != nullptr ? path : "/usr/bin:/bin";
    size_t from = 0;
    while (from <= dirs.size()) {
      const size_t to = std::min(dirs.find(':', from), dirs.size());
      const std::string dir = dirs.substr(from, to - from);
      if (!dir.empty() && access((dir + "/spd-say").c_str(), X_OK) == 0) {
        return true;
      }
      from = to + 1;
    }
    return false;
  }();
  return sOnPath && !sNoSpdSay;
}

void reap() {
  sChildren.erase(std::remove_if(sChildren.begin(), sChildren.end(),
                                 [](pid_t p) { return waitpid(p, nullptr, WNOHANG) != 0; }),
                  sChildren.end());
}

// spd-say started with args (its pid, or 0)
pid_t run(std::vector<std::string> args) {
  std::vector<char*> argv;
  for (std::string& a : args) {
    argv.push_back(a.data());
  }
  argv.push_back(nullptr);
  pid_t pid = 0;
  if (posix_spawnp(&pid, "spd-say", nullptr, nullptr, argv.data(), environ) == 0) {
    sChildren.push_back(pid);
  } else {
    pid = 0;
    sNoSpdSay = true; // (said on the page's next frame: watch below)
  }
  reap();
  return pid;
}

// the cancel finished before what comes next is sent (two spd-say at once can reach
// speech-dispatcher in either order: the cancel would cut the new text off); it is quick,
// and never waited on for more than half a second
void runAndWait(std::vector<std::string> args) {
  const pid_t pid = run(std::move(args));
  for (int i = 0; pid > 0 && i < 100; i++) {
    const pid_t done = waitpid(pid, nullptr, WNOHANG);
    if (done != 0) {
      sChildren.erase(std::remove(sChildren.begin(), sChildren.end(), pid), sChildren.end());
      return;
    }
    SDL_Delay(5);
  }
}

std::string utf8(const std::string& text) {
  std::string out;
  if (japanese()) {
    iconv_t cd = iconv_open("UTF-8", "SHIFT_JIS");
    if (cd == iconv_t(-1)) {
      return out;
    }
    std::string in = text;
    out.resize(in.size() * 3 + 4);
    char* src = in.data();
    char* dst = out.data();
    size_t left = in.size(), room = out.size();
    iconv(cd, &src, &left, &dst, &room);
    iconv_close(cd);
    out.resize(out.size() - room);
    return out;
  }
  for (unsigned char c : text) { // Latin-1
    if (c < 0x80) {
      out += char(c);
    } else {
      out += char(0xC0 | (c >> 6));
      out += char(0x80 | (c & 0x3F));
    }
  }
  return out;
}

// The cancel and the say run on a thread of their own, never the caller's (the game's frame): the
// first cancel, while spd-say autostarts speech-dispatcher, takes the whole half second. One request
// waits at most: a newer one replaces it (what's said is the latest page; a stop is a stop). Its
// state is left allocated at exit (the thread may be mid-wait).
struct Worker {
  std::mutex lock;
  std::condition_variable wake;
  bool pending = false;
  bool isSay = false;
  std::vector<std::string> args; // the say's
};

Worker& worker() {
  static Worker* sWorker = [] {
    Worker* w = new Worker;
    std::thread([w] {
      for (;;) {
        std::vector<std::string> args;
        bool isSay;
        {
          std::unique_lock hold(w->lock);
          w->wake.wait(hold, [w] { return w->pending; });
          w->pending = false;
          isSay = w->isSay;
          args = std::move(w->args);
        }
        if (isSay) {
          runAndWait({"spd-say", "-C"});
          run(std::move(args));
        } else {
          run({"spd-say", "-C"});
        }
      }
    }).detach();
    return w;
  }();
  return *sWorker;
}

void request(bool isSay, std::vector<std::string> args) {
  Worker& w = worker();
  {
    std::lock_guard hold(w.lock);
    w.pending = true;
    w.isSay = isSay;
    w.args = std::move(args);
  }
  w.wake.notify_one();
}

void say(const std::string& text) {
  request(true, {"spd-say", "-l", language(), "-r", std::to_string(std::clamp(*sRate * 20, -100, 100)), "--",
                 utf8(text)});
}
void stop() { request(false, {}); }

#else
void say(const std::string&) {}
void stop() {}
bool haveVoice() { return false; }
#endif

// what's said: to the voice, or ($TTYD_SPEECH_LOG=<file>, tests) a line each in the file, no voice
void speak(const std::string& text) {
  static const char* log = SDL_getenv("TTYD_SPEECH_LOG");
  if (log != nullptr) {
    if (FILE* f = portFopen(log, "ab")) {
      std::fprintf(f, "%s\n", text.c_str());
      std::fclose(f);
    }
    return;
  }
  say(text);
}

std::string sLastMenu;

bool on(int atLeast) { return sMode != nullptr && *sMode >= atLeast; }

// Read text aloud on (turned on, or on at the start) with no voice to read with: said over the picture,
// where the page's row is (and again if one that was there stops starting: Linux's spd-say)
void watch(int) {
  static bool sToldNone; // (said once while it stays on)
  if (!on(kMessages) || SDL_getenv("TTYD_SPEECH_LOG") != nullptr) {
    sToldNone = false;
    return;
  }
  if (!sToldNone && !haveVoice()) {
    sToldNone = true;
    std::printf("[speech] no voice found\n");
    std::fflush(stdout);
    portSaveStateNotice("No voice found: install one in the system's speech settings (Linux: speech-dispatcher).");
  }
}

const bool sAdded = [] {
  sMode = portSettingsDeclare("tts", kOff, kOff, kAll);
  sRate = portSettingsDeclare("tts_rate", 0, -5, 5);
  portSettingsAddChoice("Accessibility", "Read text aloud",
                        "The system's voice reads the message windows as each page shows, and with \"and menus\" the "
                        "Start menu's help, the Settings tab, the battle menus and the game's lists as you move "
                        "through them. In the game's language when the system has a voice for it.",
                        0, "tts", kChoices, 3, "text to speech tts read aloud voice narrator screen reader blind");
  portSettingsAddSlider("Accessibility", "Reading speed", "How fast the voice reads.", 0, "tts_rate", -5, 5, "%+d",
                        "text to speech tts rate speed voice");
  portSettingsAddOverlay(watch, nullptr); // "No voice found"
  // the message windows' pages as they show (message_text.cpp); a closed window cuts off its reading
  msgtext::listen([](const void*, const std::string& words, const char*, bool) {
                    if (on(kMessages)) {
                      speak(words);
                    }
                  },
                  [](const void*) {
                    if (on(kMessages)) {
                      stop();
                    }
                  });
  return true;
}();

} // namespace

extern "C" void portSpeechMenu(const char* text) {
  if (!on(kAll) || text == nullptr) {
    return;
  }
  const std::string words = msgtext::plain(text);
  if (words.empty() || words == sLastMenu) {
    return;
  }
  sLastMenu = words;
  speak(words);
}

