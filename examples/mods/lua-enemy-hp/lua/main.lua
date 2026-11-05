-- Enemy HP list: an example Lua mod (docs/LUA_MODDING.md). It reads the
-- battle's units each frame and draws them with a module of its own
-- (lua/enemy_hp/panel.lua).

local panel = require "enemy_hp.panel"

local names = {} -- unit kinds by number, for the ones it knows
for _, name in ipairs({"UNIT_GOOMBA", "UNIT_PARAGOOMBA", "UNIT_SPIKY_GOOMBA", "UNIT_KOOPA_TROOPA",
                        "UNIT_PARATROOPA", "UNIT_BUZZY_BEETLE", "UNIT_SPIKE_TOP", "UNIT_BRISTLE",
                        "UNIT_PUFF", "UNIT_DULL_BONES", "UNIT_LAKITU", "UNIT_HYPER_GOOMBA"}) do
  local kind = ttyd.const(name)
  if kind then
    -- "UNIT_SPIKY_GOOMBA" -> "Spiky Goomba"
    names[kind] = name:sub(6):lower():gsub("_", " "):gsub("(%a)(%w*)", function(a, b) return a:upper() .. b end)
  end
end

ttyd.on("hud", function()
  if not battle.active() then
    return
  end
  local rows = {}
  for _, id in ipairs(battle.units()) do
    local u = battle.unit(id)
    if u and u.alliance == battle.ENEMY_SIDE and u.max_hp > 0 then
      rows[#rows + 1] = {name = names[u.kind] or ("Enemy " .. u.kind), hp = u.hp, max = u.max_hp}
    end
  end
  panel.draw(rows)
end)
