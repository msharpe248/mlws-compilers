-- Loops that index tables whose array part changes: appends, shrinking,
-- library functions, metamethods and coroutines in between.
local function grow(t, n)
  for i = 1, n do
    t[#t + 1] = t[i] * 2         -- appends: the array part is reallocated
  end
  return #t, t[#t]
end
print(grow({1, 2, 3}, 6))

local function shrink(t)
  local s = 0
  for i = 1, #t do
    s = s + (t[i] or 100)
    if i == 2 then t[#t] = nil; t[#t] = nil end
  end
  return s, #t
end
print(shrink({1, 2, 3, 4, 5}))

local function lib(t)
  local out = {}
  for i = 1, 4 do
    if i == 2 then table.insert(t, 1, 0) end
    if i == 3 then table.remove(t) end
    out[#out + 1] = t[i]
    out[#out + 1] = t[i + 1]
  end
  return table.concat(out, ",")
end
print(lib({1, 2, 3, 4, 5}))

local function viafn(t, f)
  local s = 0
  for i = 1, 6 do
    s = s + t[i]
    f(t, i)
    s = s + t[i]
  end
  return s
end
print(viafn({1, 2, 3, 4, 5, 6}, function(t, i) t[i] = i * 10; t[#t + 1] = i end))
print(viafn({1, 2, 3, 4, 5, 6}, function(t, i) for k = 1, #t do t[k] = k end end))

local log = {}
local proxy = setmetatable({}, {__index = function(_, i) return -i end,
                                __newindex = function(t, i, v) log[#log + 1] = i; rawset(t, i, v) end})
local function fill(t)
  for i = 1, 5 do t[i] = t[i] * 2 end
  local s = 0
  for i = 1, 5 do s = s + t[i] end
  return s
end
print(fill(proxy), table.concat(log, " "))

local shared = {1, 2, 3}
local co = coroutine.wrap(function()
  for _ = 1, 3 do
    for i = 1, 3 do shared[#shared + 1] = i end
    coroutine.yield()
  end
end)
local function walk(t)
  local s, n = 0, 0
  for i = 1, 12 do
    if t[i] == nil then co() end
    s = s + (t[i] or 0)
    n = n + 1
  end
  return s, n, #t
end
print(walk(shared))

-- while and repeat loops, and nested loops over the same tables
local a, b = {}, {}
for i = 1, 10 do a[i] = i end
local i = 1
while a[i] do b[i] = a[i] + (b[i - 1] or 0); i = i + 1 end
repeat i = i - 1; a[i] = b[i] - a[i] until i == 1
local m = {}
for r = 1, 3 do
  m[r] = {}
  for c = 1, 3 do m[r][c] = a[r] * b[c] end
end
print(table.concat(b, " "), table.concat(a, " "), m[2][3], m[3][3])
