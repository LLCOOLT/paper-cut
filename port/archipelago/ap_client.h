// An Archipelago client (archipelago.gg's network protocol): connects a slot
// to a multiworld server, receives its items, sends its checked locations.
// Networking runs on a thread of its own; everything a game sees happens in
// poll(), on the game's thread.
#pragma once

#include "port/json.h"

#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace port_ap {

class WebSocket;

struct NetworkItem {
  int64_t item = 0;
  int64_t location = 0;
  int player = 0;
  int flags = 0; // 1 progression, 2 useful, 4 trap
};

struct Settings {
  std::string server;   // host:port, ws://... or wss://...
  std::string slot;     // the player's name in the multiworld
  std::string password;
  std::string game;     // the game's name as its apworld has it
  int itemsHandling = 0b111;
  std::vector<std::string> tags;
};

enum class Status { Off, Connecting, Connected, Failed };

class Client {
public:
  struct Callbacks {
    // connected: the slot's data, the locations the server has as checked
    std::function<void(const port_json::Json& slotData, const std::set<int64_t>& checked)> connected;
    // every item the slot has, from index on (a reconnect starts at 0)
    std::function<void(int index, const std::vector<NetworkItem>& items)> items;
    // a text line for the player (another's find, chat, the server's)
    std::function<void(const std::string& text)> print;
    // a DeathLink from another player
    std::function<void(const std::string& source, const std::string& cause)> deathLink;
  };

  explicit Client(Callbacks callbacks);
  ~Client();

  void start(const Settings& settings);
  void stop();
  // on the game's thread: the queued packets, as callbacks
  void poll();

  Status status() const;
  std::string message() const; // why it failed, or where it is

  void checkLocations(const std::vector<int64_t>& locations);
  void goal();
  void sendDeathLink(const std::string& cause);
  void setValue(const std::string& key, port_json::Json value); // the server's data storage
  void say(const std::string& text);

  std::string playerName(int slot) const;
  std::string itemName(int64_t id, int slot) const;
  std::string locationName(int64_t id, int slot) const;
  int slot() const { return mSlot; }

private:
  void run(Settings settings);
  bool send(const port_json::Json& packet);
  void handle(const port_json::Json& cmd);
  void setStatus(Status s, std::string message);

  Callbacks mCallbacks;
  std::unique_ptr<WebSocket> mSocket;
  std::thread mThread;
  mutable std::mutex mLock;
  std::deque<port_json::Json> mInbox; // commands from the server
  Status mStatus = Status::Off;
  std::string mMessage;
  Settings mSettings;
  int mSlot = 0, mTeam = 0;
  std::map<int, std::string> mPlayers;       // slot -> alias
  std::map<int, std::string> mSlotGames;     // slot -> game
  std::map<std::string, std::map<int64_t, std::string>> mItemNames, mLocationNames; // by game
};

} // namespace port_ap
