-- Code generated twice: with facts from a check (parameters on entry,
-- locals used as tables in a loop) and as a generic copy.
local function sum(t, n)
  local s = 0
  for i = 1, n do s = s + t[i] end
  return s
end
print(sum({1, 2, 3}, 3), sum({1.5, 2.5}, 2), sum({1, 2, 3}, 2.0), sum({"1", 2}, 2))
print(pcall(sum, "abc", 1))
print(pcall(sum, setmetatable({}, {__index = function(_, i) return i * 10 end}), 3))
print(pcall(sum, {1, 2}, "2"), pcall(sum, {1, 2}, {}))

local function scale(p, k, dx)
  return p.x * k + dx, p.y * k - dx
end
print(scale({x = 1, y = 2}, 3, 0.5))
print(scale({x = 1, y = 2}, 3.5, 1))
print(scale(setmetatable({}, {__index = function(_, f) return #f end}), 2, 1))
print(pcall(scale, "xy", 1, 1))

-- a string indexed like a table, in a loop
local strs = {"abc", "de"}
local out = {}
for i = 1, #strs do
  local s = strs[i]
  local up, len = s.upper, s.len
  out[#out + 1] = up(s) .. len(s)
end
print(table.concat(out, " "))

-- locals used as tables, then something else in the same loop
local items = {{v = 1, w = 2}, {v = 3, w = 4}, "x", {v = 5, w = 6}}
local acc = {}
for i = 1, #items do
  local it = items[i]
  if type(it) ~= "table" then
    acc[#acc + 1] = "skip"
  else
    local f = function() return it.v + it.w end
    acc[#acc + 1] = it.v .. ":" .. it.w .. ":" .. f()
    if it.v == 3 then
      local c <close> = setmetatable({}, {__close = function() acc[#acc + 1] = "closed" end})
      acc[#acc + 1] = it.w
    end
  end
end
print(table.concat(acc, " "))

local function find(list, key)
  for i = 1, #list do
    local e = list[i]
    if e.k == key then return e.v end
    if e.stop then break end
  end
  return "none"
end
local l = {{k = "a", v = 1}, {k = "b", v = 2, stop = true}, {k = "c", v = 3}}
print(find(l, "a"), find(l, "b"), find(l, "c"), pcall(find, {1}, "a"))

-- recursion with the generic copy in between
local function fib(n) if n < 2 then return n end return fib(n - 1) + fib(n - 2) end
print(fib(20), fib(10.0), pcall(fib, "12"), pcall(fib, {}))
local function count(n, acc2) if n == 0 then return acc2 end return count(n - 1, acc2 + 1) end
print(count(100000, 0), count(10, 0.5))
