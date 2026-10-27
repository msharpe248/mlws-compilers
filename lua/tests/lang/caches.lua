-- Inline caches of field reads and method calls: the same sites, called
-- with tables whose shapes, metatables and classes change.

local function getx(t) return t.x end
local function call(o) return o:name() end

-- field reads across shapes, own maps and missing keys
local shapes = {
  {x = 1}, {y = 2, x = 3}, {x = 4, y = 5}, {}, {x = nil}, {z = 1},
  setmetatable({}, {__index = {x = "inherited"}}),
  setmetatable({}, {__index = function(_, k) return "fn " .. k end}),
}
local big = {}
for i = 1, 40 do big["k" .. i] = i end
big.x = "big"
shapes[#shapes + 1] = big
for round = 1, 3 do
  local out = {}
  for i, t in ipairs(shapes) do out[i] = tostring(getx(t)) end
  print(round, table.concat(out, " "))
end
-- a key removed and re-added, and a table that is compacted
local t = {x = 1, y = 2}
print(getx(t)); t.x = nil; print(getx(t)); t.x = 9; print(getx(t))
for i = 1, 30 do t["d" .. i] = i end
for i = 1, 30 do t["d" .. i] = nil end
print(getx(t)); t.x = 10; print(getx(t))

-- methods
local A = {}
A.__index = A
function A:name() return "A" end
local B = setmetatable({}, {__index = A})
B.__index = B
local C = {}
C.__index = C
function C:name() return "C" end

local a1, a2 = setmetatable({v = 1}, A), setmetatable({v = 2}, A)
local b1 = setmetatable({v = 3}, B)
local c1 = setmetatable({v = 4}, C)
local e1 = setmetatable({}, A)           -- no hash part
local objs = {a1, a2, b1, c1, e1}
local function names()
  local out = {}
  for i, o in ipairs(objs) do out[i] = call(o) end
  return table.concat(out, " ")
end
print(names(), names())
setmetatable(a2, C)                      -- same shape, other class
print(names())
a1.name = function() return "own" end    -- the object gets its own method
print(names())
a1.name = nil
print(names())
function A:name() return "A2" end       -- the class's method replaced
print(names())
function B:name() return "B" end         -- the subclass overrides it
print(names())
B.name = nil
print(names())
A.__index = {name = function() return "elsewhere" end}
print(names())
A.__index = function(_, k) return function() return "fn " .. k end end
print(names())
A.__index = A
print(names())
local grow = setmetatable({}, A)
objs = {grow}
for i = 1, 20 do grow["f" .. i] = i; io.write(call(grow), " ") end
print()

-- methods on strings and on values without them
print(("abc"):upper(), ("x"):rep(3))
print(pcall(call, 5))
print(pcall(call, setmetatable({}, {})))
print(pcall(call, setmetatable({}, {__index = {}})))

-- constructors with constant keys: nil values, then the tables change
local function mk(a, b) return {p = a, q = b, r = 3} end
for _, args in ipairs({{1, 2}, {nil, 2}, {nil, nil}, {false, nil}}) do
  local t = mk(args[1], args[2])
  local keys = {}
  for k, v in pairs(t) do keys[#keys + 1] = k .. "=" .. tostring(v) end
  table.sort(keys)
  t.s = 4; t.p = nil; t.q = 5
  local after = {}
  for k, v in pairs(t) do after[#after + 1] = k .. "=" .. tostring(v) end
  table.sort(after)
  print(table.concat(keys, ","), table.concat(after, ","), t.p, t.q, t.s)
end
local list = {}
for i = 1, 50 do
  local t = mk(i, nil)
  for j = 1, 12 do t["n" .. j] = j end
  for j = 1, 12 do t["n" .. j] = nil end
  list[i] = t.p + t.r
end
print(list[1], list[50], mk(1, 2).q, mk(nil, 7).q)
