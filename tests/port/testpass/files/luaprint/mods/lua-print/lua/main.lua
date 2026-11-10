-- The test pass's Lua mod (lua_print_many): print with far more arguments than a C function's
-- 20 guaranteed stack slots (it wrote past Lua's stack once), then require found and missing.
local t = {}
for i = 1, 3000 do
  t[i] = ""
end
print(table.unpack(t))
local mixed = {}
for i = 1, 40 do
  mixed[#mixed + 1] = (i % 3 == 0) and {} or ((i % 3 == 1) and i or "s")
end
print(table.unpack(mixed))
print("lua-print: survived " .. #t)
local helper = require("parts.helper")
print("lua-print: required " .. tostring(helper.name))
local ok, err = pcall(require, "parts.missing")
print("lua-print: missing " .. tostring(ok) .. " " .. tostring(err))
-- ttyd.message: alternating texts reuse their strings (a menu may still hold either), a text over
-- 64 KB is refused with an error instead of kept outside Lua's memory cap
for i = 1, 2000 do
  ttyd.message("pc_luaprint_msg", (i % 2 == 0) and "Jump A" or "Jump B")
end
ttyd.message("pc_luaprint_msg", nil)
local big, bigErr = pcall(ttyd.message, "pc_luaprint_big", string.rep("x", 100000))
print("lua-print: message big " .. tostring(big) .. " " .. tostring(bigErr))
