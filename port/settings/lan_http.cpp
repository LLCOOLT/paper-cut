// port/include/port/lan_http.h: a plain HTTP/1.0 GET on the local network.
#ifdef _WIN32
#define NOMINMAX
#endif
#include "port/online_sock.h"
#include "port/lan_http.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#ifdef _WIN32
#include <bcrypt.h>
#pragma comment(lib, "bcrypt.lib")
#elif defined(__APPLE__)
#include <stdlib.h> // arc4random_buf
#endif

namespace port_lan {

bool splitUrl(const std::string& url, std::string& host, int& port, std::string& path) {
  constexpr const char* kScheme = "http://";
  if (url.rfind(kScheme, 0) != 0 || url.size() > 2048) {
    return false;
  }
  for (const char c : url) {
    if (c <= ' ' || c >= 0x7F || c == '\'' || c == '"' || c == '\\' || c == '`' || c == '<' || c == '>') {
      return false;
    }
  }
  const size_t start = std::strlen(kScheme);
  size_t slash = url.find('/', start);
  if (slash == std::string::npos) {
    slash = url.size();
  }
  std::string hostPort = url.substr(start, slash - start);
  path = slash < url.size() ? url.substr(slash) : "/";
  port = 80;
  const size_t colon = hostPort.rfind(':');
  if (colon != std::string::npos) {
    const std::string digits = hostPort.substr(colon + 1);
    if (digits.empty() || digits.size() > 5 || digits.find_first_not_of("0123456789") != std::string::npos) {
      return false;
    }
    port = std::atoi(digits.c_str());
    hostPort.resize(colon);
  }
  host = hostPort;
  return !host.empty() && port > 0 && port < 65536;
}

namespace {

void setTimeout(Socket s, int seconds) {
#ifdef _WIN32
  DWORD ms = DWORD(seconds * 1000);
  setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&ms), sizeof(ms));
  setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&ms), sizeof(ms));
#else
  timeval tv{seconds, 0};
  setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
  setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
#endif
}

// A connection to host:port within seconds; BAD_SOCKET and why otherwise.
Socket connectTo(const std::string& host, int port, int seconds, std::string& error) {
  addrinfo hints{}, *found = nullptr;
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  const std::string service = std::to_string(port);
  if (getaddrinfo(host.c_str(), service.c_str(), &hints, &found) != 0 || found == nullptr) {
    error = "the address " + host + " can't be found";
    return BAD_SOCKET;
  }
  Socket s = BAD_SOCKET;
  for (addrinfo* a = found; a != nullptr && s == BAD_SOCKET; a = a->ai_next) {
    s = socket(a->ai_family, a->ai_socktype, a->ai_protocol);
    if (s == BAD_SOCKET) {
      continue;
    }
    // without blocking, so an address that never answers gives up in time
    onlineNonBlocking(s, 1);
    if (connect(s, a->ai_addr, int(a->ai_addrlen)) != 0 && !WOULD_BLOCK()) {
      closeSocket(s);
      s = BAD_SOCKET;
      continue;
    }
    fd_set w, e;
    FD_ZERO(&w);
    FD_ZERO(&e);
    FD_SET(s, &w);
    FD_SET(s, &e);
    timeval wait{seconds, 0};
    int soError = 0;
    socklen_t len = sizeof(soError);
    if (select(int(s) + 1, nullptr, &w, &e, &wait) <= 0 || FD_ISSET(s, &e) ||
        getsockopt(s, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&soError), &len) != 0 || soError != 0) {
      closeSocket(s);
      s = BAD_SOCKET;
      continue;
    }
    onlineNonBlocking(s, 0);
  }
  freeaddrinfo(found);
  if (s == BAD_SOCKET) {
    error = "can't connect to " + host + ":" + service + " (is the other device still sending, on this network?)";
  }
  return s;
}

} // namespace

bool get(const std::string& url, const port_http::Sink& sink, uint64_t maxBytes, port_http::Progress* progress,
         std::string& error) {
  std::string host, path;
  int port = 0;
  if (!splitUrl(url, host, port, path)) {
    error = "not an http:// address";
    return false;
  }
  if (!onlineNetStart()) {
    error = "no network";
    return false;
  }
  Socket s = connectTo(host, port, 8, error);
  if (s == BAD_SOCKET) {
    return false;
  }
  setTimeout(s, 15);
  const std::string request = "GET " + path + " HTTP/1.0\r\nHost: " + host + ":" + std::to_string(port) +
                              "\r\nUser-Agent: PaperCut\r\nConnection: close\r\n\r\n";
  bool ok = send(s, request.data(), int(request.size()), 0) == int(request.size());
  if (!ok) {
    error = "the request couldn't be sent";
  }
  // the head (status line and headers), then the body as it comes
  std::string head;
  bool inBody = false;
  uint64_t got = 0;
  static thread_local char chunk[65536];
  while (ok) {
    const int n = int(recv(s, chunk, sizeof(chunk), 0));
    if (n == 0) {
      break;
    }
    if (n < 0) {
      error = "the transfer broke off";
      ok = false;
      break;
    }
    const char* body = chunk;
    size_t size = size_t(n);
    if (!inBody) {
      head.append(chunk, size_t(n));
      const size_t end = head.find("\r\n\r\n");
      if (end == std::string::npos) {
        if (head.size() > 16384) {
          error = "not an HTTP answer";
          ok = false;
        }
        continue;
      }
      inBody = true;
      int status = 0;
      if (std::sscanf(head.c_str(), "HTTP/%*d.%*d %d", &status) != 1) {
        error = "not an HTTP answer";
        ok = false;
        break;
      }
      if (status != 200) {
        error = status == 404 ? "the other device has no such address (was the address typed right?)"
                              : "the other device answered " + std::to_string(status);
        ok = false;
        break;
      }
      // Content-Length, for the progress
      for (size_t at = 0; at < end;) {
        const size_t eol = head.find("\r\n", at);
        std::string line = head.substr(at, eol - at);
        for (char& c : line) {
          c = char(c >= 'A' && c <= 'Z' ? c + 32 : c);
        }
        if (line.rfind("content-length:", 0) == 0 && progress != nullptr) {
          progress->total = std::strtoull(line.c_str() + 15, nullptr, 10);
        }
        at = eol + 2;
      }
      const size_t bodyAt = end + 4;
      const std::string rest = head.substr(bodyAt);
      head.clear();
      if (rest.empty()) {
        continue;
      }
      std::memcpy(chunk, rest.data(), rest.size()); // (rest came from this chunk: it fits)
      body = chunk;
      size = rest.size();
    }
    if (progress != nullptr && progress->cancel) {
      error = "cancelled";
      ok = false;
      break;
    }
    if (got + size > maxBytes) {
      error = "larger than expected";
      ok = false;
      break;
    }
    if (!sink(reinterpret_cast<const uint8_t*>(body), size)) {
      error = "can't be written to the disk (is it full?)";
      ok = false;
      break;
    }
    got += size;
    if (progress != nullptr) {
      progress->done = got;
    }
  }
  if (ok && !inBody) {
    error = "no answer";
    ok = false;
  }
  closeSocket(s);
  return ok;
}

bool fetch(const std::string& url, std::vector<uint8_t>& out, uint64_t maxBytes, std::string& error) {
  out.clear();
  return get(
      url,
      [&](const uint8_t* data, size_t size) {
        out.insert(out.end(), data, data + size);
        return true;
      },
      maxBytes, nullptr, error);
}

bool randomBytes(uint8_t* out, size_t n) {
#ifdef _WIN32
  return BCryptGenRandom(nullptr, out, ULONG(n), BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0;
#elif defined(__APPLE__)
  arc4random_buf(out, n);
  return true;
#else
  FILE* f = std::fopen("/dev/urandom", "rb");
  if (f == nullptr) {
    return false;
  }
  const bool ok = std::fread(out, 1, n, f) == n;
  std::fclose(f);
  return ok;
#endif
}

} // namespace port_lan
