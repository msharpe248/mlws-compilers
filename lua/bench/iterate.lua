-- pairs and ipairs loops
local t = {}
for i = 1, 1000 do t["k" .. i] = i end
local s = 0
for r = 1, 2000 do for k, v in pairs(t) do s = s + v end end
local a = {} for i = 1, 1000 do a[i] = i end
for r = 1, 2000 do for i, v in ipairs(a) do s = s + v end end
print(s)
