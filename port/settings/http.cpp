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
#elif defined(__EMSCRIPTEN__)
#include <emscripten.h>
#elif !(defined(__APPLE__) && TARGET_OS_IPHONE)
#include <sys/wait.h>
#include <unistd.h>
#endif

#if defined(__APPLE__) && TARGET_OS_IPHONE
// port/apple/http_apple.m: data is called on the session's queue for each
// chunk, total 0 while unknown; nonzero from it continues.
extern "C" int portHttpAppleGet(const char* url, const char* accept,
                                int (*data)(void* user, const void* bytes, size_t size, unsigned long long total),
                                void* user, char* error, size_t errorSize);
// a POST: the answer's body whatever its status, the status in *status
extern "C" int portHttpApplePost(const char* url, const void* body, size_t bodySize, const char* contentType,
                                 int (*data)(void* user, const void* bytes, size_t size, unsigned long long total),
                                 void* user, int* status, char* error, size_t errorSize);
#endif

namespace port_http {

namespace {

// A POST's body and type, and the answer's status (its body is taken whatever the status)
struct Post {
  const std::string& body;
  std::string contentType;
  mutable int status = 0;
};

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

bool backend(const std::string& url, const char* accept, Transfer& t, std::string& error, const Post* post) {
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
  HINTERNET request = connect ? WinHttpOpenRequest(connect, post != nullptr ? L"POST" : L"GET", object.c_str(),
                                                   nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                                   WINHTTP_FLAG_SECURE)
                              : nullptr;
  if (request != nullptr) {
    WinHttpSetTimeouts(request, 15000, 15000, 15000, post != nullptr ? 120000 : 30000);
    std::wstring headers = accept != nullptr ? L"Accept: " + widen(accept) + L"\r\n" : std::wstring();
    if (post != nullptr) {
      headers += L"Content-Type: " + widen(post->contentType) + L"\r\n";
    }
    DWORD status = 0, size = sizeof(status);
    const DWORD sent = post != nullptr ? DWORD(post->body.size()) : 0;
    if (!WinHttpSendRequest(request, headers.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : headers.c_str(), DWORD(-1L),
                            post != nullptr ? const_cast<char*>(post->body.data()) : WINHTTP_NO_REQUEST_DATA, sent,
                            sent, 0) ||
        !WinHttpReceiveResponse(request, nullptr) ||
        !WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                             WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX)) {
      error = "no answer (network error " + std::to_string(GetLastError()) + ")";
    } else if (status != 200 && post == nullptr) {
      error = "the server answered " + std::to_string(status);
    } else {
      if (post != nullptr) {
        post->status = int(status);
      }
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

bool backend(const std::string& url, const char* accept, Transfer& t, std::string& error, const Post* post) {
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
  bool ok = false;
  if (post != nullptr) {
    jmethodID send = env->GetStaticMethodID(cls, "httpPost", "(Ljava/lang/String;[BLjava/lang/String;J)I");
    if (send != nullptr && !env->ExceptionCheck()) {
      jstring jurl = env->NewStringUTF(url.c_str());
      jstring jtype = env->NewStringUTF(post->contentType.c_str());
      jbyteArray jbody = env->NewByteArray(static_cast<jsize>(post->body.size()));
      env->SetByteArrayRegion(jbody, 0, static_cast<jsize>(post->body.size()),
                              reinterpret_cast<const jbyte*>(post->body.data()));
      const jint status = env->CallStaticIntMethod(cls, send, jurl, jbody, jtype,
                                                   static_cast<jlong>(reinterpret_cast<intptr_t>(&t)));
      if (env->ExceptionCheck() || status < 0) {
        error = "no answer";
      } else {
        post->status = int(status);
        ok = true;
      }
      env->DeleteLocalRef(jurl);
      env->DeleteLocalRef(jtype);
      env->DeleteLocalRef(jbody);
    } else {
      error = "this build's Java side has no uploads";
    }
    if (env->ExceptionCheck()) {
      env->ExceptionClear();
    }
    env->DeleteLocalRef(cls);
    env->DeleteLocalRef(activity);
    return ok;
  }
  jmethodID get = env->GetStaticMethodID(cls, "httpGet", "(Ljava/lang/String;Ljava/lang/String;J)Ljava/lang/String;");
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
bool backend(const std::string& url, const char* accept, Transfer& t, std::string& error, const Post* post) {
  char why[256] = "";
  const auto data = [](void* user, const void* bytes, size_t size, unsigned long long total) -> int {
    auto* transfer = static_cast<Transfer*>(user);
    transfer->setTotal(total);
    return transfer->take(static_cast<const uint8_t*>(bytes), size) ? 1 : 0;
  };
  int status = 0;
  const int ok = post != nullptr ? portHttpApplePost(url.c_str(), post->body.data(), post->body.size(),
                                                     post->contentType.c_str(), data, &t, &status, why, sizeof(why))
                                 : portHttpAppleGet(url.c_str(), accept, data, &t, why, sizeof(why));
  if (post != nullptr) {
    post->status = status;
  }
  if (!ok) {
    error = why;
  }
  return ok != 0;
}

#elif defined(__EMSCRIPTEN__)
// The browser's fetch, started here and waited for a frame at a time (portWebTaskYield: the work
// runs in a fiber of its own between frames, port/src/web/web_later.cpp). The page's origin is
// another site than the server's, so the server has to allow it (CORS; the relay does).
EM_JS(int, webHttpStart, (const char* url, const char* accept, const void* body, int bodyLen, const char* type), {
  const req = { done: false, status: 0, body: null, at: 0, error: "" };
  const all = Module.papercutHttp || (Module.papercutHttp = { next: 1, reqs: {} }); // (not ??=: a trigraph to C++)
  const id = all.next++;
  all.reqs[id] = req;
  const opts = { headers: {} };
  if (accept) opts.headers["Accept"] = UTF8ToString(accept);
  if (body) {
    opts.method = "POST";
    opts.body = HEAPU8.slice(body, body + bodyLen);
    opts.headers["Content-Type"] = UTF8ToString(type);
  }
  fetch(UTF8ToString(url), opts)
    .then(async (r) => { req.status = r.status; req.body = new Uint8Array(await r.arrayBuffer()); })
    .catch((e) => { req.error = "the server can't be reached from the browser"; })
    .finally(() => { req.done = true; });
  return id;
});
EM_JS(int, webHttpDone, (int id), { return Module.papercutHttp.reqs[id].done ? 1 : 0; });
EM_JS(int, webHttpStatus, (int id), { return Module.papercutHttp.reqs[id].status; });
EM_JS(double, webHttpSize, (int id), { const b = Module.papercutHttp.reqs[id].body; return b ? b.length : -1; });
EM_JS(void, webHttpError, (int id, char* out, int cap), { stringToUTF8(Module.papercutHttp.reqs[id].error, out, cap); });
// the next piece of the body into out: how many bytes
EM_JS(int, webHttpRead, (int id, void* out, int cap), {
  const r = Module.papercutHttp.reqs[id];
  const n = Math.min(cap, r.body.length - r.at);
  HEAPU8.set(r.body.subarray(r.at, r.at + n), out);
  r.at += n;
  return n;
});
EM_JS(void, webHttpEnd, (int id), { delete Module.papercutHttp.reqs[id]; });
extern "C" int portWebTaskYield(void);

bool backend(const std::string& url, const char* accept, Transfer& t, std::string& error, const Post* post) {
  const int id = webHttpStart(url.c_str(), accept, post != nullptr ? post->body.data() : nullptr,
                              post != nullptr ? int(post->body.size()) : 0,
                              post != nullptr ? post->contentType.c_str() : nullptr);
  while (!webHttpDone(id)) {
    if (!portWebTaskYield()) {
      emscripten_sleep(10); // (not from a task: the game waits)
    }
  }
  const int status = webHttpStatus(id);
  const double size = webHttpSize(id);
  bool ok = size >= 0 && (post != nullptr || status == 200);
  if (size < 0) {
    char why[128];
    webHttpError(id, why, sizeof(why));
    error = why;
  } else if (!ok) {
    error = status == 404 ? "the server answered with an error (not found?)" : "the server answered with an error";
  } else {
    if (post != nullptr) {
      post->status = status;
    }
    t.setTotal(uint64_t(size));
    static uint8_t chunk[65536];
    int got;
    while (ok && (got = webHttpRead(id, chunk, sizeof(chunk))) > 0) {
      ok = t.take(chunk, size_t(got));
    }
  }
  webHttpEnd(id);
  return ok;
}

#else
// curl, which macOS has and Linux desktops almost always do; it writes the
// body to the pipe and its own errors nowhere. The address was checked to
// have no quote in it (validUrl).
// A POST: the body from a file of its own (curl reads it), the answer whatever its
// status, which curl writes after it on a line of its own (-w).
bool postBackend(const std::string& url, Transfer& t, std::string& error, const Post& post) {
  const char* dir = std::getenv("TMPDIR");
  std::string path = std::string(dir != nullptr && dir[0] != '\0' ? dir : "/tmp") + "/papercut-post-XXXXXX";
  const int fd = mkstemp(path.data());
  if (fd < 0) {
    error = "can't write the upload";
    return false;
  }
  const bool written = write(fd, post.body.data(), post.body.size()) == ssize_t(post.body.size());
  close(fd);
  if (!written || path.find('\'') !=std::string::npos) {
    unlink(path.c_str());
    error = "can't write the upload";
    return false;
  }
  const std::string command = "curl -sS --proto =https --connect-timeout 15 --max-time 120 -A PaperCut -X POST "
                              "-H 'Content-Type: " + post.contentType + "' --data-binary @'" + path +
                              "' -w '\\n%{http_code}' '" + url + "' 2>/dev/null";
  FILE* pipe = popen(command.c_str(), "r");
  if (pipe == nullptr) {
    unlink(path.c_str());
    error = "can't run curl";
    return false;
  }
  std::string answer;
  static thread_local char chunk[65536];
  size_t got;
  while ((got = std::fread(chunk, 1, sizeof(chunk), pipe)) > 0 && answer.size() < (8u << 20)) {
    answer.append(chunk, got);
  }
  const int exit = pclose(pipe);
  unlink(path.c_str());
  const size_t nl = answer.rfind('\n');
  const int status = nl != std::string::npos ? std::atoi(answer.c_str() + nl + 1) : 0;
  if (exit != 0 || status <= 0) {
    error = "no answer";
    return false;
  }
  post.status = status;
  return t.take(reinterpret_cast<const uint8_t*>(answer.data()), nl);
}

bool backend(const std::string& url, const char* accept, Transfer& t, std::string& error, const Post* post) {
  if (post != nullptr) {
    return postBackend(url, t, error, *post);
  }
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
  const bool ok = backend(url, accept, t, why, nullptr);
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

bool post(const std::string& url, const std::string& body, const char* contentType, std::vector<uint8_t>& out,
          int& status, uint64_t maxBytes, std::string& error) {
  out.clear();
  status = 0;
  if (!validUrl(url)) {
    error = "not an https:// address";
    return false;
  }
  const Sink sink = [&](const uint8_t* data, size_t size) {
    out.insert(out.end(), data, data + size);
    return true;
  };
  Transfer t{sink, maxBytes, nullptr};
  const Post p{body, contentType != nullptr ? contentType : "application/octet-stream"};
  std::string why;
  const bool ok = backend(url, nullptr, t, why, &p);
  if (t.tooBig) {
    error = "larger than expected";
  } else if (!ok || p.status <= 0) {
    error = why.empty() ? "no answer" : why;
  } else {
    status = p.status;
    return true;
  }
  return false;
}

} // namespace port_http
