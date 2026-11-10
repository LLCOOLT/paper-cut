// Map edits as files (port/include/port/map_edits.h, docs/MAP_EDITOR.md): a
// mod's maps/<room>.json read into things, and things written back (the map
// editor's saves). The store and the room's runtime are
// port/src/common/map_edits_pc.c.
#include "port/map_edits.h"
#include "port/battlex.h"
#include "port/json.h"
#include "evt_asm.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using port_json::Json;

namespace {

// an item id within the item table: the game's (below ITEM_TYPE_MAX, 0x153) or the port's
// (port/battlex.h PORT_ITEM_MIN..PORT_ITEM_END): anything else would index past it (a garbage icon or
// name pointer, a crash on entering the room)
constexpr long long kGameItemEnd = 0x153;
bool itemInTable(long long n) { return (n > 0 && n < kGameItemEnd) || (n >= PORT_ITEM_MIN && n < PORT_ITEM_END); }

// a number, or a header constant's name ("ITEM_MUSHROOM", "GSWF_...")
bool number(const Json* v, long long& out) {
  if (v == nullptr) {
    return false;
  }
  if (v->type == Json::Number) {
    if (!std::isfinite(v->n) || std::fabs(v->n) > 1e15) {
      return false; // (llround of it has no answer)
    }
    out = (long long)std::llround(v->n);
    return true;
  }
  if (v->type == Json::String) {
    return port_evt::constantValue(v->s, out);
  }
  if (v->type == Json::Bool) {
    out = v->b ? 1 : 0;
    return true;
  }
  return false;
}

void text(char* out, size_t size, const std::string& s) { std::snprintf(out, size, "%s", s.c_str()); }

struct Reader {
  const std::string& file;
  int index;
  std::vector<std::string>& errors;
  bool ok = true;

  void error(const std::string& what) {
    errors.push_back(file + ": thing " + std::to_string(index + 1) + ": " + what);
    ok = false;
  }
  int integer(const Json& o, const char* key, int fallback, long long min, long long max) {
    const Json* v = o.get(key);
    long long n = 0;
    if (v == nullptr) {
      return fallback;
    }
    if (!number(v, n)) {
      error(std::string("\"") + key + "\" is not a number or a known name");
      return fallback;
    }
    if (n < min || n > max) {
      error(std::string("\"") + key + "\" out of range (" + std::to_string(min) + " to " + std::to_string(max) + ")");
      return fallback;
    }
    return int(n);
  }
  void string(const Json& o, const char* key, char* out, size_t size) {
    const Json* v = o.get(key);
    if (v == nullptr) {
      return;
    }
    if (v->type != Json::String) {
      error(std::string("\"") + key + "\" is not text");
    } else if (v->s.size() >= size) {
      error(std::string("\"") + key + "\" is too long (" + std::to_string(size - 1) + " at most)");
    } else {
      text(out, size, v->s);
    }
  }
};

bool readThing(const Json& o, Reader& r, PortMapThing& t) {
  if (o.type != Json::Object) {
    r.error("not an object");
    return false;
  }
  const int type = portMapThingType(o.str("type").c_str());
  if (type < 0) {
    r.error("no such \"type\": \"" + o.str("type") + "\"");
    return false;
  }
  portMapThingInit(&t, type);
  r.string(o, "id", t.id, sizeof(t.id));
  if (const Json* p = o.get("pos")) {
    if (p->type != Json::Array || p->items.size() != 3 || p->items[0].type != Json::Number ||
        p->items[1].type != Json::Number || p->items[2].type != Json::Number) {
      r.error("\"pos\" is not [x, y, z]");
    } else {
      for (int i = 0; i < 3; i++) {
        t.pos[i] = float(p->items[i].n);
        if (std::fabs(t.pos[i]) > 20000.f) {
          r.error("\"pos\" is far outside any room");
        }
      }
    }
  } else if (portMapThingPlaced(type)) {
    r.error("no \"pos\"");
  }
  if (const Json* v = o.get("ry"); v != nullptr && v->type == Json::Number) {
    t.ry = std::isfinite(v->n) ? float(std::fmod(v->n, 360.0)) : 0.f;
  }
  if (const Json* w = o.get("when")) {
    if (w->type != Json::Object) {
      r.error("\"when\" is not an object");
    } else {
      t.story = r.integer(*w, "story", 0, 0, 0x7FFF);
      t.storyBelow = r.integer(*w, "story_below", 0, 0, 0x7FFF);
      t.gswf = r.integer(*w, "gswf", -1, 0, 8191);
      t.gswfOff = r.integer(*w, "gswf_off", -1, 0, 8191);
      t.gsw = r.integer(*w, "gsw", -1, 0, 2047);
      t.gswMin = r.integer(*w, "gsw_min", 1, -128, 255);
    }
  }
  t.hidden = o.get("hidden") != nullptr && o.get("hidden")->type == Json::Bool && o.get("hidden")->b;
  r.string(o, "to", t.to, sizeof(t.to));
  r.string(o, "entrance", t.entrance, sizeof(t.entrance));
  r.string(o, "target", t.target, sizeof(t.target));
  r.string(o, "model", t.model, sizeof(t.model));
  r.string(o, "text", t.text, sizeof(t.text));
  r.string(o, "say", t.say, sizeof(t.say));
  switch (type) {
    case PORT_MT_HEART_BLOCK: t.arg = r.integer(o, "price", 10, 0, 999); break;
    case PORT_MT_JUMP_STAND: t.arg = r.integer(o, "power", 130, 1, 1000); break;
    case PORT_MT_BLOCK:
    case PORT_MT_BADGE_BLOCK:
    case PORT_MT_BRICK:
    case PORT_MT_HIDDEN_BLOCK:
    case PORT_MT_ITEM:
      t.arg = r.integer(o, "item", 0, 0, 0x7FFF);
      if (t.arg != 0 && !itemInTable(t.arg)) {
        r.error("\"item\" is not a known one (" + std::to_string(t.arg) + ")");
        t.arg = 0;
      }
      break;
    default: break;
  }
  if (const Json* f = o.get("flag")) {
    if (f->type != Json::Object) {
      r.error("\"flag\" is not {\"gswf\": n} or {\"lswf\": n}");
    } else if (f->get("gswf") != nullptr) {
      t.flagKind = PORT_MT_FLAG_GSWF;
      t.flagId = r.integer(*f, "gswf", 0, 0, PORT_MT_GSWF_MAX);
    } else if (f->get("lswf") != nullptr) {
      t.flagKind = PORT_MT_FLAG_LSWF;
      t.flagId = r.integer(*f, "lswf", 0, 0, PORT_MT_LSWF_MAX);
    }
  }
  if (const Json* u = o.get("uses")) {
    if (u->type == Json::Object) {
      t.usesGsw = r.integer(*u, "gsw", -1, 0, 2047);
      t.usesValue = r.integer(*u, "value", 1, -128, 255);
    }
  }
  if (const Json* v = o.get("radius"); v != nullptr && v->type == Json::Number) {
    if (!std::isfinite(v->n) || v->n < 0 || v->n > 2000) {
      r.error("\"radius\" out of range (0 to 2000)");
    } else {
      t.radius = float(v->n);
    }
  }
  if (const Json* s = o.get("shop")) {
    if (s->type != Json::Array) {
      r.error("\"shop\" is not a list of items");
    }
    for (const Json& item : s->items) {
      long long n = 0;
      if (t.shopCount >= PORT_MT_SHOP_MAX) {
        r.error("\"shop\" has more than 8 items");
        break;
      }
      if (!number(&item, n) || !itemInTable(n)) {
        r.error("\"shop\" has an item that is not a known one");
        continue;
      }
      t.shop[t.shopCount++] = int(n);
    }
  }
  if (const Json* s = o.get("shows"); s != nullptr && s->type == Json::Array) {
    int k = 0;
    for (const Json& id : s->items) {
      if (id.type == Json::String && k < PORT_MT_SHOWS_MAX) {
        text(t.shows[k++], sizeof(t.shows[0]), id.s);
      }
    }
  }
  // what each type needs
  const bool link = type == PORT_MT_PIPE || type == PORT_MT_ZONE || type == PORT_MT_EXIT;
  if (link && (t.to[0] == '\0' || t.entrance[0] == '\0')) {
    r.error("needs \"to\" (a room) and \"entrance\" (an entrance there)");
  }
  if (type == PORT_MT_JUMP_STAND && (t.to[0] != '\0') != (t.entrance[0] != '\0')) {
    r.error("\"to\" and \"entrance\" go together");
  }
  if (type >= PORT_MT_HIDE_COLLISION && t.target[0] == '\0') {
    r.error("needs \"target\" (the room's own thing, by name)");
  }
  if (type == PORT_MT_NPC && t.model[0] == '\0') {
    r.error("needs \"model\" (an animation group of the disc's, as \"c_kinopio\")");
  }
  if ((type == PORT_MT_ITEM || type == PORT_MT_BLOCK || type == PORT_MT_BADGE_BLOCK) && t.arg <= 0) {
    r.error("needs \"item\"");
  }
  if (r.ok) {
    char why[128];
    if (!portMapThingCheck(&t, why, int(sizeof(why)))) {
      r.error(why);
    }
  }
  return r.ok;
}

bool within(long long v, long long min, long long max) { return v >= min && v <= max; }

} // namespace

extern "C" int portMapThingCheck(const PortMapThing* t, char* why, int size) {
  const char* bad = nullptr;
  auto finiteIn = [](float f, float limit) { return std::isfinite(f) && std::fabs(f) <= limit; };
  if (t == nullptr || t->type < 0 || t->type >= PORT_MT_COUNT) {
    bad = "no such type";
  } else if (!finiteIn(t->pos[0], 20000.f) || !finiteIn(t->pos[1], 20000.f) || !finiteIn(t->pos[2], 20000.f)) {
    bad = "\"pos\" is far outside any room";
  } else if (!finiteIn(t->ry, 1.0e6f)) {
    bad = "\"ry\" is not a finite angle";
  } else if (!finiteIn(t->radius, 20000.f) || t->radius < 0.f) {
    bad = "\"radius\" out of range (0 to 20000)";
  } else if (!within(t->story, 0, 0x7FFF) || !within(t->storyBelow, 0, 0x7FFF)) {
    bad = "\"story\" out of range (0 to 32767)";
  } else if (!within(t->gswf, -1, 8191) || !within(t->gswfOff, -1, 8191)) {
    bad = "\"gswf\" out of range (0 to 8191)";
  } else if (!within(t->gsw, -1, 2047) || !within(t->usesGsw, -1, 2047)) {
    bad = "\"gsw\" out of range (0 to 2047)";
  } else if (!within(t->gswMin, -128, 255) || !within(t->usesValue, -128, 255)) {
    bad = "a saved byte's value out of range (-128 to 255)";
  } else if (t->flagKind == PORT_MT_FLAG_GSWF   ? !within(t->flagId, 0, PORT_MT_GSWF_MAX)
             : t->flagKind == PORT_MT_FLAG_LSWF ? !within(t->flagId, 0, PORT_MT_LSWF_MAX)
                                                : t->flagKind != PORT_MT_FLAG_NONE) {
    // (GlobalWork's own sizes: the lswf bits are 512, not the 1024 this once let through)
    static_assert(PORT_MT_GSWF_MAX == 8191 && PORT_MT_LSWF_MAX == 511, "the message below names these");
    bad = "\"flag\" out of range (gswf 0 to 8191, lswf 0 to 511)";
  } else if (!within(t->shopCount, 0, PORT_MT_SHOP_MAX)) {
    bad = "\"shop\" has more than 8 items";
  } else {
    switch (t->type) {
      case PORT_MT_HEART_BLOCK: bad = within(t->arg, 0, 999) ? nullptr : "\"price\" out of range (0 to 999)"; break;
      case PORT_MT_JUMP_STAND: bad = within(t->arg, 0, 1000) ? nullptr : "\"power\" out of range (1 to 1000)"; break;
      case PORT_MT_BLOCK:
      case PORT_MT_BADGE_BLOCK:
      case PORT_MT_BRICK:
      case PORT_MT_HIDDEN_BLOCK:
      case PORT_MT_ITEM: // (as the JSON reader: 0, none, or an id within the item table)
        bad = t->arg == 0 || itemInTable(t->arg) ? nullptr : "\"item\" is not a known one";
        break;
      default: break;
    }
    for (int i = 0; bad == nullptr && i < t->shopCount; i++) {
      if (!itemInTable(t->shop[i])) {
        bad = "\"shop\" has an item that is not a known one";
      }
    }
  }
  if (bad != nullptr && why != nullptr && size > 0) {
    std::snprintf(why, size_t(size), "%s", bad);
  }
  return bad == nullptr ? 1 : 0;
}

bool portMapEditsParse(const std::string& textIn, const std::string& file, std::string& room,
                       std::vector<PortMapThing>& out, std::vector<std::string>& errors) {
  Json root;
  std::string why;
  const std::vector<uint8_t> bytes(textIn.begin(), textIn.end());
  if (!port_json::parseJson(bytes, root, why) || root.type != Json::Object) {
    errors.push_back(file + ": " + (why.empty() ? std::string("not a JSON object") : why));
    return false;
  }
  room = root.str("room");
  if (room.empty()) {
    // maps/gor_01.json: the room is the file's name
    const size_t slash = file.find_last_of('/');
    std::string base = slash == std::string::npos ? file : file.substr(slash + 1);
    if (base.size() > 5 && base.compare(base.size() - 5, 5, ".json") == 0) {
      base.resize(base.size() - 5);
    }
    room = base;
  }
  if (room.empty() || room.size() >= 16) {
    errors.push_back(file + ": no room (\"room\": \"gor_01\", or the file named after it)");
    return false;
  }
  const Json* things = root.get("things");
  if (things == nullptr || things->type != Json::Array) {
    errors.push_back(file + ": no \"things\" list");
    return false;
  }
  bool ok = true;
  constexpr size_t kMaxThings = 1024; // (a room's; more is a damaged or hostile file)
  if (things->items.size() > kMaxThings) {
    errors.push_back(file + ": more than " + std::to_string(kMaxThings) + " things: the first ones are used");
    ok = false;
  }
  for (size_t i = 0; i < things->items.size() && i < kMaxThings; i++) {
    Reader r{file, int(i), errors};
    PortMapThing t;
    if (readThing(things->items[i], r, t)) {
      out.push_back(t);
    } else {
      ok = false; // (a thing with an error is left out; the others are used)
    }
  }
  return ok;
}

namespace {

void field(std::string& out, const char* key, const std::string& value) {
  out += ", \"";
  out += key;
  out += "\": ";
  port_json::quote(out, value);
}

void field(std::string& out, const char* key, long long value) {
  out += ", \"";
  out += key;
  out += "\": " + std::to_string(value);
}

std::string num(float v) {
  char b[32];
  if (std::fabs(v - std::round(v)) < 0.005f) {
    std::snprintf(b, sizeof(b), "%d", int(std::lround(v)));
  } else {
    std::snprintf(b, sizeof(b), "%.2f", v);
  }
  return b;
}

} // namespace

std::string portMapEditsWrite(const std::string& room, const std::vector<PortMapThing>& things) {
  std::string out = "{\n  \"room\": ";
  port_json::quote(out, room);
  out += ",\n  \"things\": [";
  for (size_t i = 0; i < things.size(); i++) {
    const PortMapThing& t = things[i];
    out += i == 0 ? "\n    {" : ",\n    {";
    out += "\"type\": ";
    port_json::quote(out, portMapThingTypeName(t.type));
    if (t.id[0] != '\0') {
      field(out, "id", t.id);
    }
    if (portMapThingPlaced(t.type)) {
      out += ", \"pos\": [" + num(t.pos[0]) + ", " + num(t.pos[1]) + ", " + num(t.pos[2]) + "]";
    }
    if (t.ry != 0.f) {
      out += ", \"ry\": " + num(t.ry);
    }
    const char* const strs[][2] = {{"to", t.to},           {"entrance", t.entrance}, {"target", t.target},
                                   {"model", t.model},     {"text", t.text},         {"say", t.say}};
    for (const auto& s : strs) {
      if (s[1][0] != '\0') {
        field(out, s[0], s[1]);
      }
    }
    switch (t.type) {
      case PORT_MT_HEART_BLOCK: field(out, "price", t.arg); break;
      case PORT_MT_JUMP_STAND: field(out, "power", t.arg > 0 ? t.arg : 130); break;
      case PORT_MT_BLOCK:
      case PORT_MT_BADGE_BLOCK:
      case PORT_MT_BRICK:
      case PORT_MT_HIDDEN_BLOCK:
      case PORT_MT_ITEM:
        if (t.arg > 0) {
          field(out, "item", t.arg);
        }
        break;
      default: break;
    }
    if (t.flagKind != PORT_MT_FLAG_NONE) {
      out += std::string(", \"flag\": {\"") + (t.flagKind == PORT_MT_FLAG_GSWF ? "gswf" : "lswf") +
             "\": " + std::to_string(t.flagId) + "}";
    }
    std::string when;
    if (t.story > 0) field(when, "story", t.story);
    if (t.storyBelow > 0) field(when, "story_below", t.storyBelow);
    if (t.gswf >= 0) field(when, "gswf", t.gswf);
    if (t.gswfOff >= 0) field(when, "gswf_off", t.gswfOff);
    if (t.gsw >= 0) {
      field(when, "gsw", t.gsw);
      field(when, "gsw_min", t.gswMin);
    }
    if (!when.empty()) {
      out += ", \"when\": {" + when.substr(2) + "}";
    }
    if (t.usesGsw >= 0) {
      out += ", \"uses\": {\"gsw\": " + std::to_string(t.usesGsw) + ", \"value\": " + std::to_string(t.usesValue) + "}";
    }
    if (t.hidden) {
      out += ", \"hidden\": true";
    }
    if (t.type == PORT_MT_ZONE && t.radius > 0.f) {
      out += ", \"radius\": " + num(t.radius);
    }
    if (t.shopCount > 0) {
      out += ", \"shop\": [";
      for (int k = 0; k < t.shopCount; k++) {
        out += (k ? ", " : "") + std::to_string(t.shop[k]);
      }
      out += "]";
    }
    std::string shows;
    for (const auto& id : t.shows) {
      if (id[0] != '\0') {
        shows += ", ";
        port_json::quote(shows, id);
      }
    }
    if (!shows.empty()) {
      out += ", \"shows\": [" + shows.substr(2) + "]";
    }
    out += "}";
  }
  out += "\n  ]\n}\n";
  return out;
}
