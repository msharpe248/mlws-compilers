-- Multiple assignment: the tables and keys of its targets are those
-- before any of its values or stores. (Where a call in the values changes
-- a captured local used as a key, Lua leaves the order undefined.)
local function show(t, n)
  local s = {}
  for i = 1, n do s[i] = tostring(t[i]) end
  return table.concat(s, " ")
end

-- swaps of locals' elements
local q = {1, 2, 3, 4, 5}
local i, j = 1, 5
repeat q[i], q[j] = q[j], q[i]; i = i + 1; j = j - 1 until i >= j
print(show(q, 5))

-- a local assigned by an earlier target
local k = 1
local t = {}
k, t[k] = k + 1, 20
print(k, t[1], t[2])
t[k], k = 30, 5
print(k, t[2], t[5])

-- a table local assigned by a target
local a, b = {}, {}
local u = a
u, u[1] = b, "x"
print(a[1], b[1])

-- a captured table local changed by a metamethod of the first store
local x, y = {}, {}
local cur = x
local m = setmetatable({}, {__newindex = function(_, key, v) rawset(y, key, v); cur = y end})
m[1], cur[2] = "m", "c"
print(x[2], y[2], y[1])

-- floats, strings and globals as keys
local f = 1.5
local g = {}
g[f], g["s"], G = "f", "s", "g"
print(g[1.5], g.s, G)
