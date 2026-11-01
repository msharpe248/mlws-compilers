-- pairs through the array part, the hash part, and slots made dead
local t = {10, 20, 30, x = 1, y = 2, z = 3}
local ks, s = {}, 0
for k, v in pairs(t) do ks[#ks + 1] = tostring(k); s = s + v end
table.sort(ks)
print(table.concat(ks, " "), s)
t.y = nil
local n = 0
for k in pairs(t) do n = n + 1 end
print(n)
for k, v in pairs(t) do if k == "x" then t[k] = nil end end
n = 0
for k, v in pairs(t) do n = n + 1 end
print(n, t.x, t.z)
local e = {}
for k, v in pairs(e) do print("never") end
local h = {}
for i = 1, 100 do h["k" .. i] = i end
for i = 1, 100, 3 do h["k" .. i] = nil end
local c, sum = 0, 0
for k, v in pairs(h) do c = c + 1; sum = sum + v end
print(c, sum)
for _, v in pairs({a = false}) do print(v) end
