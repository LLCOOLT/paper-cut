-- map.place / map.remove / map.edits (docs/MAP_EDITOR.md, Lua)
ttyd.on("map_load", function(room)
  print("mapedit-lua: map_load " .. room .. ", edits " .. tostring(map.edits()) .. ", can change " .. tostring(ttyd.can_change()))
  if room == "gor_01" then
    local a = map.place{type = "save_block", id = "lua_save", pos = {200, 60, -100}}
    local b = map.place{type = "sign", id = "lua_sign", x = 220, y = 0, z = 60, say = "Placed from Lua."}
    -- an unknown kind is an error (the mod's mistake), not a false
    local ok = pcall(map.place, {type = "no_such_kind", pos = {0, 0, -400}})
    local c = ok and "no error" or "error"
    print("mapedit-lua: placed save " .. tostring(a) .. ", sign " .. tostring(b) .. ", bad " .. c)
    -- numbers past what the game can take are refused (errors), never placed:
    -- flags past their arrays, items past any id, positions not finite
    -- (off Mario's spawn at 0,0,0: a wrongly placed item there holds him in its window)
    local refused, out = 0, {}
    for i, t in ipairs({
      {type = "item", item = 1, pos = {0, 0, -400}, flag_gswf = 1e9},
      {type = "item", item = 1, pos = {0, 0, -400}, flag_gswf = 1e12},
      {type = "item", item = 1, pos = {0, 0, -400}, flag_lswf = 5000},
      {type = "item", item = 1e12, pos = {0, 0, -400}},
      {type = "item", item = 1, pos = {1 / 0, 0, -400}},
      {type = "item", item = 1, x = 0 / 0, y = 0, z = -400},
      {type = "item", item = 1, pos = {50000, 0, -400}},
      {type = "npc", model = "c_kinopio", pos = {0, 0, -400}, shop = {1e9}},
      {type = "heart_block", pos = {0, 0, -400}, price = -5},
    }) do
      if pcall(map.place, t) then
        out[#out + 1] = tostring(i)
      else
        refused = refused + 1
      end
    end
    print("mapedit-lua: out of range refused " .. refused .. " of 9" ..
          (#out > 0 and (", placed " .. table.concat(out, " ")) or ""))
    print("mapedit-lua: removed sign " .. tostring(map.remove("lua_sign")) .. ", removed none " .. tostring(map.remove("nothing_here")))
    print("mapedit-lua: edits now " .. tostring(map.edits()))
  end
end)
