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
#include "port/settings.h"
#include "port/speech.h"
#include "../message_text.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
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

#elif defined(__APPLE__)
float appleRate() { return std::clamp(1.f + float(*sRate) * 0.15f, 0.3f, 1.8f); }
void say(const std::string& text) {
  portSpeechAppleSay(text.data(), int(text.size()), japanese() ? 1 : 0, language(), appleRate());
}
void stop() { portSpeechAppleStop(); }

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

#elif !defined(__EMSCRIPTEN__)
// Linux: speech-dispatcher's spd-say (no shell: its arguments as they are)
std::vector<pid_t> sChildren;

void reap() {
  sChildren.erase(std::remove_if(sChildren.begin(), sChildren.end(),
                                 [](pid_t p) { return waitpid(p, nullptr, WNOHANG) != 0; }),
                  sChildren.end());
}

void run(std::vector<std::string> args) {
  std::vector<char*> argv;
  for (std::string& a : args) {
    argv.push_back(a.data());
  }
  argv.push_back(nullptr);
  pid_t pid = 0;
  if (posix_spawnp(&pid, "spd-say", nullptr, nullptr, argv.data(), environ) == 0) {
    sChildren.push_back(pid);
  }
  reap();
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

void say(const std::string& text) {
  run({"spd-say", "-C"});
  run({"spd-say", "-l", language(), "-r", std::to_string(std::clamp(*sRate * 20, -100, 100)), "--", utf8(text)});
}
void stop() { run({"spd-say", "-C"}); }

#else
void say(const std::string&) {}
void stop() {}
#endif

// what's said: to the voice, or ($TTYD_SPEECH_LOG=<file>, tests) a line each in the file, no voice
void speak(const std::string& text) {
  static const char* log = SDL_getenv("TTYD_SPEECH_LOG");
  if (log != nullptr) {
    if (FILE* f = std::fopen(log, "ab")) {
      std::fprintf(f, "%s\n", text.c_str());
      std::fclose(f);
    }
    return;
  }
  say(text);
}

std::string sLastMenu;

bool on(int atLeast) { return sMode != nullptr && *sMode >= atLeast; }

const bool sAdded = [] {
  sMode = portSettingsDeclare("tts", kOff, kOff, kAll);
  sRate = portSettingsDeclare("tts_rate", 0, -5, 5);
  portSettingsAddChoice("Accessibility", "Read text aloud",
                        "The system's voice reads the message windows as each page shows, and with \"and menus\" the "
                        "Start menu's help and the Settings tab as you move through them. In the game's language when "
                        "the system has a voice for it.",
                        0, "tts", kChoices, 3, "text to speech tts read aloud voice narrator screen reader blind");
  portSettingsAddSlider("Accessibility", "Reading speed", nullptr, 0, "tts_rate", -5, 5, "%+d",
                        "text to speech tts rate speed voice");
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

