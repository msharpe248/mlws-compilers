-- A table's shape kept in registers while code reads its fields: any call
-- can change it (adding or removing keys, growing the hash part).
local function grow(t, n)
  for i = 1, n do t["k" .. i] = i end
end
local function drop(t, k) t[k] = nil end

-- a parameter, with calls that change it between reads
local function f(t)
  local a, b = t.a, t.b
  grow(t, 20)
  local c = t.a + t.b + t.k20
  drop(t, "a")
  t.b = t.b + 1
  local d = t.a
  t.a = 5
  return a, b, c, d, t.a, t.b, t.k1
end
print(f({a = 1, b = 2}))
print(f({b = 2, a = 1, z = 0}))
print(f(setmetatable({a = 1, b = 2}, {__index = function(_, k) return "idx:" .. k end})))

-- a loop over a local table
local o = {x = 1, y = 2}
local s = 0
for i = 1, 30 do
  s = s + o.x + o.y
  o.x = o.x + 1
  if i == 10 then grow(o, 30) end
  if i == 20 then o.x = nil; o.x = 100 end
  if i == 25 then o["new" .. i] = i end
end
print(s, o.x, o.y, o.k30, o.new25)

-- a table that is a local in a loop's body, with methods that change it
local C = {}
C.__index = C
function C:bump() self.n = (self.n or 0) + 1; self["f" .. self.n] = true end
function C:get() return self.v end
local objs = {}
for i = 1, 4 do objs[i] = setmetatable({v = i, w = -i}, C) end
local acc = {}
for i = 1, 4 do
  local ob = objs[i]
  local before = ob.v + ob.w
  ob:bump(); ob:bump()
  ob.v = ob.v * 10
  acc[#acc + 1] = before .. ":" .. ob.v .. ":" .. ob:get() .. ":" .. tostring(ob.f2) .. ":" .. ob.n
  setmetatable(ob, nil)
  acc[#acc + 1] = tostring(ob.get)
end
print(table.concat(acc, " "))

-- a callback that replaces fields of the table being read
local cfg = {a = 1, b = 2, c = 3}
local function each(t, fn)
  local r = {}
  for _, k in ipairs({"a", "b", "c"}) do
    r[#r + 1] = t.a + t.b + t.c
    fn(t, k)
  end
  return table.concat(r, ",")
end
print(each(cfg, function(t, k) t[k] = nil; t[k .. k] = 1; t[k] = 10 end))

-- many fields: a table with a map of its own
local big = {}
for i = 1, 40 do big["f" .. i] = i end
local function sum3(t) return t.f1 + t.f20 + t.f40, t.f1 * t.f40 end
print(sum3(big))
big.f20 = nil
print(pcall(sum3, big))

-- a coroutine that changes the table between resumes
local co = coroutine.wrap(function(t)
  local x = t.p
  coroutine.yield(x)
  return t.p + t.q
end)
local ct = {p = 1, q = 2}
print(co(ct))
ct.p = 50; grow(ct, 5)
print(co())
