// A mod's scripts in text (docs/MODDING.md, "Scripts"): the game's own
// script macros, as the decomp's sources write them, assembled at load into
// the PC's script words.
//
//   EVT_BEGIN(room_init)
//       USER_FUNC(evt_mario_key_onoff, 0)
//       SET_VALUE(LW(0), ITEM_MUSHROOM)
//       IF_INT_EQUAL(GSW(0), 10)
//           CALL_CHILD(gor_01_init_evt)
//       END_IF
//       RETURN
//   EVT_END()
//
// A name is one of the mod's scripts, a constant of the game's headers
// (port/mods/evt_table_gen.h) or a game function or variable; a file's
// static is written `file::name` where the name alone is ambiguous
// (gor_01::evt_door). Strings are the mod's (pointers to them).
#pragma once

#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace port_evt {

using Word = intptr_t; // EvtWord on the PC

struct Expr;

class Program {
public:
  Program();
  ~Program();
  // A file's scripts; false and a message (file:line: ...) on an error.
  bool add(const std::string& text, const std::string& file, std::string& error);
  // Every name to a value: the mod's scripts first, then `lookup` (a game
  // name or constant). Unresolved names are listed and become 0.
  bool link(const std::function<bool(const std::string&, Word&, std::string&)>& lookup,
            std::vector<std::string>& errors);
  // A script's words, once linked; nullptr if there's none of that name.
  Word* find(const std::string& name);
  size_t size() const { return mScripts.size(); }

private:
  struct Slot {
    size_t script, index;
    std::shared_ptr<Expr> expr;
    std::string where;
  };
  struct Script {
    std::string name;
    std::vector<Word> code;
  };
  std::vector<Script> mScripts;
  std::map<std::string, size_t> mByName;
  std::vector<Slot> mSlots;          // words waiting for a value (names)
  std::deque<std::string> mStrings;  // the mod's strings, at fixed addresses
};

} // namespace port_evt
