-- the Paper (0x02), Tube (0x03) and Plane (0x04) Curses into the key items on the first map
local given = false
ttyd.on("map_load", function(room)
  if not given then
    given = true
    print("test-paperforms: gave " .. tostring(items.give(2)) .. " " .. tostring(items.give(3)) .. " " ..
          tostring(items.give(4)))
  end
end)
