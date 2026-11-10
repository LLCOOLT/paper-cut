-- three items into the bag and 100 coins on the first map; the bag and the storage counted every 300 frames
local given, n = false, 0
ttyd.on("map_load", function(room)
  if not given then
    given = true
    player.set_coins(100)
    print("test-items: gave " .. tostring(items.give(148)) .. " " .. tostring(items.give(132)) .. " " .. tostring(items.give(179)))
  end
end)
ttyd.on("frame", function()
  n = n + 1
  if n % 300 == 0 then
    print("test-items: held " .. #items.held() .. " stored " .. #items.stored())
  end
end)
