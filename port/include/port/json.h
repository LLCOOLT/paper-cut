// A small JSON reader and writer (objects, arrays, strings, numbers,
// literals): mod.json (port/mods/mods.cpp), Archipelago's messages
// (port/archipelago).
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace port_json {


struct JsonField;

struct Json {
  enum Type { Null, Bool, Number, String, Array, Object } type = Null;
  bool b = false;
  double n = 0;
  std::string s;
  std::vector<Json> items;
  // (not std::pair<std::string, Json>: a pair of a type still being defined
  // is one libstdc++ refuses to instantiate)
  std::vector<JsonField> fields;

  const Json* get(const char* key) const;
  std::string str(const char* key) const;
};

// An object's member: a key and its value (first, second, as a pair's)
struct JsonField {
  std::string first;
  Json second;
};

inline const Json* Json::get(const char* key) const {
  for (const auto& [k, v] : fields) {
    if (k == key) {
      return &v;
    }
  }
  return nullptr;
}

inline std::string Json::str(const char* key) const {
  const Json* v = get(key);
  return v != nullptr && v->type == String ? v->s : std::string();
}

struct JsonParser {
  const char* p;
  const char* end;
  std::string error;

  void ws() {
    while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) {
      p++;
    }
  }
  bool fail(const char* what) {
    if (error.empty()) {
      error = what;
    }
    return false;
  }
  static void utf8(std::string& out, unsigned c) {
    if (c < 0x80) {
      out += char(c);
    } else if (c < 0x800) {
      out += char(0xC0 | (c >> 6));
      out += char(0x80 | (c & 0x3F));
    } else if (c < 0x10000) {
      out += char(0xE0 | (c >> 12));
      out += char(0x80 | ((c >> 6) & 0x3F));
      out += char(0x80 | (c & 0x3F));
    } else {
      out += char(0xF0 | (c >> 18));
      out += char(0x80 | ((c >> 12) & 0x3F));
      out += char(0x80 | ((c >> 6) & 0x3F));
      out += char(0x80 | (c & 0x3F));
    }
  }
  bool hex4(unsigned& c) {
    c = 0; // (before the length check too: the analyzer doesn't always inline fail() to see it returns false)
    if (end - p < 4) {
      return fail("bad \\u escape");
    }
    for (int i = 0; i < 4; i++, p++) {
      const char h = *p;
      c <<= 4;
      if (h >= '0' && h <= '9') {
        c |= h - '0';
      } else if (h >= 'a' && h <= 'f') {
        c |= h - 'a' + 10;
      } else if (h >= 'A' && h <= 'F') {
        c |= h - 'A' + 10;
      } else {
        return fail("bad \\u escape");
      }
    }
    return true;
  }
  bool string(std::string& out) {
    p++; // "
    while (p < end && *p != '"') {
      if (*p != '\\') {
        out += *p++;
        continue;
      }
      if (++p >= end) {
        break;
      }
      const char e = *p++;
      switch (e) {
      case 'n': out += '\n'; break;
      case 't': out += '\t'; break;
      case 'r': out += '\r'; break;
      case 'b': out += '\b'; break;
      case 'f': out += '\f'; break;
      case 'u': {
        unsigned c;
        if (!hex4(c)) {
          return false;
        }
        if (c >= 0xD800 && c < 0xDC00 && end - p >= 6 && p[0] == '\\' && p[1] == 'u') {
          unsigned lo;
          p += 2;
          if (!hex4(lo)) {
            return false;
          }
          if (lo >= 0xDC00 && lo < 0xE000) {
            c = 0x10000 + ((c - 0xD800) << 10) + (lo - 0xDC00);
          } else {
            // a high surrogate with no low one after it: U+FFFD for it, the second escape as itself
            // (combining them gave a wrong character or one past U+10FFFF)
            utf8(out, 0xFFFD);
            c = lo;
          }
        }
        utf8(out, c);
        break;
      }
      default: out += e; break;
      }
    }
    if (p >= end) {
      return fail("unterminated string");
    }
    p++;
    return true;
  }
  bool value(Json& v, int depth) {
    if (depth > 64) {
      return fail("nested too deeply");
    }
    ws();
    if (p >= end) {
      return fail("unexpected end");
    }
    if (*p == '{') {
      v.type = Json::Object;
      p++;
      ws();
      if (p < end && *p == '}') {
        p++;
        return true;
      }
      for (;;) {
        ws();
        if (p >= end || *p != '"') {
          return fail("expected a key");
        }
        std::string key;
        if (!string(key)) {
          return false;
        }
        ws();
        if (p >= end || *p != ':') {
          return fail("expected ':'");
        }
        p++;
        v.fields.push_back({std::move(key), Json()});
        if (!value(v.fields.back().second, depth + 1)) {
          return false;
        }
        ws();
        if (p < end && *p == ',') {
          p++;
          continue;
        }
        if (p < end && *p == '}') {
          p++;
          return true;
        }
        return fail("expected ',' or '}'");
      }
    }
    if (*p == '[') {
      v.type = Json::Array;
      p++;
      ws();
      if (p < end && *p == ']') {
        p++;
        return true;
      }
      for (;;) {
        v.items.emplace_back();
        if (!value(v.items.back(), depth + 1)) {
          return false;
        }
        ws();
        if (p < end && *p == ',') {
          p++;
          continue;
        }
        if (p < end && *p == ']') {
          p++;
          return true;
        }
        return fail("expected ',' or ']'");
      }
    }
    if (*p == '"') {
      v.type = Json::String;
      return string(v.s);
    }
    for (const char* lit : {"true", "false", "null"}) {
      const size_t len = strlen(lit);
      if (size_t(end - p) >= len && memcmp(p, lit, len) == 0) {
        p += len;
        v.type = lit[0] == 'n' ? Json::Null : Json::Bool;
        v.b = lit[0] == 't';
        return true;
      }
    }
    char* after = nullptr;
    const std::string rest(p, std::min<size_t>(end - p, 64));
    v.n = strtod(rest.c_str(), &after);
    if (after == rest.c_str()) {
      return fail("unexpected character");
    }
    // (strtod also reads nan, inf and 1e999, which JSON has no such numbers for: a NaN would
    // pass every range check after it)
    if (!std::isfinite(v.n)) {
      return fail("a number out of range");
    }
    v.type = Json::Number;
    p += after - rest.c_str();
    return true;
  }
};

inline bool parseJson(const std::vector<uint8_t>& text, Json& out, std::string& error) {
  JsonParser parser{reinterpret_cast<const char*>(text.data()), reinterpret_cast<const char*>(text.data()) + text.size(),
                    {}};
  if (text.size() >= 3 && text[0] == 0xEF && text[1] == 0xBB && text[2] == 0xBF) {
    parser.p += 3; // UTF-8 BOM
  }
  if (!parser.value(out, 0)) {
    error = parser.error;
    return false;
  }
  parser.ws();
  if (parser.p != parser.end) {
    error = "text after the end";
    return false;
  }
  return true;
}

// --- writing ---

inline void quote(std::string& out, const std::string& s) {
  out += '"';
  for (const unsigned char c : s) {
    if (c == '"' || c == '\\') {
      out += '\\';
      out += char(c);
    } else if (c < 0x20) {
      char buf[8];
      snprintf(buf, sizeof(buf), "\\u%04x", c);
      out += buf;
    } else {
      out += char(c);
    }
  }
  out += '"';
}

inline void write(std::string& out, const Json& v) {
  switch (v.type) {
  case Json::Null: out += "null"; break;
  case Json::Bool: out += v.b ? "true" : "false"; break;
  case Json::Number: {
    char buf[32];
    if (std::fabs(v.n) < 9e18 && v.n == double(int64_t(v.n))) {
      snprintf(buf, sizeof(buf), "%lld", (long long)v.n);
    } else {
      snprintf(buf, sizeof(buf), "%.17g", v.n);
    }
    out += buf;
    break;
  }
  case Json::String: quote(out, v.s); break;
  case Json::Array:
    out += '[';
    for (size_t i = 0; i < v.items.size(); i++) {
      if (i) out += ',';
      write(out, v.items[i]);
    }
    out += ']';
    break;
  case Json::Object:
    out += '{';
    for (size_t i = 0; i < v.fields.size(); i++) {
      if (i) out += ',';
      quote(out, v.fields[i].first);
      out += ':';
      write(out, v.fields[i].second);
    }
    out += '}';
    break;
  }
}

inline std::string dump(const Json& v) {
  std::string out;
  write(out, v);
  return out;
}

// --- building ---

inline Json number(double n) {
  Json v;
  v.type = Json::Number;
  v.n = n;
  return v;
}
inline Json string(std::string s) {
  Json v;
  v.type = Json::String;
  v.s = std::move(s);
  return v;
}
inline Json boolean(bool b) {
  Json v;
  v.type = Json::Bool;
  v.b = b;
  return v;
}
inline Json array(std::vector<Json> items = {}) {
  Json v;
  v.type = Json::Array;
  v.items = std::move(items);
  return v;
}
inline Json object(std::vector<JsonField> fields = {}) {
  Json v;
  v.type = Json::Object;
  v.fields = std::move(fields);
  return v;
}

} // namespace port_json
