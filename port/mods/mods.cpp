// Mods (port/include/port/mods.h, docs/MODDING.md).
//
// A mod is a folder or a .ttydmod/.zip file in <user>/mods holding:
//   mod.json     {"id": "...", "name": "...", "version": "...", "author": "...",
//                 "description": "...", "games": ["G8ME01", "G8MJ01"]}
//   overlay/     files replacing or adding disc files, by disc path
//   textures/    texture replacements under Dolphin's names (tex1_...)
//   messages/    added or replaced messages: *.txt for every language,
//                <LANG>/*.txt for one (JP US DE FR SP IT; EN: the JP build's English)
//   patches/     game data changed by name: "itemDataTable[ITEM_MUSHROOM].sell_price = 10"
// A zip may hold the mod in one top-level folder.
//
// Disc files go through Aurora's DVD overlay, textures through its texture
// replacement (both already in Aurora), messages through msgSearch, patches
// through the reflection table (port/include/port/reflect.h), which also
// keeps each patched place's original bytes to put back.
// <user>/textures holds loose texture packs (a Dolphin pack as it comes),
// below every mod.
//
// Apply keeps what it replaced alive: the game may hold a message pointer or
// an open file of the previous set.
#include "port/mods.h"
#include "port/mod_host.h"
#include "port/reflect.h"

#include <SDL3/SDL.h>
#include <aurora/dvd.h>
#include <dolphin/gx.h> // GXTexObj, for aurora/texture.hpp
#include <aurora/texture.hpp>
#include <zlib.h>

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

// --- mod.json: a small JSON reader (objects, arrays, strings, numbers, literals) ---

struct Json {
  enum Type { Null, Bool, Number, String, Array, Object } type = Null;
  bool b = false;
  double n = 0;
  std::string s;
  std::vector<Json> items;
  std::vector<std::pair<std::string, Json>> fields;

  const Json* get(const char* key) const {
    for (const auto& [k, v] : fields) {
      if (k == key) {
        return &v;
      }
    }
    return nullptr;
  }
  std::string str(const char* key) const {
    const Json* v = get(key);
    return v != nullptr && v->type == String ? v->s : std::string();
  }
};

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
    if (end - p < 4) {
      return fail("bad \\u escape");
    }
    c = 0;
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
          c = 0x10000 + ((c - 0xD800) << 10) + (lo - 0xDC00);
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
        v.fields.emplace_back(std::move(key), Json());
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
    v.type = Json::Number;
    p += after - rest.c_str();
    return true;
  }
};

bool parseJson(const std::vector<uint8_t>& text, Json& out, std::string& error) {
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

// --- files: a folder, or a zip (stored or deflated entries, no zip64) ---

bool readHostFile(const std::string& path, std::vector<uint8_t>& out) {
  size_t size = 0;
  void* data = SDL_LoadFile(path.c_str(), &size);
  if (data == nullptr) {
    return false;
  }
  out.assign(static_cast<uint8_t*>(data), static_cast<uint8_t*>(data) + size);
  SDL_free(data);
  return true;
}

uint16_t le16(const uint8_t* p) { return uint16_t(p[0] | (p[1] << 8)); }
uint32_t le32(const uint8_t* p) { return uint32_t(p[0] | (p[1] << 8) | (p[2] << 16)) | (uint32_t(p[3]) << 24); }

struct ZipEntry {
  uint16_t method;
  uint32_t compressed;
  uint32_t size;
  uint32_t localHeader;
};

class Source {
public:
  // A folder (dir) or a zip file (path).
  static std::unique_ptr<Source> open(const std::string& path, bool isZip, std::string& error) {
    auto src = std::unique_ptr<Source>(new Source);
    src->mPath = path;
    src->mZip = isZip;
    if (isZip ? !src->readZip(error) : !src->readFolder(path, "")) {
      return nullptr;
    }
    // A zip of a folder: the mod is in its one top-level folder.
    if (src->mFiles.count("mod.json") == 0) {
      std::string top;
      bool single = true;
      for (const auto& [name, size] : src->mFiles) {
        const size_t slash = name.find('/');
        const std::string first = slash == std::string::npos ? std::string() : name.substr(0, slash + 1);
        if (first.empty() || (!top.empty() && first != top)) {
          single = false;
          break;
        }
        top = first;
      }
      if (single && !top.empty() && src->mFiles.count(top + "mod.json") != 0) {
        src->mPrefix = top;
      }
    }
    return src;
  }

  // Relative paths under dir ("overlay/"), with their sizes.
  std::vector<std::pair<std::string, uint32_t>> list(const std::string& dir) const {
    std::vector<std::pair<std::string, uint32_t>> out;
    const std::string full = mPrefix + dir;
    for (auto it = mFiles.lower_bound(full); it != mFiles.end() && it->first.compare(0, full.size(), full) == 0; ++it) {
      out.emplace_back(it->first.substr(mPrefix.size()), it->second);
    }
    return out;
  }
  bool has(const std::string& rel) const { return mFiles.count(mPrefix + rel) != 0; }
  uint32_t size(const std::string& rel) const {
    const auto it = mFiles.find(mPrefix + rel);
    return it == mFiles.end() ? 0 : it->second;
  }
  // Thread safe (overlay and texture reads come from Aurora's workers).
  bool read(const std::string& rel, std::vector<uint8_t>& out) const {
    if (!mZip) {
      return readHostFile(mPath + "/" + mPrefix + rel, out);
    }
    const auto it = mEntries.find(mPrefix + rel);
    return it != mEntries.end() && readZipEntry(it->second, out);
  }
  // A folder's file on disk, for streaming it instead of reading it whole.
  std::string hostPath(const std::string& rel) const { return mZip ? std::string() : mPath + "/" + mPrefix + rel; }
  const std::string& path() const { return mPath; }

private:
  std::string mPath;
  std::string mPrefix;
  bool mZip = false;
  std::map<std::string, uint32_t> mFiles; // relative path ('/'), size
  std::unordered_map<std::string, ZipEntry> mEntries;

  bool readFolder(const std::string& root, const std::string& rel) {
    struct Ctx {
      Source* self;
      std::string root;
      std::string rel;
      std::vector<std::string> dirs;
    } ctx{this, root, rel, {}};
    const std::string dir = rel.empty() ? root : root + "/" + rel;
    auto cb = [](void* user, const char* dirname, const char* fname) -> SDL_EnumerationResult {
      auto* c = static_cast<Ctx*>(user);
      const std::string relPath = c->rel.empty() ? std::string(fname) : c->rel + "/" + fname;
      SDL_PathInfo info;
      if (SDL_GetPathInfo((std::string(dirname) + fname).c_str(), &info)) {
        if (info.type == SDL_PATHTYPE_DIRECTORY) {
          c->dirs.push_back(relPath);
        } else if (info.type == SDL_PATHTYPE_FILE && info.size < 0xFFFFFFFFull) {
          c->self->mFiles[relPath] = uint32_t(info.size);
        }
      }
      return SDL_ENUM_CONTINUE;
    };
    if (!SDL_EnumerateDirectory(dir.c_str(), cb, &ctx)) {
      return false;
    }
    for (const std::string& sub : ctx.dirs) {
      readFolder(root, sub);
    }
    return true;
  }

  bool readZip(std::string& error) {
    SDL_IOStream* io = SDL_IOFromFile(mPath.c_str(), "rb");
    if (io == nullptr) {
      error = "can't open the file";
      return false;
    }
    const Sint64 fileSize = SDL_GetIOSize(io);
    // The end-of-central-directory record: in the last 22 + 65535 bytes.
    const Sint64 tail = std::min<Sint64>(fileSize, 22 + 65535);
    std::vector<uint8_t> buf(size_t(tail > 0 ? tail : 0));
    bool ok = tail >= 22 && SDL_SeekIO(io, fileSize - tail, SDL_IO_SEEK_SET) >= 0 &&
              SDL_ReadIO(io, buf.data(), buf.size()) == buf.size();
    Sint64 eocd = -1;
    for (Sint64 i = Sint64(buf.size()) - 22; ok && i >= 0; i--) {
      if (le32(&buf[size_t(i)]) == 0x06054B50) {
        eocd = i;
        break;
      }
    }
    if (eocd < 0) {
      SDL_CloseIO(io);
      error = "not a zip file";
      return false;
    }
    const uint8_t* e = &buf[size_t(eocd)];
    const uint32_t count = le16(e + 10);
    const uint32_t cdSize = le32(e + 12);
    const uint32_t cdOffset = le32(e + 16);
    std::vector<uint8_t> cd(cdSize);
    ok = cdOffset != 0xFFFFFFFF && SDL_SeekIO(io, cdOffset, SDL_IO_SEEK_SET) >= 0 &&
         SDL_ReadIO(io, cd.data(), cd.size()) == cd.size();
    SDL_CloseIO(io);
    if (!ok) {
      error = "zip64 or damaged zip file";
      return false;
    }
    size_t at = 0;
    for (uint32_t i = 0; i < count; i++) {
      if (at + 46 > cd.size() || le32(&cd[at]) != 0x02014B50) {
        error = "damaged zip directory";
        return false;
      }
      const uint8_t* h = &cd[at];
      const uint16_t flags = le16(h + 8);
      ZipEntry entry{le16(h + 10), le32(h + 20), le32(h + 24), le32(h + 42)};
      const uint16_t nameLen = le16(h + 28);
      const size_t next = at + 46 + nameLen + le16(h + 30) + le16(h + 32);
      if (next > cd.size()) {
        error = "damaged zip directory";
        return false;
      }
      std::string name(reinterpret_cast<const char*>(h + 46), nameLen);
      std::replace(name.begin(), name.end(), '\\', '/');
      at = next;
      if (name.empty() || name.back() == '/') {
        continue; // a folder
      }
      if ((flags & 1) != 0 || (entry.method != 0 && entry.method != 8) || entry.size == 0xFFFFFFFF) {
        error = "\"" + name + "\" is encrypted, zip64 or not deflated";
        return false;
      }
      mFiles[name] = entry.size;
      mEntries[name] = entry;
    }
    return true;
  }

  bool readZipEntry(const ZipEntry& entry, std::vector<uint8_t>& out) const {
    SDL_IOStream* io = SDL_IOFromFile(mPath.c_str(), "rb");
    if (io == nullptr) {
      return false;
    }
    uint8_t header[30];
    std::vector<uint8_t> data(entry.compressed);
    bool ok = SDL_SeekIO(io, entry.localHeader, SDL_IO_SEEK_SET) >= 0 && SDL_ReadIO(io, header, 30) == 30 &&
              le32(header) == 0x04034B50 &&
              SDL_SeekIO(io, Sint64(entry.localHeader) + 30 + le16(header + 26) + le16(header + 28), SDL_IO_SEEK_SET) >=
                  0 &&
              SDL_ReadIO(io, data.data(), data.size()) == data.size();
    SDL_CloseIO(io);
    if (!ok) {
      return false;
    }
    if (entry.method == 0) {
      out = std::move(data);
      return true;
    }
    out.resize(entry.size);
    z_stream z{};
    if (inflateInit2(&z, -MAX_WBITS) != Z_OK) {
      return false;
    }
    z.next_in = data.data();
    z.avail_in = uInt(data.size());
    z.next_out = out.data();
    z.avail_out = uInt(out.size());
    const int result = inflate(&z, Z_FINISH);
    inflateEnd(&z);
    return result == Z_STREAM_END && z.total_out == entry.size;
  }
};

// --- the mods ---

struct Option {
  std::string key, label;
  int type = PORT_MOD_OPTION_BOOL;
  int min = 0, max = 1, fallback = 0;
  std::vector<std::string> choices;
  std::vector<const char*> choicePtrs;
  PortModOption info{};
};

struct Mod {
  std::string id, name, version, author, description, display, error;
  std::unique_ptr<Source> source; // NULL: built into the game, no folder
  bool enabled = true;
  PortModInfo info{};
  int files = 0, textures = 0, messages = 0, patches = 0;
  std::string warning; // the first patch that could not be applied
  std::vector<std::unique_ptr<Option>> options;
};

// A mod's code (a library in lib/<platform>/, or built into the game): it
// starts at the game's start and can't stop, so turning it on or off is
// noticed here and takes effect at the next start.
struct Code {
  bool running = false;
  std::string error;
};

std::string sUserPath;
std::string sModsDir;
std::string sDumpDir;
std::string sCachePath;
std::vector<std::unique_ptr<Mod>> sMods; // in order, the first winning
bool sDisabledAll;
bool sStarted; // the mods' code has been started (once, at the game's start)
std::map<std::string, Code> sCode;        // by mod id
std::map<std::string, int> sOptionValues; // "id.key" -> value (mods/options.txt)

// The applied set, with what earlier applies left behind (see the top).
struct OverlayFile {
  const Source* source;
  std::string rel; // in the source: "overlay/..."
};
std::vector<std::unique_ptr<Source>> sRetiredSources;
std::vector<std::unique_ptr<std::vector<OverlayFile>>> sOverlayGenerations;
std::vector<aurora::texture::ReplacementGroup> sTextureGroups;
// language code ("" for all) -> tag -> text in the game's encoding
using Messages = std::unordered_map<std::string, std::unordered_map<std::string, std::string>>;
std::vector<std::unique_ptr<Messages>> sMessageGenerations;
Messages* sMessages;
std::mutex sMessageLock;
// Messages set by mods' code (api->setMessage): above every file's.
std::unordered_map<std::string, const char*> sCodeMessages;
std::deque<std::string> sCodeMessageText;

struct OverlayHandle {
  SDL_IOStream* io = nullptr;
  std::vector<uint8_t> data;
  size_t pos = 0;
};

void* overlayOpen(void* userData) {
  const auto* file = static_cast<const OverlayFile*>(userData);
  auto* handle = new OverlayHandle;
  const std::string host = file->source->hostPath(file->rel);
  if (!host.empty()) {
    handle->io = SDL_IOFromFile(host.c_str(), "rb");
    if (handle->io != nullptr) {
      return handle;
    }
  } else if (file->source->read(file->rel, handle->data)) {
    return handle;
  }
  SDL_Log("mods: can't read %s from %s", file->rel.c_str(), file->source->path().c_str());
  delete handle;
  return nullptr;
}

void overlayClose(void* h) {
  auto* handle = static_cast<OverlayHandle*>(h);
  if (handle->io != nullptr) {
    SDL_CloseIO(handle->io);
  }
  delete handle;
}

int64_t overlayRead(void* h, uint8_t* buf, size_t len) {
  auto* handle = static_cast<OverlayHandle*>(h);
  if (handle->io != nullptr) {
    const size_t n = SDL_ReadIO(handle->io, buf, len);
    return n == 0 && SDL_GetIOStatus(handle->io) == SDL_IO_STATUS_ERROR ? -1 : int64_t(n);
  }
  const size_t n = std::min(len, handle->data.size() - std::min(handle->pos, handle->data.size()));
  memcpy(buf, handle->data.data() + handle->pos, n);
  handle->pos += n;
  return int64_t(n);
}

int64_t overlaySeek(void* h, int64_t offset, int32_t whence) {
  auto* handle = static_cast<OverlayHandle*>(h);
  if (handle->io != nullptr) {
    return SDL_SeekIO(handle->io, offset, whence == 1 ? SDL_IO_SEEK_CUR : whence == 2 ? SDL_IO_SEEK_END : SDL_IO_SEEK_SET);
  }
  const int64_t base = whence == 1 ? int64_t(handle->pos) : whence == 2 ? int64_t(handle->data.size()) : 0;
  if (base + offset < 0) {
    return -1;
  }
  handle->pos = size_t(base + offset);
  return int64_t(handle->pos);
}

bool texturesVirtualRead(void* userData, const char* path, std::vector<uint8_t>& out) {
  return static_cast<const Source*>(userData)->read(path, out);
}

// The message folders' language codes, as mods name them.
std::string languageCode(const char* langDir) {
  if (langDir == nullptr) {
    return {};
  }
  const char* slash = strrchr(langDir, '/');
  std::string code = slash != nullptr ? slash + 1 : langDir;
  if (code == "msg") {
    return "JP"; // the JP build's folders: msg (Japanese), emsg (English)
  }
  if (code == "emsg") {
    return "EN";
  }
  return code;
}

// UTF-8 to the game's encoding: Latin-1 on the US build (its fonts' upper
// half), unchanged on the JP build (Shift-JIS: the file must already be).
std::string toGameText(const std::string& text) {
  if (strcmp(PORT_GAME_ID, "G8MJ01") == 0) {
    return text;
  }
  std::string out;
  for (size_t i = 0; i < text.size();) {
    const auto c = uint8_t(text[i]);
    unsigned cp;
    size_t len;
    if (c < 0x80) {
      cp = c, len = 1;
    } else if ((c & 0xE0) == 0xC0) {
      cp = c & 0x1F, len = 2;
    } else if ((c & 0xF0) == 0xE0) {
      cp = c & 0x0F, len = 3;
    } else {
      cp = c & 0x07, len = 4;
    }
    for (size_t k = 1; k < len && i + k < text.size(); k++) {
      cp = (cp << 6) | (uint8_t(text[i + k]) & 0x3F);
    }
    i += len;
    out += cp < 0x100 ? char(cp) : '?';
  }
  return out;
}

// A messages file: "[tag]" lines, each followed by its text up to the next
// one (the line break before the next tag isn't part of it). Lines before
// the first tag are comments.
int parseMessages(const std::vector<uint8_t>& bytes, std::unordered_map<std::string, std::string>& out) {
  std::string text(bytes.begin(), bytes.end());
  if (text.compare(0, 3, "\xEF\xBB\xBF") == 0) {
    text.erase(0, 3);
  }
  text.erase(std::remove(text.begin(), text.end(), '\r'), text.end());
  int count = 0;
  std::string tag, body;
  bool inEntry = false;
  auto flush = [&]() {
    if (inEntry) {
      if (!body.empty() && body.back() == '\n') {
        body.pop_back();
      }
      out[tag] = toGameText(body);
      count++;
    }
  };
  size_t at = 0;
  while (at <= text.size()) {
    size_t eol = text.find('\n', at);
    if (eol == std::string::npos) {
      eol = text.size();
    }
    const std::string line = text.substr(at, eol - at);
    if (line.size() > 2 && line.front() == '[' && line.back() == ']' && line.find(' ') == std::string::npos) {
      flush();
      tag = line.substr(1, line.size() - 2);
      body.clear();
      inEntry = true;
    } else if (inEntry) {
      body += line;
      if (eol < text.size()) {
        body += '\n';
      }
    }
    at = eol + 1;
  }
  flush();
  return count;
}

std::string lower(std::string s) {
  for (char& c : s) {
    c = char(tolower(uint8_t(c)));
  }
  return s;
}

bool endsWith(const std::string& s, const char* suffix) {
  const size_t n = strlen(suffix);
  return s.size() >= n && lower(s.substr(s.size() - n)) == suffix;
}

// --- data patches ---
//
// A patches file: one "target = value" per line, '#' starting a comment.
//   target  a variable of the reflection table, then fields and indexes:
//           itemDataTable[ITEM_MUSHROOM].sell_price, unitdata_Mario.maxHp,
//           ItemWeaponData_Kinoko.damage_function_params[0]; [*] is every element
//   value   an integer (12, -3, 0x1F), a number (1.5), an enum constant
//           (ITEM_MUSHROOM), true/false, or "text" for a text field (a
//           message tag, a sound name)

std::unordered_map<std::string, const PortReflSymbol*> sSymbols;
std::unordered_map<std::string, long long> sEnums;
std::map<uint8_t*, std::vector<uint8_t>> sOriginal; // patched bytes as they were
std::deque<std::string> sPatchStrings;              // text the game now points at (never freed)

void indexReflection() {
  if (!sSymbols.empty()) {
    return;
  }
  for (int i = 0; i < portReflSymbolCount; i++) {
    sSymbols[portReflSymbols[i].name] = &portReflSymbols[i];
  }
  for (int i = 0; i < portReflEnumCount; i++) {
    sEnums[portReflEnums[i].name] = portReflEnums[i].value;
  }
}

void restorePatched() {
  for (auto& [address, bytes] : sOriginal) {
    memcpy(address, bytes.data(), bytes.size());
  }
  sOriginal.clear();
}

void write(uint8_t* address, const void* data, size_t size) {
  if (sOriginal.count(address) == 0) {
    sOriginal[address].assign(address, address + size);
  }
  memcpy(address, data, size);
}

struct PatchValue {
  enum { Int, Real, Text } type = Int;
  long long i = 0;
  double f = 0;
  std::string s;
};

bool parseValue(const std::string& text, PatchValue& v, std::string& why) {
  if (text.size() >= 2 && text.front() == '"' && text.back() == '"') {
    v.type = PatchValue::Text;
    for (size_t k = 1; k + 1 < text.size(); k++) {
      if (text[k] == '\\' && k + 2 < text.size()) {
        const char e = text[++k];
        v.s += e == 'n' ? '\n' : e;
      } else {
        v.s += text[k];
      }
    }
    v.s = toGameText(v.s);
    return true;
  }
  if (text == "true" || text == "false") {
    v.i = text == "true";
    return true;
  }
  if (const auto it = sEnums.find(text); it != sEnums.end()) {
    v.i = it->second;
    return true;
  }
  char* end = nullptr;
  errno = 0;
  const long long i = strtoll(text.c_str(), &end, 0);
  if (end != text.c_str() && *end == '\0' && errno == 0) {
    v.i = i;
    return true;
  }
  const double f = strtod(text.c_str(), &end);
  if (end != text.c_str() && *end == '\0') {
    v.type = PatchValue::Real;
    v.f = f;
    return true;
  }
  why = "\"" + text + "\" is not a number, \"text\" or a known constant";
  return false;
}

// One element of the target: its address, kind and size.
struct Place {
  uint8_t* address;
  int kind;
  size_t size;
  const PortReflStruct* type;
};

// Splits "a[1].b[*]" into names and indexes ("" for none).
bool parseTarget(const std::string& text, std::vector<std::pair<std::string, std::string>>& parts) {
  size_t at = 0;
  while (at < text.size()) {
    size_t end = text.find_first_of(".[", at);
    std::string name = text.substr(at, end == std::string::npos ? std::string::npos : end - at);
    std::string index;
    at = end == std::string::npos ? text.size() : end;
    if (at < text.size() && text[at] == '[') {
      const size_t close = text.find(']', at);
      if (close == std::string::npos) {
        return false;
      }
      index = text.substr(at + 1, close - at - 1);
      at = close + 1;
      if (index.empty()) {
        return false;
      }
    }
    if (name.empty()) {
      return false;
    }
    parts.emplace_back(name, index);
    if (at < text.size()) {
      if (text[at] != '.') {
        return false;
      }
      at++;
    }
  }
  return !parts.empty();
}

// The elements an index names, of count: [all] for "*".
bool resolveIndex(const std::string& index, int count, std::vector<int>& out, std::string& why) {
  out.clear();
  if (index.empty()) {
    if (count != 1) {
      why = "needs an index (0 to " + std::to_string(count - 1) + ", or *)";
      return false;
    }
    out.push_back(0);
    return true;
  }
  if (index == "*") {
    for (int i = 0; i < count; i++) {
      out.push_back(i);
    }
    return true;
  }
  PatchValue v;
  if (!parseValue(index, v, why) || v.type != PatchValue::Int) {
    why = "index " + index + " is not a number or a known constant";
    return false;
  }
  if (v.i < 0 || v.i >= count) {
    why = "index " + index + " is past the end (" + std::to_string(count) + ")";
    return false;
  }
  out.push_back(int(v.i));
  return true;
}

bool resolveTarget(const std::string& text, std::vector<Place>& places, std::string& why) {
  std::vector<std::pair<std::string, std::string>> parts;
  if (!parseTarget(text, parts)) {
    why = "\"" + text + "\" is not a target (name, name[index], name.field...)";
    return false;
  }
  const auto sym = sSymbols.find(parts[0].first);
  if (sym == sSymbols.end()) {
    why = "no game data called " + parts[0].first + " (docs/MOD_DATA.md lists them)";
    return false;
  }
  const PortReflSymbol* s = sym->second;
  std::vector<int> indexes;
  if (!resolveIndex(parts[0].second, s->count, indexes, why)) {
    why = parts[0].first + " " + why;
    return false;
  }
  places.clear();
  for (int i : indexes) {
    places.push_back({static_cast<uint8_t*>(s->address) + size_t(i) * s->size, s->kind, s->size, s->type});
  }
  for (size_t p = 1; p < parts.size(); p++) {
    const auto& [name, index] = parts[p];
    std::vector<Place> next;
    for (const Place& place : places) {
      if (place.kind != PORT_REFL_STRUCT || place.type == nullptr) {
        why = parts[p - 1].first + " has no fields";
        return false;
      }
      const PortReflField* field = nullptr;
      for (int f = 0; f < place.type->fieldCount; f++) {
        if (name == place.type->fields[f].name) {
          field = &place.type->fields[f];
        }
      }
      if (field == nullptr) {
        why = std::string(place.type->name) + " has no field " + name;
        return false;
      }
      if (!resolveIndex(index, field->count, indexes, why)) {
        why = name + " " + why;
        return false;
      }
      for (int i : indexes) {
        next.push_back({place.address + field->offset + size_t(i) * field->size, field->kind, field->size, field->type});
      }
    }
    places = std::move(next);
  }
  return true;
}

bool store(const Place& place, const PatchValue& v, std::string& why) {
  switch (place.kind) {
  case PORT_REFL_UNSIGNED:
  case PORT_REFL_SIGNED: {
    if (v.type != PatchValue::Int) {
      why = "needs a whole number";
      return false;
    }
    const int bits = int(place.size * 8);
    const bool fits = place.kind == PORT_REFL_UNSIGNED
                          ? v.i >= 0 && (bits >= 64 || v.i < (1LL << bits))
                          : bits >= 64 || (v.i >= -(1LL << (bits - 1)) && v.i < (1LL << (bits - 1)));
    // An unsigned field also takes a negative value's bits (e.g. -1 for 0xFFFF).
    if (!fits && !(place.kind == PORT_REFL_UNSIGNED && v.i < 0 && v.i >= -(1LL << (bits - 1)))) {
      why = std::to_string(v.i) + " does not fit in " + std::to_string(place.size) + " byte(s)";
      return false;
    }
    uint8_t bytes[8];
    const unsigned long long u = static_cast<unsigned long long>(v.i);
    for (size_t b = 0; b < place.size; b++) {
      bytes[b] = uint8_t(u >> (8 * b)); // little-endian hosts (every target the port builds for)
    }
    write(place.address, bytes, place.size);
    return true;
  }
  case PORT_REFL_FLOAT: {
    if (v.type == PatchValue::Text) {
      why = "needs a number";
      return false;
    }
    const double d = v.type == PatchValue::Real ? v.f : double(v.i);
    if (place.size == sizeof(float)) {
      const float f = float(d);
      write(place.address, &f, sizeof(f));
    } else {
      write(place.address, &d, sizeof(d));
    }
    return true;
  }
  case PORT_REFL_STRING: {
    if (v.type != PatchValue::Text) {
      why = "needs \"text\"";
      return false;
    }
    sPatchStrings.push_back(v.s);
    const char* text = sPatchStrings.back().c_str();
    write(place.address, &text, sizeof(text));
    return true;
  }
  default:
    why = place.kind == PORT_REFL_STRUCT ? "is a group of fields: name one of them"
                                         : "can't be changed by a patch (a pointer)";
    return false;
  }
}

// Applies one patches file; returns the lines applied. The first problem
// goes to warning ("file:line: ...").
int applyPatchFile(const std::vector<uint8_t>& bytes, const std::string& name, std::string& warning) {
  indexReflection();
  const std::string text(bytes.begin(), bytes.end());
  int applied = 0;
  int lineNo = 0;
  size_t at = 0;
  while (at < text.size()) {
    size_t eol = text.find('\n', at);
    if (eol == std::string::npos) {
      eol = text.size();
    }
    std::string line = text.substr(at, eol - at);
    at = eol + 1;
    lineNo++;
    // '#' outside quotes starts a comment
    bool quoted = false;
    for (size_t k = 0; k < line.size(); k++) {
      if (line[k] == '"') {
        quoted = !quoted;
      } else if (line[k] == '#' && !quoted) {
        line.resize(k);
        break;
      }
    }
    auto trim = [](std::string s) {
      const size_t b = s.find_first_not_of(" \t\r");
      const size_t e = s.find_last_not_of(" \t\r");
      return b == std::string::npos ? std::string() : s.substr(b, e - b + 1);
    };
    line = trim(line);
    if (line.empty()) {
      continue;
    }
    const size_t eq = line.find('=');
    std::string why;
    std::vector<Place> places;
    PatchValue value;
    bool ok = eq != std::string::npos;
    if (!ok) {
      why = "expected \"target = value\"";
    }
    std::string target = ok ? trim(line.substr(0, eq)) : std::string();
    target.erase(std::remove(target.begin(), target.end(), ' '), target.end());
    ok = ok && resolveTarget(target, places, why) && parseValue(trim(line.substr(eq + 1)), value, why);
    for (size_t p = 0; ok && p < places.size(); p++) {
      ok = store(places[p], value, why);
      if (!ok) {
        why = target + " " + why;
      }
    }
    if (ok) {
      applied++;
    } else if (warning.empty()) {
      warning = name + ":" + std::to_string(lineNo) + ": " + why;
    }
  }
  return applied;
}

// What mod.json says: id, name, ..., options, the versions it is for.
void describe(Mod& m, const Json& root) {
  Mod* mod = &m;
  mod->id = root.str("id");
  mod->name = root.str("name");
  mod->version = root.str("version");
  mod->author = root.str("author");
  mod->description = root.str("description");
  if (mod->id.empty()) {
    mod->error = "mod.json has no \"id\"";
  }
  // "options": [{"key", "label", "type": "bool"|"int"|"choice", "default",
  // "min", "max", "choices": [...]}], for the mod's code (api->optionInt)
  if (const Json* options = root.get("options"); options != nullptr && options->type == Json::Array) {
    for (const Json& o : options->items) {
      auto opt = std::make_unique<Option>();
      opt->key = o.str("key");
      opt->label = o.str("label").empty() ? opt->key : o.str("label");
      const std::string type = o.str("type");
      if (const Json* d = o.get("default"); d != nullptr) {
        opt->fallback = d->type == Json::Bool ? int(d->b) : int(d->n);
      }
      if (type == "int") {
        opt->type = PORT_MOD_OPTION_INT;
        opt->min = o.get("min") != nullptr ? int(o.get("min")->n) : 0;
        opt->max = o.get("max") != nullptr ? int(o.get("max")->n) : 100;
      } else if (type == "choice") {
        opt->type = PORT_MOD_OPTION_CHOICE;
        if (const Json* c = o.get("choices"); c != nullptr && c->type == Json::Array) {
          for (const Json& item : c->items) {
            opt->choices.push_back(item.s);
          }
        }
        opt->max = std::max(0, int(opt->choices.size()) - 1);
      }
      if (!opt->key.empty()) {
        mod->options.push_back(std::move(opt));
      }
    }
  }
  if (const Json* games = root.get("games"); games != nullptr && games->type == Json::Array) {
    bool found = false;
    for (const Json& g : games->items) {
      found |= g.type == Json::String && g.s == PORT_GAME_ID;
    }
    if (!found) {
      mod->error = std::string("is not for this version (") + PORT_GAME_ID + ")";
    }
  }
}

void readMod(const std::string& path, bool isZip, const std::string& display) {
  auto mod = std::make_unique<Mod>();
  mod->display = display;
  std::string error;
  mod->source = Source::open(path, isZip, error);
  std::vector<uint8_t> json;
  Json root;
  if (mod->source == nullptr) {
    mod->error = error.empty() ? "can't be read" : error;
  } else if (!mod->source->read("mod.json", json)) {
    if (!isZip) {
      return; // a folder without mod.json isn't a mod (e.g. one being made)
    }
    mod->error = "has no mod.json";
  } else if (!parseJson(json, root, error) || root.type != Json::Object) {
    mod->error = "mod.json: " + (error.empty() ? std::string("not an object") : error);
  } else {
    describe(*mod, root);
  }
  if (mod->id.empty()) {
    mod->id = display; // to list it with its error
  }
  if (mod->name.empty()) {
    mod->name = mod->id;
  }
  for (const auto& other : sMods) {
    if (other->id == mod->id && mod->error.empty()) {
      mod->error = "has the same id as " + other->display;
    }
  }
  sMods.push_back(std::move(mod));
}

void scan() {
  std::vector<std::unique_ptr<Mod>> old = std::move(sMods);
  for (auto& mod : old) {
    if (mod->source != nullptr) {
      sRetiredSources.push_back(std::move(mod->source)); // open overlay files may still read it
    }
  }
  sMods.clear();
  SDL_CreateDirectory(sModsDir.c_str());
  std::vector<std::string> names;
  SDL_EnumerateDirectory(
      sModsDir.c_str(),
      [](void* user, const char*, const char* fname) -> SDL_EnumerationResult {
        static_cast<std::vector<std::string>*>(user)->emplace_back(fname);
        return SDL_ENUM_CONTINUE;
      },
      &names);
  std::sort(names.begin(), names.end());
  for (const std::string& name : names) {
    const std::string path = sModsDir + "/" + name;
    SDL_PathInfo info;
    if (!SDL_GetPathInfo(path.c_str(), &info)) {
      continue;
    }
    if (info.type == SDL_PATHTYPE_DIRECTORY) {
      readMod(path, false, name);
    } else if (endsWith(name, ".ttydmod") || endsWith(name, ".zip")) {
      readMod(path, true, name);
    }
  }
  // Mods built into the game without a folder of their own (iOS, where
  // the code has to be in the app): listed too.
  for (int i = 0; portModHostBuiltinId(i) != nullptr; i++) {
    const char* id = portModHostBuiltinId(i);
    if (std::none_of(sMods.begin(), sMods.end(), [&](const auto& m) { return m->id == id; })) {
      auto mod = std::make_unique<Mod>();
      const char* text = portModHostBuiltinJson(i);
      const std::vector<uint8_t> json(text, text + strlen(text));
      Json root;
      std::string error;
      if (parseJson(json, root, error) && root.type == Json::Object) {
        describe(*mod, root);
      }
      mod->id = id;
      if (mod->name.empty()) {
        mod->name = id;
      }
      mod->display = "(built in)";
      sMods.push_back(std::move(mod));
    }
  }
  // mods.txt: the order, the first winning; "-" in front: turned off. Mods
  // it doesn't name yet go after the ones it does, turned on.
  std::vector<uint8_t> list;
  std::vector<std::unique_ptr<Mod>> ordered;
  if (readHostFile(sModsDir + "/mods.txt", list)) {
    std::string text(list.begin(), list.end());
    size_t at = 0;
    while (at < text.size()) {
      size_t eol = text.find('\n', at);
      if (eol == std::string::npos) {
        eol = text.size();
      }
      std::string line = text.substr(at, eol - at);
      at = eol + 1;
      while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) {
        line.pop_back();
      }
      if (line.empty() || line[0] == '#') {
        continue;
      }
      const bool off = line[0] == '-';
      const std::string id = off ? line.substr(1) : line;
      for (auto& mod : sMods) {
        if (mod != nullptr && mod->id == id) {
          mod->enabled = !off;
          ordered.push_back(std::move(mod));
          break;
        }
      }
    }
  }
  for (auto& mod : sMods) {
    if (mod != nullptr) {
      ordered.push_back(std::move(mod));
    }
  }
  sMods = std::move(ordered);
}

void writeList() {
  std::string text =
      "# The mods in order: the first wins over the ones after it.\n"
      "# A \"-\" in front of an id turns that mod off. The settings page (Mods) writes this file.\n";
  for (const auto& mod : sMods) {
    text += (mod->enabled ? "" : "-") + mod->id + "\n";
  }
  SDL_SaveFile((sModsDir + "/mods.txt").c_str(), text.data(), text.size());
}

bool usable(const Mod& mod) { return mod.enabled && mod.error.empty() && !sDisabledAll; }

// Its library for this platform (lib/<platform>/*.dll|so|dylib), or "".
std::string libraryOf(const Mod& mod) {
  if (mod.source == nullptr) {
    return {};
  }
  for (const auto& [rel, size] : mod.source->list(std::string("lib/") + TTYD_MOD_PLATFORM + "/")) {
    if (endsWith(rel, ".dll") || endsWith(rel, ".so") || endsWith(rel, ".dylib")) {
      return rel;
    }
  }
  return {};
}

bool hasCode(const Mod& mod) { return portModHostHasBuiltin(mod.id.c_str()) || !libraryOf(mod).empty(); }

// At the game's start: each usable mod's code.
void startCode() {
  for (auto& m : sMods) {
    Mod& mod = *m;
    if (!usable(mod) || !hasCode(mod)) {
      continue;
    }
    const std::string dataDir = sUserPath + "/mods_data/" + mod.id;
    SDL_CreateDirectory((sUserPath + "/mods_data").c_str());
    const char* why;
    if (portModHostHasBuiltin(mod.id.c_str())) {
      why = portModHostStartBuiltin(mod.id.c_str(), dataDir.c_str());
    } else {
      // A zip's library is copied out to the cache first (it is loaded from disk).
      const std::string rel = libraryOf(mod);
      std::string path = mod.source->hostPath(rel);
      if (path.empty()) {
        const std::string dir = sCachePath + "/mod_libs/" + mod.id;
        SDL_CreateDirectory((sCachePath + "/mod_libs").c_str());
        SDL_CreateDirectory(dir.c_str());
        path = dir + "/" + rel.substr(rel.rfind('/') + 1);
        std::vector<uint8_t> bytes;
        if (!mod.source->read(rel, bytes) || !SDL_SaveFile(path.c_str(), bytes.data(), bytes.size())) {
          sCode[mod.id].error = "its library can't be copied out of the zip";
          continue;
        }
      }
      why = portModHostLoadLibrary(mod.id.c_str(), path.c_str(), dataDir.c_str());
    }
    sCode[mod.id].running = why == nullptr;
    sCode[mod.id].error = why != nullptr ? why : "";
    if (why != nullptr) {
      SDL_Log("mods: %s %s", mod.id.c_str(), why);
    }
  }
}

void readOptions() {
  std::vector<uint8_t> bytes;
  if (!readHostFile(sModsDir + "/options.txt", bytes)) {
    return;
  }
  std::string text(bytes.begin(), bytes.end());
  size_t at = 0;
  while (at < text.size()) {
    size_t eol = text.find('\n', at);
    if (eol == std::string::npos) {
      eol = text.size();
    }
    const std::string line = text.substr(at, eol - at);
    at = eol + 1;
    const size_t eq = line.find('=');
    if (eq != std::string::npos && line[0] != '#') {
      sOptionValues[line.substr(0, eq)] = atoi(line.c_str() + eq + 1);
    }
  }
}

void writeOptions() {
  std::string text = "# The mods' options (mod id.option = value); the settings page (Mods) writes this file.\n";
  for (const auto& [key, value] : sOptionValues) {
    text += key + "=" + std::to_string(value) + "\n";
  }
  SDL_SaveFile((sModsDir + "/options.txt").c_str(), text.data(), text.size());
}

int optionValue(const Mod& mod, const Option& opt) {
  const auto it = sOptionValues.find(mod.id + "." + opt.key);
  const int v = it != sOptionValues.end() ? it->second : opt.fallback;
  return std::clamp(v, opt.min, opt.max);
}

void apply() {
  // Disc files: the first mod with a path wins.
  auto files = std::make_unique<std::vector<OverlayFile>>();
  std::unordered_map<std::string, size_t> byPath; // lower-case disc path
  std::vector<AuroraOverlayFile> overlay;
  for (auto& mod : sMods) {
    mod->files = mod->textures = mod->messages = 0;
    if (!usable(*mod) || mod->source == nullptr) {
      continue;
    }
    for (const auto& [rel, size] : mod->source->list("overlay/")) {
      const std::string disc = lower(rel.substr(strlen("overlay")));
      if (byPath.emplace(disc, files->size()).second) {
        files->push_back({mod->source.get(), rel});
        overlay.push_back({nullptr, nullptr, size});
        mod->files++;
      }
    }
  }
  for (size_t i = 0; i < overlay.size(); i++) {
    overlay[i].userData = &(*files)[i];
  }
  std::vector<std::string> discPaths(files->size());
  for (size_t i = 0; i < files->size(); i++) {
    discPaths[i] = (*files)[i].rel.substr(strlen("overlay"));
    overlay[i].fileName = discPaths[i].c_str();
  }
  static const AuroraOverlayCallbacks kCallbacks = {overlayOpen, overlayClose, overlayRead, overlaySeek};
  aurora_dvd_overlay_callbacks(&kCallbacks);
  aurora_dvd_overlay_files(overlay.data(), overlay.size(), nullptr);
  sOverlayGenerations.push_back(std::move(files));

  // Textures: priority by order; the loose folder below every mod.
  for (auto& group : sTextureGroups) {
    aurora::texture::unregister_replacements(group);
  }
  sTextureGroups.clear();
  if (!sDisabledAll) {
    sTextureGroups.push_back(aurora::texture::load_replacement_directory(sUserPath + "/textures", {.priority = 0}));
  }
  for (size_t i = 0; i < sMods.size(); i++) {
    Mod& mod = *sMods[i];
    if (!usable(mod) || mod.source == nullptr) {
      continue;
    }
    const aurora::texture::ReplacementOptions options{.priority = int32_t(1000 - i)};
    aurora::texture::ReplacementGroup group;
    const std::string host = mod.source->hostPath("textures");
    if (!host.empty()) {
      SDL_PathInfo info;
      if (SDL_GetPathInfo(host.c_str(), &info) && info.type == SDL_PATHTYPE_DIRECTORY) {
        group = aurora::texture::load_replacement_directory(host, options);
      }
    } else {
      for (const auto& [rel, size] : mod.source->list("textures/")) {
        const std::string name = lower(rel);
        if ((endsWith(name, ".png") || endsWith(name, ".dds")) && name.find("_mip") == std::string::npos) {
          const auto reg = aurora::texture::register_virtual_replacement(
              rel, {texturesVirtualRead, const_cast<Source*>(mod.source.get())}, options);
          if (reg.id != 0) {
            group.registrations.push_back(reg);
          }
        }
      }
    }
    mod.textures = int(group.registrations.size());
    sTextureGroups.push_back(std::move(group));
  }

  // Messages: the first mod with a tag wins.
  auto messages = std::make_unique<Messages>();
  for (auto it = sMods.rbegin(); it != sMods.rend(); ++it) {
    Mod& mod = **it;
    if (!usable(mod) || mod.source == nullptr) {
      continue;
    }
    for (const auto& [rel, size] : mod.source->list("messages/")) {
      if (!endsWith(rel, ".txt")) {
        continue;
      }
      const std::string inner = rel.substr(strlen("messages/"));
      const size_t slash = inner.find('/');
      const std::string lang = slash == std::string::npos ? std::string() : inner.substr(0, slash);
      std::vector<uint8_t> bytes;
      if (mod.source->read(rel, bytes)) {
        mod.messages += parseMessages(bytes, (*messages)[lang]);
      }
    }
  }
  {
    std::lock_guard lock(sMessageLock);
    sMessages = messages.get();
    sMessageGenerations.push_back(std::move(messages));
  }

  // Data patches: the original values back, then every mod's from the
  // last to the first, so the first wins.
  restorePatched();
  for (auto it = sMods.rbegin(); it != sMods.rend(); ++it) {
    Mod& mod = **it;
    mod.patches = 0;
    mod.warning.clear();
    if (!usable(mod) || mod.source == nullptr) {
      continue;
    }
    for (const auto& [rel, size] : mod.source->list("patches/")) {
      std::vector<uint8_t> bytes;
      if (endsWith(rel, ".txt") && mod.source->read(rel, bytes)) {
        mod.patches += applyPatchFile(bytes, rel, mod.warning);
      }
    }
  }

  for (auto& mod : sMods) {
    int code = PORT_MOD_CODE_NONE;
    const char* codeError = nullptr;
    if (hasCode(*mod)) {
      const auto it = sCode.find(mod->id);
      const bool running = it != sCode.end() && it->second.running;
      if (it != sCode.end() && !it->second.error.empty()) {
        code = PORT_MOD_CODE_FAILED;
        codeError = it->second.error.c_str();
      } else if (running) {
        code = usable(*mod) ? PORT_MOD_CODE_RUNNING : PORT_MOD_CODE_STOPS_NEXT_START;
      } else {
        code = usable(*mod) && sStarted ? PORT_MOD_CODE_STARTS_NEXT_START : PORT_MOD_CODE_OFF;
      }
    }
    for (auto& opt : mod->options) {
      opt->choicePtrs.clear();
      for (const auto& c : opt->choices) {
        opt->choicePtrs.push_back(c.c_str());
      }
      opt->info = {opt->key.c_str(), opt->label.c_str(), opt->type, opt->min, opt->max, optionValue(*mod, *opt),
                   int(opt->choicePtrs.size()), opt->choicePtrs.data()};
    }
    mod->info = {mod->id.c_str(), mod->name.c_str(), mod->version.c_str(), mod->author.c_str(),
                 mod->description.c_str(), mod->display.c_str(), mod->error.empty() ? nullptr : mod->error.c_str(),
                 mod->enabled ? 1 : 0, mod->files, mod->textures, mod->messages, mod->patches,
                 mod->warning.empty() ? nullptr : mod->warning.c_str(), code, codeError, int(mod->options.size())};
    if (usable(*mod)) {
      SDL_Log("mods: %s%s%s: %d files, %d textures, %d messages, %d patches", mod->id.c_str(),
              mod->version.empty() ? "" : " ", mod->version.c_str(), mod->files, mod->textures, mod->messages,
              mod->patches);
      if (!mod->warning.empty()) {
        SDL_Log("mods: %s: %s", mod->id.c_str(), mod->warning.c_str());
      }
    } else if (!mod->error.empty()) {
      SDL_Log("mods: %s %s", mod->display.c_str(), mod->error.c_str());
    }
  }
}

} // namespace

extern "C" {

void portModsInit(const char* userPath, const char* cachePath) {
  sUserPath = userPath;
  sModsDir = sUserPath + "/mods";
  sDumpDir = std::string(cachePath) + "/texture_dumps";
  sCachePath = cachePath;
  sDisabledAll = getenv("TTYD_NO_MODS") != nullptr;
  SDL_CreateDirectory((sUserPath + "/textures").c_str());
  portModHostInit();
  scan();
  readOptions();
  apply();
  startCode();
  sStarted = true;
  apply(); // the code's state, for the Mods tab
}

int portModsOptionInt(const char* modId, const char* key, int fallback) {
  for (const auto& mod : sMods) {
    if (mod->id == modId) {
      for (const auto& opt : mod->options) {
        if (opt->key == key) {
          return optionValue(*mod, *opt);
        }
      }
    }
  }
  return fallback;
}

const PortModOption* portModsGetOption(int index, int option) {
  if (index < 0 || index >= int(sMods.size()) || option < 0 || option >= int(sMods[size_t(index)]->options.size())) {
    return nullptr;
  }
  return &sMods[size_t(index)]->options[size_t(option)]->info;
}

void portModsSetOption(int index, int option, int value) {
  if (portModsGetOption(index, option) == nullptr) {
    return;
  }
  Mod& mod = *sMods[size_t(index)];
  Option& opt = *mod.options[size_t(option)];
  sOptionValues[mod.id + "." + opt.key] = std::clamp(value, opt.min, opt.max);
  opt.info.value = optionValue(mod, opt);
  writeOptions();
}

void portModsSetMessage(const char* tag, const char* text) {
  std::lock_guard lock(sMessageLock);
  if (text == nullptr) {
    sCodeMessages.erase(tag);
  } else {
    sCodeMessageText.push_back(toGameText(text)); // kept: the game may still show the old one
    sCodeMessages[tag] = sCodeMessageText.back().c_str();
  }
}

int portModsCount(void) { return int(sMods.size()); }

const PortModInfo* portModsGet(int index) {
  return index >= 0 && index < int(sMods.size()) ? &sMods[size_t(index)]->info : nullptr;
}

void portModsSetEnabled(int index, int enabled) {
  if (index < 0 || index >= int(sMods.size())) {
    return;
  }
  sMods[size_t(index)]->enabled = enabled != 0;
  writeList();
  apply();
}

void portModsMove(int index, int delta) {
  const int to = index + delta;
  if (index < 0 || index >= int(sMods.size()) || to < 0 || to >= int(sMods.size())) {
    return;
  }
  std::swap(sMods[size_t(index)], sMods[size_t(to)]);
  writeList();
  apply();
}

void portModsReload(void) {
  scan();
  apply();
}

const char* portModsDir(void) { return sModsDir.c_str(); }

const char* portModsDumpDir(void) { return sDumpDir.c_str(); }

// $TTYD_TRACE: each tag a mod answered, once (local_ci's mods stage)
static const char* traced(const char* tag, const char* text) {
  static const bool trace = getenv("TTYD_TRACE") != nullptr;
  static std::unordered_map<std::string, bool> seen;
  if (trace && !seen[tag]) {
    seen[tag] = true;
    printf("[trace] mod message %s\n", tag);
    fflush(stdout);
  }
  return text;
}

const char* portModMsgSearch(const char* tag, const char* langDir) {
  std::lock_guard lock(sMessageLock);
  if (!sCodeMessages.empty()) {
    if (const auto it = sCodeMessages.find(tag); it != sCodeMessages.end()) {
      return traced(tag, it->second);
    }
  }
  if (sMessages == nullptr || sMessages->empty()) {
    return nullptr;
  }
  for (const std::string& lang : {languageCode(langDir), std::string()}) {
    const auto table = sMessages->find(lang);
    if (table != sMessages->end()) {
      const auto it = table->second.find(tag);
      if (it != table->second.end()) {
        return traced(tag, it->second.c_str());
      }
    }
  }
  return nullptr;
}

} // extern "C"
