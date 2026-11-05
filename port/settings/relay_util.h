// The relay's web side, as the features that talk to it share it (bingo, daily, raid, boards: port/settings/features).
#pragma once

#include "port/online.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace portrelay {

// the first wss:// relay online uses, as https:// ("" when there is none)
inline std::string relayBase() {
  for (int i = 0; portOnlineRelayAt(i) != nullptr; i++) {
    const std::string relay = portOnlineRelayAt(i);
    if (relay.compare(0, 6, "wss://") == 0) {
      return "https://" + relay.substr(6);
    }
  }
  return "";
}

// text for a URL's query: letters, digits and - _ . as they are, the rest %XX
inline std::string percent(const std::string& s) {
  std::string out;
  for (unsigned char c : s) {
    if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '-' || c == '_' ||
        c == '.') {
      out += static_cast<char>(c);
    } else {
      char hex[4];
      std::snprintf(hex, sizeof(hex), "%%%02X", c);
      out += hex;
    }
  }
  return out;
}

// percent's text back (a % not followed by two hex digits kept as it is)
inline std::string unpercent(const std::string& s) {
  std::string out;
  for (size_t i = 0; i < s.size(); i++) {
    if (s[i] == '%' && i + 2 < s.size() && std::isxdigit(static_cast<unsigned char>(s[i + 1])) &&
        std::isxdigit(static_cast<unsigned char>(s[i + 2]))) {
      out += static_cast<char>(std::strtol(s.substr(i + 1, 2).c_str(), nullptr, 16));
      i += 2;
    } else {
      out += s[i];
    }
  }
  return out;
}

} // namespace portrelay
