// Plain HTTP on the local network, over the sockets online co-op uses
// (port/include/port/online_sock.h), for what https:// (port/include/port/
// http.h) can't reach: another device's one-time save server (port/settings/
// features/save_transfer.cpp), and the update check's test release on this
// computer ($TTYD_UPDATE_API, port/settings/features/update_check.cpp).
// HTTP/1.0, one request a connection, no TLS: only for addresses on the
// player's own network. port/settings/lan_http.cpp.
#pragma once

#include "port/http.h"
#include <cstdint>
#include <string>
#include <vector>

namespace port_lan {

// "http://host:port/path" split; false for anything else (https, no host,
// spaces or quotes).
bool splitUrl(const std::string& url, std::string& host, int& port, std::string& path);

// The body of url (http://), chunk by chunk, into sink: as port_http::get.
bool get(const std::string& url, const port_http::Sink& sink, uint64_t maxBytes, port_http::Progress* progress,
         std::string& error);

// The whole body of url into out.
bool fetch(const std::string& url, std::vector<uint8_t>& out, uint64_t maxBytes, std::string& error);

// n random bytes from the system's generator (BCryptGenRandom, arc4random,
// /dev/urandom); false when there is none.
bool randomBytes(uint8_t* out, size_t n);

} // namespace port_lan
