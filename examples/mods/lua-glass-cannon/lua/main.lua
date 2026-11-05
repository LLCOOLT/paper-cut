-- Glass cannon: an example Lua mod (docs/LUA_MODDING.md) that changes the
-- game: the damage filter, a challenge rule, units' HP as they're made, and
-- coins. Online it runs only when both players have it (and the same options).

local power = ttyd.option("power", 150)
local tough = ttyd.option("tough_enemies", 0) ~= 0

-- no plain Guards; Superguards still work (the Challenges tab's rule, while this runs)
rules.set("challenge_guard", 1)

local function side(id)
  local u = id >= 0 and battle.unit(id)
  return u and u.alliance
end

-- every hit between Mario's side and the enemies, scaled (rounded, at least 1)
ttyd.on("damage", function(attacker, target, damage)
  if damage <= 0 then
    return damage
  end
  local a, t = side(attacker), side(target)
  if (a == battle.MARIO_SIDE and t == battle.ENEMY_SIDE) or (a == battle.ENEMY_SIDE and t == battle.MARIO_SIDE) then
    return math.max(1, (damage * power + 50) // 100)
  end
  return damage
end)

ttyd.on("unit_spawn", function(id, kind, alliance)
  if tough and alliance == battle.ENEMY_SIDE then
    local u = battle.unit(id)
    if u then
      battle.set_max_hp(id, u.max_hp * 3 // 2)
      battle.set_hp(id, u.max_hp * 3 // 2)
    end
  end
end)

ttyd.on("guard", function(unit, superguard)
  if superguard then
    player.add_coins(1)
  end
end)

print(string.format("damage at %d%%%s", power, tough and ", tough enemies" or ""))
