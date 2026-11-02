// port/archipelago/websocket.h: WinHTTP's WebSocket on Windows; elsewhere
// RFC 6455's framing over a TCP socket (no TLS yet).
#include "websocket.h"

#include <atomic>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <random>
#include <string>
#include <vector>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <winhttp.h>
#else
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace port_ap {

namespace {

struct Url {
  bool secure = false;
  std::string host, path = "/";
  int port = 0;
};

bool parseUrl(const std::string& url, Url& out, std::string& error) {
  std::string rest = url;
  if (rest.rfind("wss://", 0) == 0) {
    out.secure = true;
    rest = rest.substr(6);
  } else if (rest.rfind("ws://", 0) == 0) {
    rest = rest.substr(5);
  } else {
    error = "not a ws:// or wss:// address";
    return false;
  }
  const size_t slash = rest.find('/');
  if (slash != std::string::npos) {
    out.path = rest.substr(slash);
    rest = rest.substr(0, slash);
  }
  const size_t colon = rest.rfind(':');
  if (colon != std::string::npos) {
    out.port = std::atoi(rest.substr(colon + 1).c_str());
    rest = rest.substr(0, colon);
  }
  out.host = rest;
  if (out.port == 0) {
    out.port = out.secure ? 443 : 80;
  }
  if (out.host.empty()) {
    error = "no host";
    return false;
  }
  return true;
}

#ifdef _WIN32

std::wstring wide(const std::string& s) {
  std::wstring w(s.size(), L'\0');
  const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), w.data(), int(w.size()));
  w.resize(n > 0 ? n : 0);
  return w;
}

std::string lastError(const std::string& what) {
  return what + " failed (WinHTTP error " + std::to_string(GetLastError()) + ")";
}

class WinHttpSocket final : public WebSocket {
public:
  ~WinHttpSocket() override { close(); }

  bool connect(const std::string& url, std::string& error) override {
    Url u;
    if (!parseUrl(url, u, error)) {
      return false;
    }
    mSession = WinHttpOpen(L"PaperCut", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                           WINHTTP_NO_PROXY_BYPASS, 0);
    if (!mSession) {
      error = lastError("WinHttpOpen");
      return false;
    }
    WinHttpSetTimeouts(mSession, 10000, 10000, 10000, 0);
    mConnection = WinHttpConnect(mSession, wide(u.host).c_str(), INTERNET_PORT(u.port), 0);
    if (!mConnection) {
      error = lastError("connecting to " + u.host);
      return false;
    }
    HINTERNET request = WinHttpOpenRequest(mConnection, L"GET", wide(u.path).c_str(), nullptr, WINHTTP_NO_REFERER,
                                           WINHTTP_DEFAULT_ACCEPT_TYPES, u.secure ? WINHTTP_FLAG_SECURE : 0);
    if (!request) {
      error = lastError("WinHttpOpenRequest");
      return false;
    }
    if (!WinHttpSetOption(request, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, nullptr, 0) ||
        !WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, nullptr, 0, 0, 0) ||
        !WinHttpReceiveResponse(request, nullptr)) {
      error = lastError("connecting to " + u.host + ":" + std::to_string(u.port) + ": the request");
      WinHttpCloseHandle(request);
      return false;
    }
    DWORD status = 0, size = sizeof(status);
    WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                        &status, &size, WINHTTP_NO_HEADER_INDEX);
    if (status != 101) {
      error = "the server answered HTTP " + std::to_string(status) + ", not a WebSocket";
      WinHttpCloseHandle(request);
      return false;
    }
    mSocket = WinHttpWebSocketCompleteUpgrade(request, 0);
    WinHttpCloseHandle(request);
    if (!mSocket) {
      error = lastError("WinHttpWebSocketCompleteUpgrade");
      return false;
    }
    return true;
  }

  bool send(const std::string& text) override {
    std::lock_guard lock(mSendLock);
    return mSocket && WinHttpWebSocketSend(mSocket, WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE,
                                           const_cast<char*>(text.data()), DWORD(text.size())) == NO_ERROR;
  }

  bool receive(std::string& text) override {
    text.clear();
    std::vector<char> buf(64 * 1024);
    for (;;) {
      if (!mSocket) {
        return false;
      }
      DWORD read = 0;
      WINHTTP_WEB_SOCKET_BUFFER_TYPE type;
      if (WinHttpWebSocketReceive(mSocket, buf.data(), DWORD(buf.size()), &read, &type) != NO_ERROR) {
        return false;
      }
      if (type == WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE) {
        return false;
      }
      text.append(buf.data(), read);
      if (type == WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE || type == WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE) {
        return true;
      }
    }
  }

  void close() override {
    if (mSocket) {
      WinHttpWebSocketClose(mSocket, WINHTTP_WEB_SOCKET_SUCCESS_CLOSE_STATUS, nullptr, 0);
      WinHttpCloseHandle(mSocket);
      mSocket = nullptr;
    }
    if (mConnection) {
      WinHttpCloseHandle(mConnection);
      mConnection = nullptr;
    }
    if (mSession) {
      WinHttpCloseHandle(mSession);
      mSession = nullptr;
    }
  }

private:
  HINTERNET mSession = nullptr, mConnection = nullptr, mSocket = nullptr;
  std::mutex mSendLock;
};

#else

class PlainSocket final : public WebSocket {
public:
  ~PlainSocket() override { close(); }

  bool connect(const std::string& url, std::string& error) override {
    Url u;
    if (!parseUrl(url, u, error)) {
      return false;
    }
    if (u.secure) {
      error = "wss:// isn't supported on this platform yet";
      return false;
    }
    addrinfo hints{};
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* res = nullptr;
    if (getaddrinfo(u.host.c_str(), std::to_string(u.port).c_str(), &hints, &res) != 0 || res == nullptr) {
      error = "can't find " + u.host;
      return false;
    }
    for (addrinfo* a = res; a != nullptr; a = a->ai_next) {
      const int fd = socket(a->ai_family, a->ai_socktype, a->ai_protocol);
      if (fd >= 0 && ::connect(fd, a->ai_addr, a->ai_addrlen) == 0) {
        mFd = fd;
        break;
      }
      if (fd >= 0) {
        ::close(fd);
      }
    }
    freeaddrinfo(res);
    if (mFd < 0) {
      error = "can't connect to " + u.host + ":" + std::to_string(u.port);
      return false;
    }
    // the upgrade (RFC 6455 4.1); the server's key isn't checked
    unsigned char key[16];
    std::random_device rd;
    for (unsigned char& k : key) {
      k = static_cast<unsigned char>(rd());
    }
    const std::string request = "GET " + u.path + " HTTP/1.1\r\nHost: " + u.host + ":" + std::to_string(u.port) +
                                "\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: " +
                                base64(key, sizeof(key)) + "\r\nSec-WebSocket-Version: 13\r\n\r\n";
    if (!writeAll(request.data(), request.size())) {
      error = "the request failed";
      return false;
    }
    std::string head;
    char c;
    while (head.size() < 8192 && head.find("\r\n\r\n") == std::string::npos) {
      if (::recv(mFd, &c, 1, 0) != 1) {
        error = "no answer";
        return false;
      }
      head += c;
    }
    if (head.compare(0, 12, "HTTP/1.1 101") != 0) {
      error = "the server answered " + head.substr(0, head.find('\r')) + ", not a WebSocket";
      return false;
    }
    return true;
  }

  bool send(const std::string& text) override { return sendFrame(0x1, text.data(), text.size()); }

  bool receive(std::string& text) override {
    text.clear();
    for (;;) {
      unsigned char h[2];
      if (!readAll(h, 2)) {
        return false;
      }
      const int op = h[0] & 0x0F;
      const bool fin = (h[0] & 0x80) != 0;
      uint64_t len = h[1] & 0x7F;
      if (len == 126) {
        unsigned char e[2];
        if (!readAll(e, 2)) return false;
        len = (uint64_t(e[0]) << 8) | e[1];
      } else if (len == 127) {
        unsigned char e[8];
        if (!readAll(e, 8)) return false;
        len = 0;
        for (unsigned char b : e) len = (len << 8) | b;
      }
      unsigned char mask[4] = {};
      const bool masked = (h[1] & 0x80) != 0;
      if (masked && !readAll(mask, 4)) {
        return false;
      }
      std::string payload(size_t(len), '\0');
      if (len && !readAll(payload.data(), payload.size())) {
        return false;
      }
      if (masked) {
        for (size_t i = 0; i < payload.size(); i++) payload[i] ^= mask[i & 3];
      }
      if (op == 0x8) { // close
        return false;
      }
      if (op == 0x9) { // ping
        sendFrame(0xA, payload.data(), payload.size());
        continue;
      }
      if (op == 0xA) {
        continue;
      }
      text += payload;
      if (fin) {
        return true;
      }
    }
  }

  void close() override {
    const int fd = mFd.exchange(-1);
    if (fd >= 0) {
      ::shutdown(fd, SHUT_RDWR);
      ::close(fd);
    }
  }

private:
  static std::string base64(const unsigned char* p, size_t n) {
    static const char* k = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    for (size_t i = 0; i < n; i += 3) {
      const uint32_t v = (uint32_t(p[i]) << 16) | (i + 1 < n ? uint32_t(p[i + 1]) << 8 : 0) | (i + 2 < n ? p[i + 2] : 0);
      out += k[(v >> 18) & 63];
      out += k[(v >> 12) & 63];
      out += i + 1 < n ? k[(v >> 6) & 63] : '=';
      out += i + 2 < n ? k[v & 63] : '=';
    }
    return out;
  }
  bool writeAll(const void* data, size_t n) {
    const char* p = static_cast<const char*>(data);
    while (n) {
      const ssize_t w = ::send(mFd, p, n, 0);
      if (w <= 0) return false;
      p += w;
      n -= size_t(w);
    }
    return true;
  }
  bool readAll(void* data, size_t n) {
    char* p = static_cast<char*>(data);
    while (n) {
      const ssize_t r = ::recv(mFd, p, n, 0);
      if (r <= 0) return false;
      p += r;
      n -= size_t(r);
    }
    return true;
  }
  // a client's frames are masked (RFC 6455 5.3)
  bool sendFrame(int op, const char* data, size_t n) {
    std::lock_guard lock(mSendLock);
    std::vector<unsigned char> f;
    f.push_back(static_cast<unsigned char>(0x80 | op));
    if (n < 126) {
      f.push_back(static_cast<unsigned char>(0x80 | n));
    } else if (n < 65536) {
      f.insert(f.end(), {0x80 | 126, static_cast<unsigned char>(n >> 8), static_cast<unsigned char>(n)});
    } else {
      f.push_back(0x80 | 127);
      for (int i = 7; i >= 0; i--) f.push_back(static_cast<unsigned char>(uint64_t(n) >> (i * 8)));
    }
    unsigned char mask[4];
    std::random_device rd;
    for (unsigned char& m : mask) m = static_cast<unsigned char>(rd());
    f.insert(f.end(), mask, mask + 4);
    for (size_t i = 0; i < n; i++) f.push_back(static_cast<unsigned char>(data[i] ^ mask[i & 3]));
    return mFd >= 0 && writeAll(f.data(), f.size());
  }

  std::atomic<int> mFd{-1};
  std::mutex mSendLock;
};

#endif

} // namespace

std::unique_ptr<WebSocket> WebSocket::create() {
#ifdef _WIN32
  return std::make_unique<WinHttpSocket>();
#else
  return std::make_unique<PlainSocket>();
#endif
}

bool WebSocket::secureSupported() {
#ifdef _WIN32
  return true;
#else
  return false;
#endif
}

} // namespace port_ap
