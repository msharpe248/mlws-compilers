local W, H = 20, 10
local function new() local g = {} for y = 1, H do g[y] = {} for x = 1, W do g[y][x] = 0 end end return g end
local g = new()
g[2][3], g[3][4], g[4][2], g[4][3], g[4][4] = 1, 1, 1, 1, 1
local function step(g)
  local n = new()
  for y = 1, H do for x = 1, W do
    local c = 0
    for dy = -1, 1 do for dx = -1, 1 do
      if dy ~= 0 or dx ~= 0 then
        local yy, xx = (y + dy - 1) % H + 1, (x + dx - 1) % W + 1
        c = c + g[yy][xx]
      end
    end end
    n[y][x] = (c == 3 or (c == 2 and g[y][x] == 1)) and 1 or 0
  end end
  return n
end
for _ = 1, 8 do g = step(g) end
for y = 1, H do local row = {} for x = 1, W do row[x] = g[y][x] == 1 and "#" or "." end print(table.concat(row)) end
