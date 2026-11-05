-- Crowded fields: an example Lua mod (docs/LUA_MODDING.md) for Master
-- Quest's hooks (they work with Master Quest off too) and the looks.
-- It changes fights, so online it runs only when both players have it.

local extra = ttyd.option("extra", 1)
local lead -- the kind leading the fight being set up

-- A field fight about to be set up: its enemies' kinds, the first first.
-- The area must have fights with a kind for it to come (Master Quest leaves
-- out the others), so the extra ones are more of the first.
ttyd.on("formation", function(area, map, units)
  lead = units[1]
  for _ = 1, extra do
    if #units < 5 then
      units[#units + 1] = lead
    end
  end
  print(string.format("%s: %d enemies, led by %s", map, #units, battle.kind_name(lead) or tostring(lead)))
  return units
end)

-- Each enemy as it enters such a fight, with Master Quest's changes for it
-- (all unchanged with Master Quest off): the leading kind one HP tougher.
ttyd.on("enemy_changes", function(area, kind, changes)
  if kind == lead then
    changes.hp_add = changes.hp_add + 1
  end
end)

-- Looks: how things look only, each player's own.
if ttyd.option("skin", 0) ~= 0 then
  local skins = looks.skins()
  if #skins > 0 then
    looks.set_skin(1, skins[1]) -- Goombella (partner 1)
    print("Goombella wears " .. skins[1])
  end
end
