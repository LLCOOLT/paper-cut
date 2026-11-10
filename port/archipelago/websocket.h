// A WebSocket client for text messages (port/archipelago): Windows through
// WinHTTP (ws:// and wss://), elsewhere over a socket (ws:// only, for now).
// connect, receive: blocking; send may come from another thread than receive.
#pragma once

#include <memory>
#include <string>

namespace port_ap {

class WebSocket {
public:
  virtual ~WebSocket() = default;
  // url: ws://host:port/path or wss://...; false and why on failure
  virtual bool connect(const std::string& url, std::string& error) = 0;
  virtual bool send(const std::string& text) = 0;
  // the next text message; false once closed or failed
  virtual bool receive(std::string& text) = 0;
  // from another thread than receive's (the game's): ends a receive in progress (now, or once the server
  // answers or the receive times out) without waiting, and without freeing what that receive may still be
  // using; close() (or the destructor, once nobody uses the socket) frees it
  virtual void interrupt() = 0;
  // the full close, by the socket's last user only: may wait on the server (WinHTTP's close handshake)
  virtual void close() = 0;

  static std::unique_ptr<WebSocket> create();
  // whether wss:// works here
  static bool secureSupported();
};

} // namespace port_ap
