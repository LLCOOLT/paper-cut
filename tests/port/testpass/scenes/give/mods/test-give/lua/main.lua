-- on the first map: the Blimp Ticket (0x34), the Train Ticket (0x35), three Shine Sprites (0x57),
-- two Mushrooms (0x94) and 100 coins; on tik_05 the first two Crystal Stars (0x72-0x73), on pik_03 the
-- first five (0x72-0x76): the stars a file has there. The bag's state printed for the log.
local given = false
ttyd.on("map_load", function(room)
  if given then
    return
  end
  given = true
  player.set_coins(100)
  local list = {52, 53, 87, 87, 87, 148, 148}
  local stars = room == "tik_05" and 2 or room == "pik_03" and 5 or 0
  for i = 0, stars - 1 do
    if items.count(0x72 + i) == 0 then
      list[#list + 1] = 0x72 + i
    end
  end
  local ok = {}
  for _, id in ipairs(list) do
    ok[#ok + 1] = tostring(items.give(id))
  end
  local s = player.stats()
  print("test-give: gave " .. table.concat(ok, " ") .. " shine " .. tostring(s.shine_sprites) .. " coins " ..
        tostring(s.coins) .. " stars " .. tostring(stars))
end)
