// port/archipelago/ap_client.h. The protocol: archipelago.gg's network
// protocol documentation (docs/network protocol.md in ArchipelagoMW/Archipelago).
#include "ap_client.h"
#include "websocket.h"

#include <SDL3/SDL.h>

#include <chrono>
#include <random>

// the thread's own alternate signal stack for crash reports (port/platform.h)
extern "C" void portCrashThreadInit(void);

namespace port_ap {

using port_json::Json;

namespace {

Json field(const char* name, Json v) { return port_json::object({{name, std::move(v)}}); }

double num(const Json* v, double fallback = 0) { return v != nullptr && v->type == Json::Number ? v->n : fallback; }

std::string randomUuid() {
  std::random_device rd;
  char buf[33];
  for (int i = 0; i < 32; i++) {
    buf[i] = "0123456789abcdef"[rd() & 15];
  }
  buf[32] = 0;
  return buf;
}

} // namespace

Client::Client(Callbacks callbacks) : mCallbacks(std::move(callbacks)) {}

Client::~Client() { stop(); }

void Client::start(const Settings& settings) {
  stop();
  mSettings = settings;
#ifdef __EMSCRIPTEN__ // (one thread: nothing to keep the connection with while the game runs)
  setStatus(Status::Failed, "connecting to a server isn't in the browser yet");
  return;
#endif
  setStatus(Status::Connecting, "connecting to " + settings.server);
  std::thread(&Client::run, mLink, settings).detach(); // never waited for: see Link
}

void Client::stop() {
  std::shared_ptr<WebSocket> socket;
  Status status;
  std::string message;
  {
    std::lock_guard lock(mLink->lock);
    mLink->stop = true;
    socket = mLink->socket; // (none while connecting: the thread sees stop once connected, and closes it)
    status = mLink->status;
    message = mLink->message;
  }
  if (socket) {
    // ends a receive in progress, never waiting on the server; the full close is the last user's (the
    // thread's, as it lets go of the link), never the game's
    socket->interrupt();
  }
  // a new state: the old one stays the thread's until it ends; what shows carries over
  mLink = std::make_shared<Link>();
  if (status == Status::Connecting || status == Status::Connected) {
    status = Status::Off;
    message = "disconnected";
  }
  mLink->status = status;
  mLink->message = std::move(message);
}

void Client::setStatusOf(Link& link, Status s, std::string message) {
  std::lock_guard lock(link.lock);
  link.status = s;
  link.message = std::move(message);
  SDL_Log("archipelago: %s", link.message.c_str());
}

void Client::setStatus(Status s, std::string message) { setStatusOf(*mLink, s, std::move(message)); }

Status Client::status() const {
  std::lock_guard lock(mLink->lock);
  return mLink->status;
}

std::string Client::message() const {
  std::lock_guard lock(mLink->lock);
  return mLink->message;
}

// The network thread: connect (wss:// then ws:// for an address without one),
// then queue every command the server sends. Only its own link: the Client may be gone.
void Client::run(std::shared_ptr<Link> link, Settings settings) {
  portCrashThreadInit(); // (a stack overflow here reported too)
  std::vector<std::string> urls;
  if (settings.server.find("://") != std::string::npos) {
    urls.push_back(settings.server);
  } else {
    if (WebSocket::secureSupported()) {
      urls.push_back("wss://" + settings.server);
    }
    urls.push_back("ws://" + settings.server);
  }
  std::string error;
  std::shared_ptr<WebSocket> socket;
  for (const std::string& url : urls) {
    std::shared_ptr<WebSocket> s = WebSocket::create();
    if (s->connect(url, error)) {
      socket = std::move(s);
      break;
    }
    std::lock_guard lock(link->lock);
    if (link->stop) {
      return;
    }
  }
  {
    std::lock_guard lock(link->lock);
    if (link->stop) { // stopped while connecting: nothing to show (a newer state shows)
      if (socket) {
        socket->close();
      }
      return;
    }
    link->socket = socket;
  }
  if (!socket) {
    setStatusOf(*link, Status::Failed, "can't connect to " + settings.server + ": " + error);
    return;
  }
  std::string text;
  while (socket->receive(text)) {
    Json packet;
    std::string why;
    if (!port_json::parseJson(std::vector<uint8_t>(text.begin(), text.end()), packet, why) ||
        packet.type != Json::Array) {
      continue;
    }
    std::lock_guard lock(link->lock);
    if (link->stop) {
      break;
    }
    for (Json& cmd : packet.items) {
      link->inbox.push_back(std::move(cmd));
    }
  }
  std::lock_guard lock(link->lock);
  if (link->status != Status::Failed && !link->stop) {
    link->status = Status::Failed;
    link->message = "the server closed the connection";
  }
}

bool Client::send(const Json& packet) {
  std::shared_ptr<WebSocket> socket;
  {
    std::lock_guard lock(mLink->lock);
    socket = mLink->socket;
  }
  return socket && socket->send(port_json::dump(port_json::array({packet})));
}

void Client::poll() {
  std::deque<Json> inbox;
  {
    std::lock_guard lock(mLink->lock);
    inbox.swap(mLink->inbox);
  }
  for (const Json& cmd : inbox) {
    handle(cmd);
  }
}

void Client::handle(const Json& cmd) {
  const std::string name = cmd.str("cmd");
  if (name == "RoomInfo") {
    // the games' names first (a small data package), then the slot
    Json games = port_json::array();
    if (const Json* g = cmd.get("games"); g != nullptr && g->type == Json::Array) {
      games = *g;
    }
    send(port_json::object({{"cmd", port_json::string("GetDataPackage")}, {"games", games}}));
    Json tags = port_json::array();
    for (const std::string& t : mSettings.tags) {
      tags.items.push_back(port_json::string(t));
    }
    send(port_json::object({
        {"cmd", port_json::string("Connect")},
        {"password", port_json::string(mSettings.password)},
        {"game", port_json::string(mSettings.game)},
        {"name", port_json::string(mSettings.slot)},
        {"uuid", port_json::string(randomUuid())},
        {"version", port_json::object({{"major", port_json::number(0)}, {"minor", port_json::number(6)},
                                       {"build", port_json::number(2)}, {"class", port_json::string("Version")}})},
        {"items_handling", port_json::number(mSettings.itemsHandling)},
        {"tags", tags},
        {"slot_data", port_json::boolean(true)},
    }));
  } else if (name == "ConnectionRefused") {
    std::string why;
    if (const Json* e = cmd.get("errors"); e != nullptr) {
      for (const Json& x : e->items) {
        why += (why.empty() ? "" : ", ") + x.s;
      }
    }
    setStatus(Status::Failed, "the server refused: " + (why.empty() ? std::string("no reason given") : why));
  } else if (name == "Connected") {
    mSlot = int(num(cmd.get("slot")));
    mTeam = int(num(cmd.get("team")));
    if (const Json* players = cmd.get("players"); players != nullptr) {
      for (const Json& p : players->items) {
        mPlayers[int(num(p.get("slot")))] = p.str("alias").empty() ? p.str("name") : p.str("alias");
      }
    }
    if (const Json* info = cmd.get("slot_info"); info != nullptr) {
      for (const auto& [k, v] : info->fields) {
        mSlotGames[std::atoi(k.c_str())] = v.str("game");
      }
    }
    std::set<int64_t> checked;
    if (const Json* c = cmd.get("checked_locations"); c != nullptr) {
      for (const Json& x : c->items) {
        checked.insert(int64_t(x.n));
      }
    }
    setStatus(Status::Connected, "connected as " + mSettings.slot);
    if (mCallbacks.connected) {
      const Json* data = cmd.get("slot_data");
      mCallbacks.connected(data != nullptr ? *data : Json(), checked);
    }
  } else if (name == "ReceivedItems") {
    std::vector<NetworkItem> items;
    if (const Json* list = cmd.get("items"); list != nullptr) {
      for (const Json& i : list->items) {
        items.push_back({int64_t(num(i.get("item"))), int64_t(num(i.get("location"))), int(num(i.get("player"))),
                         int(num(i.get("flags")))});
      }
    }
    if (mCallbacks.items) {
      mCallbacks.items(int(num(cmd.get("index"))), items);
    }
  } else if (name == "DataPackage") {
    if (const Json* games = cmd.get("data") ? cmd.get("data")->get("games") : nullptr; games != nullptr) {
      for (const auto& [game, pkg] : games->fields) {
        if (const Json* items = pkg.get("item_name_to_id"); items != nullptr) {
          for (const auto& [n, id] : items->fields) {
            mItemNames[game][int64_t(id.n)] = n;
          }
        }
        if (const Json* locs = pkg.get("location_name_to_id"); locs != nullptr) {
          for (const auto& [n, id] : locs->fields) {
            mLocationNames[game][int64_t(id.n)] = n;
          }
        }
      }
    }
  } else if (name == "PrintJSON") {
    // the parts' text, ids as names
    std::string text;
    if (const Json* parts = cmd.get("data"); parts != nullptr) {
      for (const Json& p : parts->items) {
        const std::string type = p.str("type");
        const std::string t = p.str("text");
        const int owner = int(num(p.get("player"), mSlot));
        if (type == "player_id") {
          text += playerName(std::atoi(t.c_str()));
        } else if (type == "item_id") {
          text += itemName(std::atoll(t.c_str()), owner);
        } else if (type == "location_id") {
          text += locationName(std::atoll(t.c_str()), owner);
        } else {
          text += t;
        }
      }
    }
    if (!text.empty() && mCallbacks.print) {
      mCallbacks.print(text);
    }
  } else if (name == "Bounced") {
    bool death = false;
    if (const Json* tags = cmd.get("tags"); tags != nullptr) {
      for (const Json& t : tags->items) {
        death |= t.s == "DeathLink";
      }
    }
    const Json* data = cmd.get("data");
    if (death && data != nullptr && mCallbacks.deathLink && data->str("source") != mSettings.slot) {
      mCallbacks.deathLink(data->str("source"), data->str("cause"));
    }
  }
}

void Client::checkLocations(const std::vector<int64_t>& locations) {
  if (locations.empty() || status() != Status::Connected) {
    return;
  }
  Json ids = port_json::array();
  for (int64_t l : locations) {
    ids.items.push_back(port_json::number(double(l)));
  }
  send(port_json::object({{"cmd", port_json::string("LocationChecks")}, {"locations", ids}}));
}

void Client::goal() {
  // ClientStatus.CLIENT_GOAL
  send(port_json::object({{"cmd", port_json::string("StatusUpdate")}, {"status", port_json::number(30)}}));
}

void Client::sendDeathLink(const std::string& cause) {
  const double now = std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
  send(port_json::object({
      {"cmd", port_json::string("Bounce")},
      {"tags", port_json::array({port_json::string("DeathLink")})},
      {"data", port_json::object({{"time", port_json::number(now)}, {"source", port_json::string(mSettings.slot)},
                                  {"cause", port_json::string(cause)}})},
  }));
}

void Client::setValue(const std::string& key, Json value) {
  send(port_json::object({
      {"cmd", port_json::string("Set")},
      {"key", port_json::string(key)},
      {"default", port_json::number(0)},
      {"want_reply", port_json::boolean(false)},
      {"operations", port_json::array({port_json::object(
                         {{"operation", port_json::string("replace")}, {"value", std::move(value)}})})},
  }));
}

void Client::say(const std::string& text) {
  send(port_json::object({{"cmd", port_json::string("Say")}, {"text", port_json::string(text)}}));
}

std::string Client::playerName(int slot) const {
  const auto it = mPlayers.find(slot);
  return it != mPlayers.end() ? it->second : "player " + std::to_string(slot);
}

std::string Client::itemName(int64_t id, int slot) const {
  const auto g = mSlotGames.find(slot);
  if (g != mSlotGames.end()) {
    if (const auto t = mItemNames.find(g->second); t != mItemNames.end()) {
      if (const auto it = t->second.find(id); it != t->second.end()) {
        return it->second;
      }
    }
  }
  return "item " + std::to_string(id);
}

std::string Client::locationName(int64_t id, int slot) const {
  const auto g = mSlotGames.find(slot);
  if (g != mSlotGames.end()) {
    if (const auto t = mLocationNames.find(g->second); t != mLocationNames.end()) {
      if (const auto it = t->second.find(id); it != t->second.end()) {
        return it->second;
      }
    }
  }
  return "location " + std::to_string(id);
}

} // namespace port_ap
