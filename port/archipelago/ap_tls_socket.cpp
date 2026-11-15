// port/archipelago/websocket.h's socket of its own: RFC 6455 over a TCP socket,
// through Mbed TLS for wss:// (the build's own Mbed TLS and the root
// certificates online play ships, port/src/common/online_roots.inc). Every
// platform but the browser; Windows keeps WinHTTP unless $TTYD_AP_SOCKET is
// "mbedtls" (tests). One thread reads (receive); a send only queues the text,
// and the reading thread writes it between reads, so the TLS context is only
// ever that thread's. TLS 1.2 at most: online play's relay link may be doing
// TLS on its own thread at the same time, and Mbed TLS's PSA (TLS 1.3's keys)
// isn't built for two threads (port/src/common/online_ws_pc.c).
#include "websocket.h"

#ifdef PORT_AP_MBEDTLS

#include "port/online_sock.h"

#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/error.h>
#include <mbedtls/net_sockets.h>
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <random>
#include <string>
#include <vector>

#include "../src/common/online_roots.inc"

extern "C" int portOnlinePsaInit(void); // port/include/port/online_link.h (C, with the game's types)

namespace port_ap {

namespace {

constexpr int kConnectMs = 10000;
constexpr int kHandshakeMs = 10000;
constexpr int kPollMs = 50;

class TlsSocket final : public WebSocket {
public:
  ~TlsSocket() override { close(); }

  bool connect(const std::string& url, std::string& error) override {
    if (!parseUrl(url, error) || !onlineNetStart() || !connectTcp(error)) {
      if (error.empty()) error = "no network";
      return false;
    }
    if (mTls && !startTls(error)) {
      return false;
    }
    return upgrade(error);
  }

  bool send(const std::string& text) override {
    std::lock_guard lock(mOutLock);
    mOut.push_back(text);
    return mOpen.load();
  }

  bool receive(std::string& text) override {
    text.clear();
    std::string message;
    for (;;) {
      if (mInterrupted.load() || !flush()) {
        return false;
      }
      // the frames already read
      for (;;) {
        int op = 0;
        bool fin = false;
        std::string payload;
        const int got = takeFrame(op, fin, payload);
        if (got < 0) {
          return false;
        }
        if (got == 0) {
          break;
        }
        if (op == 0x8) { // close
          mOpen = false;
          return false;
        }
        if (op == 0x9) { // ping: its pong
          writeFrame(0xA, payload.data(), payload.size());
          continue;
        }
        if (op == 0xA) {
          continue;
        }
        message += payload;
        if (fin) {
          text = std::move(message);
          return true;
        }
      }
      if (readSome(kPollMs) < 0) {
        mOpen = false;
        return false;
      }
    }
  }

  void interrupt() override { mInterrupted = true; }

  void close() override {
    if (mSock != BAD_SOCKET) {
      if (mTls && mTlsUp) {
        mbedtls_ssl_close_notify(&mSsl);
      }
      ::shutdown(mSock, 2);
      ::closeSocket(mSock); // (the global one: close() here is this class's, and the macro is close on POSIX)
      mSock = BAD_SOCKET;
    }
    if (mTlsInit) {
      mbedtls_ssl_free(&mSsl);
      mbedtls_ssl_config_free(&mConf);
      mbedtls_x509_crt_free(&mCa);
      mbedtls_ctr_drbg_free(&mDrbg);
      mbedtls_entropy_free(&mEntropy);
      mTlsInit = false;
      mTlsUp = false;
    }
    mOpen = false;
  }

private:
  bool parseUrl(const std::string& url, std::string& error) {
    std::string rest = url;
    if (rest.rfind("wss://", 0) == 0) {
      mTls = true;
      rest = rest.substr(6);
    } else if (rest.rfind("ws://", 0) == 0) {
      rest = rest.substr(5);
    } else {
      error = "not a ws:// or wss:// address";
      return false;
    }
    const size_t slash = rest.find('/');
    if (slash != std::string::npos) {
      mPath = rest.substr(slash);
      rest = rest.substr(0, slash);
    }
    const size_t colon = rest.rfind(':');
    mPort = mTls ? 443 : 80;
    if (colon != std::string::npos) {
      mPort = std::atoi(rest.substr(colon + 1).c_str());
      rest = rest.substr(0, colon);
    }
    mHost = rest;
    if (mHost.empty() || mPort <= 0) {
      error = "no host";
      return false;
    }
    return true;
  }

  bool connectTcp(std::string& error) {
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* found = nullptr;
    if (getaddrinfo(mHost.c_str(), std::to_string(mPort).c_str(), &hints, &found) != 0 || found == nullptr) {
      error = "can't find " + mHost;
      return false;
    }
    for (addrinfo* a = found; a != nullptr && mSock == BAD_SOCKET && !mInterrupted.load(); a = a->ai_next) {
      Socket s = socket(a->ai_family, a->ai_socktype, a->ai_protocol);
      if (s == BAD_SOCKET) continue;
      onlineNonBlocking(s, 1);
      int done = ::connect(s, a->ai_addr, (int)a->ai_addrlen) == 0 ? 1 : 0;
      for (int waited = 0; done == 0 && WOULD_BLOCK() && waited < kConnectMs && !mInterrupted.load(); waited += 100) {
        fd_set w, e;
        timeval wait = {0, 100000};
        FD_ZERO(&w);
        FD_ZERO(&e);
        FD_SET(s, &w);
        FD_SET(s, &e);
        if (select((int)s + 1, nullptr, &w, &e, &wait) > 0) {
          int soError = 0;
          socklen_t len = sizeof(soError);
          done = !FD_ISSET(s, &e) && getsockopt(s, SOL_SOCKET, SO_ERROR, (char*)&soError, &len) == 0 && soError == 0
                     ? 1
                     : -1;
        }
      }
      if (done == 1) {
        onlineNonBlocking(s, 0);
        int one = 1;
        setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (const char*)&one, sizeof(one));
        mSock = s;
      } else {
        ::closeSocket(s);
      }
    }
    freeaddrinfo(found);
    if (mSock == BAD_SOCKET) {
      error = "can't connect to " + mHost + ":" + std::to_string(mPort);
      return false;
    }
    return true;
  }

  static int bioSend(void* ctx, const unsigned char* buf, size_t len) {
    auto* self = static_cast<TlsSocket*>(ctx);
    const int n = (int)::send(self->mSock, (const char*)buf, (int)len, SEND_FLAGS);
    return n >= 0 ? n : MBEDTLS_ERR_NET_SEND_FAILED;
  }

  static int bioRecv(void* ctx, unsigned char* buf, size_t len) {
    auto* self = static_cast<TlsSocket*>(ctx);
    if (!onlineReadable(self->mSock, self->mTlsUp ? kPollMs : kHandshakeMs)) {
      return MBEDTLS_ERR_SSL_WANT_READ;
    }
    const int n = (int)::recv(self->mSock, (char*)buf, (int)len, 0);
    return n > 0 ? n : n == 0 ? MBEDTLS_ERR_NET_CONN_RESET : MBEDTLS_ERR_NET_RECV_FAILED;
  }

  bool startTls(std::string& error) {
    portOnlinePsaInit(); // (once, for any part of Mbed TLS that asks; this link stays on TLS 1.2)
    mbedtls_ssl_init(&mSsl);
    mbedtls_ssl_config_init(&mConf);
    mbedtls_x509_crt_init(&mCa);
    mbedtls_entropy_init(&mEntropy);
    mbedtls_ctr_drbg_init(&mDrbg);
    mTlsInit = true;
    if (mbedtls_ctr_drbg_seed(&mDrbg, mbedtls_entropy_func, &mEntropy, (const unsigned char*)"papercut-ap", 11) != 0 ||
        mbedtls_x509_crt_parse(&mCa, (const unsigned char*)kOnlineRoots, sizeof(kOnlineRoots)) < 0 ||
        mbedtls_ssl_config_defaults(&mConf, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM,
                                    MBEDTLS_SSL_PRESET_DEFAULT) != 0) {
      error = "a secure connection couldn't be set up";
      return false;
    }
    mbedtls_ssl_conf_max_tls_version(&mConf, MBEDTLS_SSL_VERSION_TLS1_2);
    mbedtls_ssl_conf_authmode(&mConf, MBEDTLS_SSL_VERIFY_REQUIRED);
    mbedtls_ssl_conf_ca_chain(&mConf, &mCa, nullptr);
    mbedtls_ssl_conf_rng(&mConf, mbedtls_ctr_drbg_random, &mDrbg);
    if (mbedtls_ssl_setup(&mSsl, &mConf) != 0 || mbedtls_ssl_set_hostname(&mSsl, mHost.c_str()) != 0) {
      error = "a secure connection couldn't be set up";
      return false;
    }
    mbedtls_ssl_set_bio(&mSsl, this, bioSend, bioRecv, nullptr);
    int ret;
    int tries = 0;
    while ((ret = mbedtls_ssl_handshake(&mSsl)) != 0) {
      if ((ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE) && ++tries < 2 &&
          !mInterrupted.load()) {
        continue;
      }
      char why[120];
      mbedtls_strerror(ret, why, sizeof(why));
      error = ret == MBEDTLS_ERR_X509_CERT_VERIFY_FAILED
                  ? std::string("the server's certificate wasn't recognised (is the clock right?)")
                  : "a secure connection failed: " + std::string(why);
      return false;
    }
    mTlsUp = true;
    return true;
  }

  // the bytes out, all of them
  bool writeAll(const unsigned char* p, size_t len) {
    while (len > 0) {
      int n;
      if (mTls) {
        n = mbedtls_ssl_write(&mSsl, p, len);
        if (n == MBEDTLS_ERR_SSL_WANT_WRITE || n == MBEDTLS_ERR_SSL_WANT_READ) continue;
      } else {
        n = (int)::send(mSock, (const char*)p, (int)len, SEND_FLAGS);
      }
      if (n <= 0) return false;
      p += n;
      len -= size_t(n);
    }
    return true;
  }

  // what's there now (or within ms) into mRx; 0 nothing, -1 closed
  int readSome(int ms) {
    unsigned char tmp[16384];
    int n;
    if (mTls) {
      if (mbedtls_ssl_get_bytes_avail(&mSsl) == 0 && !onlineReadable(mSock, ms)) return 0;
      n = mbedtls_ssl_read(&mSsl, tmp, sizeof(tmp));
      if (n == MBEDTLS_ERR_SSL_WANT_READ || n == MBEDTLS_ERR_SSL_WANT_WRITE) return 0;
    } else {
      if (!onlineReadable(mSock, ms)) return 0;
      n = (int)::recv(mSock, (char*)tmp, sizeof(tmp), 0);
    }
    if (n <= 0) return -1;
    mRx.append(reinterpret_cast<const char*>(tmp), size_t(n));
    return n;
  }

  // the HTTP upgrade (RFC 6455 4.1); the server's accept key isn't checked
  bool upgrade(std::string& error) {
    unsigned char key[16];
    std::random_device rd;
    for (unsigned char& k : key) k = static_cast<unsigned char>(rd());
    const bool defaultPort = mPort == (mTls ? 443 : 80);
    const std::string request = "GET " + mPath + " HTTP/1.1\r\nHost: " + mHost +
                                (defaultPort ? "" : ":" + std::to_string(mPort)) +
                                "\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: " + base64(key, 16) +
                                "\r\nSec-WebSocket-Version: 13\r\nUser-Agent: PaperCut\r\n\r\n";
    if (!writeAll(reinterpret_cast<const unsigned char*>(request.data()), request.size())) {
      error = "the request failed";
      return false;
    }
    size_t end;
    for (int waited = 0; (end = mRx.find("\r\n\r\n")) == std::string::npos; waited += kPollMs) {
      if (waited > kHandshakeMs || mRx.size() > 16384 || mInterrupted.load() || readSome(kPollMs) < 0) {
        error = "no answer";
        return false;
      }
    }
    const std::string head = mRx.substr(0, end);
    mRx.erase(0, end + 4); // (a frame right behind the answer stays read)
    if (head.compare(0, 12, "HTTP/1.1 101") != 0) {
      error = "the server answered " + head.substr(0, head.find('\r')) + ", not a WebSocket";
      return false;
    }
    mOpen = true;
    return true;
  }

  // the queued texts out, as frames (the reading thread's)
  bool flush() {
    std::vector<std::string> out;
    {
      std::lock_guard lock(mOutLock);
      out.swap(mOut);
    }
    for (const std::string& t : out) {
      if (!writeFrame(0x1, t.data(), t.size())) {
        mOpen = false;
        return false;
      }
    }
    return true;
  }

  // a client's frames are masked (RFC 6455 5.3)
  bool writeFrame(int op, const char* data, size_t n) {
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
    return writeAll(f.data(), f.size());
  }

  // one whole frame from mRx: 1 taken, 0 not all there yet, -1 too big
  int takeFrame(int& op, bool& fin, std::string& payload) {
    const auto* p = reinterpret_cast<const unsigned char*>(mRx.data());
    const size_t have = mRx.size();
    if (have < 2) return 0;
    op = p[0] & 0x0F;
    fin = (p[0] & 0x80) != 0;
    const bool masked = (p[1] & 0x80) != 0;
    uint64_t len = p[1] & 0x7F;
    size_t at = 2;
    if (len == 126) {
      if (have < 4) return 0;
      len = (uint64_t(p[2]) << 8) | p[3];
      at = 4;
    } else if (len == 127) {
      if (have < 10) return 0;
      len = 0;
      for (int i = 0; i < 8; i++) len = (len << 8) | p[2 + i];
      at = 10;
    }
    if (len > (64u << 20)) return -1; // (a data package is a few MB at most)
    unsigned char mask[4] = {};
    if (masked) {
      if (have < at + 4) return 0;
      memcpy(mask, p + at, 4);
      at += 4;
    }
    if (have < at + len) return 0;
    payload.assign(mRx, at, size_t(len));
    if (masked) {
      for (size_t i = 0; i < payload.size(); i++) payload[i] ^= mask[i & 3];
    }
    mRx.erase(0, at + size_t(len));
    return 1;
  }

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

  std::string mHost, mPath = "/";
  int mPort = 0;
  bool mTls = false, mTlsInit = false, mTlsUp = false;
  Socket mSock = BAD_SOCKET;
  mbedtls_ssl_context mSsl;
  mbedtls_ssl_config mConf;
  mbedtls_x509_crt mCa;
  mbedtls_entropy_context mEntropy;
  mbedtls_ctr_drbg_context mDrbg;
  std::string mRx;
  std::mutex mOutLock;
  std::vector<std::string> mOut;
  std::atomic<bool> mOpen{false}, mInterrupted{false};
};

} // namespace

std::unique_ptr<WebSocket> WebSocket::createTls() { return std::make_unique<TlsSocket>(); }

} // namespace port_ap

#else

namespace port_ap {
std::unique_ptr<WebSocket> WebSocket::createTls() { return nullptr; }
} // namespace port_ap

#endif
