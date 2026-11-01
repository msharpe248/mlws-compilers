-- t[i] past the array part: nil inline for a plain table, else the hash part or __index
local t = {}
print(t[1], t[2])
t[5] = "five"
print(t[1], t[5])
local u = setmetatable({}, {__index = function(_, k) return k * 10 end})
print(u[1], u[3])
local v = {1, 2}
v[2] = nil
print(v[2], v[3])
local w = setmetatable({7}, {__index = {8, 9, 10}})
print(w[1], w[2], w[3])
local x = {x = 1}
x[1] = nil
print(x[1])
