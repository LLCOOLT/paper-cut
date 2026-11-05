-- Guard counter: an example Lua mod (docs/LUA_MODDING.md).
-- Events and the HUD only, so it may run online whatever the other player
-- has (mod.json "lua": {"online": "hud"}).

local battleGuards, battleSupers = 0, 0
local allGuards, allSupers = 0, 0

ttyd.on("battle_start", function()
  battleGuards, battleSupers = 0, 0
end)

ttyd.on("guard", function(unit, superguard)
  if superguard then
    battleSupers = battleSupers + 1
    allSupers = allSupers + 1
  else
    battleGuards = battleGuards + 1
    allGuards = allGuards + 1
  end
end)

-- Save states and rewind: the counts go back with the game (a Superguard
-- rewound away isn't counted twice).
ttyd.keep(function()
  return {battleGuards, battleSupers, allGuards, allSupers}
end, function(kept)
  if kept then
    battleGuards, battleSupers, allGuards, allSupers = kept[1], kept[2], kept[3], kept[4]
  end
end)

ttyd.on("battle_end", function()
  print(string.format("battle over: %d Guards, %d Superguards (%d and %d in all)",
                      battleGuards, battleSupers, allGuards, allSupers))
end)

ttyd.on("hud", function()
  if not battle.active() then
    return
  end
  hud.rect(16, 392, 196, 52, 0x00000090)
  hud.text(26, 397, string.format("Guards  %d  (%d)", battleGuards, allGuards), 18, 0x90E0FFFF)
  hud.text(26, 419, string.format("Superguards  %d  (%d)", battleSupers, allSupers), 18, 0xFFD830FF)
end)
