-- The enemy list's drawing: a module (require "enemy_hp.panel").
local panel = {}

local X, Y, W, ROW = 440, 60, 184, 26

function panel.draw(rows)
  if #rows == 0 then
    return
  end
  hud.rect(X - 8, Y - 6, W + 16, #rows * ROW + 8, 0x00000080)
  for i, r in ipairs(rows) do
    local y = Y + (i - 1) * ROW
    hud.text(X, y, string.format("%s  %d/%d", r.name, r.hp, r.max), 16)
    local part = r.max > 0 and math.max(0, r.hp) / r.max or 0
    hud.rect(X, y + 19, W, 4, 0x404040C0)
    local color = part > 0.5 and 0x60D060FF or part > 0.25 and 0xE0C040FF or 0xE05040FF
    hud.rect(X, y + 19, W * part, 4, color)
  end
end

return panel
