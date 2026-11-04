// port/include/port/http.h: an https:// GET with each platform's own
// client. Every backend hands the body over in chunks to one Transfer,
// which enforces the size limit, reports progress and notices a cancel, so
// the backends only move bytes.
#include "port/http.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#ifdef __APPLE__
#include <TargetConditionals.h>
#endif
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")
#elif defined(__ANDROID__)
#include <SDL3/SDL.h>
#include <jni.h>
#include <mutex>
#elif !defined(__EMSCRIPTEN__) && !(defined(__APPLE__) && TARGET_OS_IPHONE)
#include <sys/wait.h>
#endif

#if defined(__APPLE__) && TARGET_OS_IPHONE
// port/apple/http_apple.m: data is called on the session's queue for each
// chunk, total 0 while unknown; nonzero from it continues.
extern "C" int portHttpAppleGet(const char* url, const char* accept,
                                int (*data)(void* user, const void* bytes, size_t size, unsigned long long total),
                                void* user, char* error, size_t errorSize);
#endif

namespace port_http {

namespace {

struct Transfer {
  const Sink& sink;
  uint64_t maxBytes;
  Progress* progress;
  uint64_t got = 0;
  bool cancelled = false, tooBig = false, writeFailed = false;

  void setTotal(uint64_t total) {
    if (progress != nullptr && total > 0) {
      progress->total = total;
    }
  }
  // A chunk: false stops the transfer (and says why in the flags).
  bool take(const uint8_t* data, size_t size) {
    if (progress != nullptr && progress->cancel) {
      cancelled = true;
      return false;
    }
    if (got + size > maxBytes) {
      tooBig = true;
      return false;
    }
    if (size > 0 && !sink(data, size)) {
      writeFailed = true;
      return false;
    }
    got += size;
    if (progress != nullptr) {
      progress->done = got;
    }
    return true;
  }
};

#ifdef _WIN32
std::wstring widen(const std::string& text) {
  const int n = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
  std::wstring out(n > 1 ? size_t(n - 1) : 0, L'\0');
  if (n > 1) {
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, out.data(), n);
  }
  return out;
}

bool backend(const std::string& url, const char* accept, Transfer& t, std::string& error) {
  const std::wstring wurl = widen(url);
  // null buffers with nonzero lengths: the parts point into wurl
  URL_COMPONENTS parts{};
  parts.dwStructSize = sizeof(parts);
  parts.dwHostNameLength = DWORD(-1);
  parts.dwUrlPathLength = DWORD(-1);
  parts.dwExtraInfoLength = DWORD(-1);
  if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &parts) || parts.nScheme != INTERNET_SCHEME_HTTPS) {
    error = "not an https:// address";
    return false;
  }
  const std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
  std::wstring object(parts.lpszUrlPath, parts.dwUrlPathLength);
  if (parts.lpszExtraInfo != nullptr) {
    object.append(parts.lpszExtraInfo, parts.dwExtraInfoLength); // ?query
  }
  if (object.empty()) {
    object = L"/";
  }
  bool ok = false;
  // Redirects are followed, but never from https to http (WinHTTP's default policy).
  HINTERNET session = WinHttpOpen(L"PaperCut", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                                  WINHTTP_NO_PROXY_BYPASS, 0);
  HINTERNET connect = session ? WinHttpConnect(session, host.c_str(), parts.nPort, 0) : nullptr;
  HINTERNET request = connect ? WinHttpOpenRequest(connect, L"GET", object.c_str(), nullptr, WINHTTP_NO_REFERER,
                                                   WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE)
                              : nullptr;
  if (request != nullptr) {
    WinHttpSetTimeouts(request, 15000, 15000, 15000, 30000);
    const std::wstring headers = accept != nullptr ? L"Accept: " + widen(accept) + L"\r\n" : std::wstring();
    DWORD status = 0, size = sizeof(status);
    if (!WinHttpSendRequest(request, headers.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : headers.c_str(), DWORD(-1L),
                            WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(request, nullptr) ||
        !WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                             WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX)) {
      error = "no answer (network error " + std::to_string(GetLastError()) + ")";
    } else if (status != 200) {
      error = "the server answered " + std::to_string(status);
    } else {
      DWORD length = 0;
      size = sizeof(length);
      if (WinHttpQueryHeaders(request, WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER,
                              WINHTTP_HEADER_NAME_BY_INDEX, &length, &size, WINHTTP_NO_HEADER_INDEX)) {
        t.setTotal(length);
      }
      static thread_local uint8_t chunk[65536];
      DWORD got = 0;
      ok = true;
      while (true) {
        if (!WinHttpReadData(request, chunk, sizeof(chunk), &got)) {
          error = "the transfer broke off (network error " + std::to_string(GetLastError()) + ")";
          ok = false;
          break;
        }
        if (got == 0) {
          break;
        }
        if (!t.take(chunk, got)) {
          ok = false;
          break;
        }
      }
    }
  } else {
    error = "can't connect (network error " + std::to_string(GetLastError()) + ")";
  }
  if (request != nullptr) {
    WinHttpCloseHandle(request);
  }
  if (connect != nullptr) {
    WinHttpCloseHandle(connect);
  }
  if (session != nullptr) {
    WinHttpCloseHandle(session);
  }
  return ok;
}

#elif defined(__ANDROID__)
// TtydActivity.httpData: a chunk of the body for the Transfer at handle.
jboolean JNICALL httpData(JNIEnv* env, jclass, jlong handle, jbyteArray data, jint length, jlong total) {
  auto* t = reinterpret_cast<Transfer*>(static_cast<intptr_t>(handle));
  t->setTotal(total > 0 ? uint64_t(total) : 0);
  if (length <= 0) {
    return JNI_TRUE;
  }
  static thread_local std::vector<uint8_t> buffer;
  buffer.resize(size_t(length));
  env->GetByteArrayRegion(data, 0, length, reinterpret_cast<jbyte*>(buffer.data()));
  return t->take(buffer.data(), buffer.size()) ? JNI_TRUE : JNI_FALSE;
}

bool backend(const std::string& url, const char* accept, Transfer& t, std::string& error) {
  // SDL attaches this thread to the VM; the activity's class (not FindClass,
  // which on a thread of our own sees only the system's classes) has the
  // Java side.
  auto* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
  auto activity = static_cast<jobject>(SDL_GetAndroidActivity());
  if (env == nullptr || activity == nullptr) {
    error = "Java isn't reachable";
    return false;
  }
  jclass cls = env->GetObjectClass(activity);
  static std::once_flag sRegistered;
  std::call_once(sRegistered, [&] {
    // (char*, not const, in some jni.h)
    const JNINativeMethod method{const_cast<char*>("httpData"), const_cast<char*>("(J[BIJ)Z"),
                                 reinterpret_cast<void*>(httpData)};
    env->RegisterNatives(cls, &method, 1);
  });
  jmethodID get = env->GetStaticMethodID(cls, "httpGet", "(Ljava/lang/String;Ljava/lang/String;J)Ljava/lang/String;");
  bool ok = false;
  if (get != nullptr && !env->ExceptionCheck()) {
    jstring jurl = env->NewStringUTF(url.c_str());
    jstring jaccept = accept != nullptr ? env->NewStringUTF(accept) : nullptr;
    auto result = static_cast<jstring>(
        env->CallStaticObjectMethod(cls, get, jurl, jaccept, static_cast<jlong>(reinterpret_cast<intptr_t>(&t))));
    if (env->ExceptionCheck()) {
      error = "the download failed in Java";
    } else if (result != nullptr) {
      const char* why = env->GetStringUTFChars(result, nullptr);
      error = why != nullptr ? why : "the download failed";
      env->ReleaseStringUTFChars(result, why);
      env->DeleteLocalRef(result);
    } else {
      ok = true;
    }
    env->DeleteLocalRef(jurl);
    if (jaccept != nullptr) {
      env->DeleteLocalRef(jaccept);
    }
  } else {
    error = "this build's Java side has no downloads";
  }
  if (env->ExceptionCheck()) {
    env->ExceptionClear();
  }
  env->DeleteLocalRef(cls);
  env->DeleteLocalRef(activity);
  return ok;
}

#elif defined(__APPLE__) && TARGET_OS_IPHONE
bool backend(const std::string& url, const char* accept, Transfer& t, std::string& error) {
  char why[256] = "";
  const int ok = portHttpAppleGet(
      url.c_str(), accept,
      [](void* user, const void* bytes, size_t size, unsigned long long total) -> int {
        auto* transfer = static_cast<Transfer*>(user);
        transfer->setTotal(total);
        return transfer->take(static_cast<const uint8_t*>(bytes), size) ? 1 : 0;
      },
      &t, why, sizeof(why));
  if (!ok) {
    error = why;
  }
  return ok != 0;
}

#elif defined(__EMSCRIPTEN__)
bool backend(const std::string&, const char*, Transfer&, std::string& error) {
  error = "downloads aren't available in the browser build";
  return false;
}

#else
// curl, which macOS has and Linux desktops almost always do; it writes the
// body to the pipe and its own errors nowhere. The address was checked to
// have no quote in it (validUrl).
bool backend(const std::string& url, const char* accept, Transfer& t, std::string& error) {
  std::string command = "curl -fsSL --proto =https --proto-redir =https --connect-timeout 15 "
                        "--speed-limit 1 --speed-time 30 -A PaperCut";
  if (accept != nullptr) {
    command += std::string(" -H 'Accept: ") + accept + "'";
  }
  command += " '" + url + "' 2>/dev/null";
  FILE* pipe = popen(command.c_str(), "r");
  if (pipe == nullptr) {
    error = "can't run curl";
    return false;
  }
  static thread_local uint8_t chunk[65536];
  size_t got;
  bool stopped = false;
  while ((got = std::fread(chunk, 1, sizeof(chunk), pipe)) > 0) {
    if (!t.take(chunk, got)) {
      stopped = true;
      break;
    }
  }
  const int status = pclose(pipe);
  if (stopped) {
    return false;
  }
  if (status == 0) {
    return true;
  }
  const int code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
  switch (code) {
  case 6: error = "the server's name can't be found (offline?)"; break;
  case 7: error = "can't connect to the server"; break;
  case 22: error = "the server answered with an error (not found?)"; break;
  case 28: error = "the server took too long"; break;
  case 127: error = "curl is not installed"; break;
  default: error = "curl failed (" + std::to_string(code) + ")"; break;
  }
  return false;
}
#endif

} // namespace

bool validUrl(const std::string& url) {
  constexpr const char* kScheme = "https://";
  if (url.rfind(kScheme, 0) != 0 || url.size() <= std::strlen(kScheme) || url.size() > 2048 ||
      url[std::strlen(kScheme)] == '/') {
    return false;
  }
  for (const char c : url) {
    if (c <= ' ' || c >= 0x7F || c == '\'' || c == '"' || c == '\\' || c == '`' || c == '<' || c == '>') {
      return false;
    }
  }
  return true;
}

bool get(const std::string& url, const Sink& sink, uint64_t maxBytes, Progress* progress, std::string& error,
         const char* accept) {
  if (!validUrl(url)) {
    error = "not an https:// address";
    return false;
  }
  Transfer t{sink, maxBytes, progress};
  std::string why;
  const bool ok = backend(url, accept, t, why);
  if (t.cancelled) {
    error = "cancelled";
  } else if (t.tooBig) {
    error = "larger than expected";
  } else if (t.writeFailed) {
    error = "can't be written to the disk (is it full?)";
  } else if (!ok) {
    error = why.empty() ? "no answer" : why;
  } else {
    return true;
  }
  return false;
}

bool fetch(const std::string& url, std::vector<uint8_t>& out, uint64_t maxBytes, std::string& error,
           const char* accept) {
  out.clear();
  return get(
      url,
      [&](const uint8_t* data, size_t size) {
        out.insert(out.end(), data, data + size);
        return true;
      },
      maxBytes, nullptr, error, accept);
}

} // namespace port_http
