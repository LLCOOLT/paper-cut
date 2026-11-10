-- "Always" scene tests on a file that has seen nothing: as test-give, but no Crystal Star held before the
-- scene (pik_03's Garnet Star is the only one: a first star's fanfare) and on tik_05 only the first (the
-- first pedestal scene). The bag's state printed for the log.
local given = false
ttyd.on("map_load", function(room)
  if given then
    return
  end
  given = true
  player.set_coins(100)
  local list = {52, 53, 87, 87, 87, 148, 148}
  if room == "tik_05" and items.count(0x72) == 0 then
    list[#list + 1] = 0x72
  end
  local ok = {}
  for _, id in ipairs(list) do
    ok[#ok + 1] = tostring(items.give(id))
  end
  local s = player.stats()
  print("test-give: gave " .. table.concat(ok, " ") .. " shine " .. tostring(s.shine_sprites) .. " coins " ..
        tostring(s.coins))
end)
