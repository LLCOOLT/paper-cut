-- the Plane Curse (0x04) into the key items on the first map
local given = false
ttyd.on("map_load", function(room)
  if not given then
    given = true
    print("test-plane: gave " .. tostring(items.give(4)) .. " held " .. tostring(items.count(4)))
  end
end)
