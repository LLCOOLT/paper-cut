-- Enemy HP list: an example Lua mod (docs/LUA_MODDING.md). It reads the
-- battle's units each frame and draws them with a module of its own
-- (lua/enemy_hp/panel.lua).

local panel = require "enemy_hp.panel"

-- "UNIT_SPIKY_GOOMBA" -> "Spiky Goomba": for a unit whose game text is empty
local function fromKind(name)
  return (name:sub(6):lower():gsub("_", " "):gsub("(%a)(%w*)", function(a, b) return a:upper() .. b end))
end

ttyd.on("hud", function()
  if not battle.active() then
    return
  end
  local rows = {}
  for _, id in ipairs(battle.units()) do
    local u = battle.unit(id)
    if u and u.alliance == battle.ENEMY_SIDE and u.max_hp > 0 then
      -- the game's own name for it, in the player's language (fine to show;
      -- for anything that changes the game use u.name, "UNIT_GOOMBA")
      local name = u.display_name
      if name == nil or name == "" then
        name = u.name and fromKind(u.name) or ("Enemy " .. u.kind)
      end
      rows[#rows + 1] = {name = name, hp = u.hp, max = u.max_hp}
    end
  end
  panel.draw(rows)
end)
