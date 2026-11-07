-- Weak tables (__mode): a collection removes an entry whose weak key or
-- value nothing else holds. Strings, numbers and booleans are values, so
-- they stay. (Objects are made by functions that have returned, called
-- through a table so that the optimizer cannot inline them: a register of
-- a running function keeps what it last held.)
local F = {}
function F.obj(name) return {name = name} end
function F.fill(t, n)
  for i = 1, n do t[F.obj("k" .. i)] = i end
end
local function count(t)
  local n = 0
  for _ in pairs(t) do n = n + 1 end
  return n
end

-- weak keys
local wk = setmetatable({}, {__mode = "k"})
local keep = F.obj("kept")
wk[keep] = "kept value"
wk.name = "a string key stays"
wk[1] = "an integer key stays"
F.fill(wk, 5)
collectgarbage()
print(count(wk), wk[keep], wk.name, wk[1])

-- weak values
local wv = setmetatable({}, {__mode = "v"})
local kv = F.obj("kept")
wv.a = kv
wv.s = "strings stay"
wv.n = 42
wv.f = 1.5
wv.b = false
function F.fillv(t, n) for i = 1, n do t[i] = F.obj("v" .. i) end end
F.fillv(wv, 4)
collectgarbage()
print(count(wv), wv.a == kv, wv.s, wv.n, wv.f, wv.b, wv[1])

-- both
local kvt = setmetatable({}, {__mode = "kv"})
kvt[keep] = kv
F.fill(kvt, 3)
function F.setx(t) t.x = F.obj("gone") end
F.setx(kvt)
collectgarbage()
print(count(kvt), kvt[keep] == kv)

-- ephemerons: a value that refers to its key does not keep it
local eph = setmetatable({}, {__mode = "k"})
function F.cycle(t) local o = {}; t[o] = {back = o} end
F.cycle(eph)
collectgarbage()
print(count(eph))

-- a weak table works as a table: #, ipairs, next, rawget, rawset, table.*
local arr = setmetatable({}, {__mode = "v"})
for i = 1, 5 do arr[i] = i * 10 end
print(#arr, rawlen(arr), rawget(arr, 3))
rawset(arr, 6, 60)
table.insert(arr, 70)
table.insert(arr, 1, 0)
print(table.concat(arr, ","), table.remove(arr), #arr)
local s = 0
for i, v in ipairs(arr) do s = s + i * v end
print(s, next({}), select("#", next(arr)))
local seen = {}
for k, v in pairs(arr) do seen[#seen + 1] = k .. "=" .. v end
table.sort(seen)
print(table.concat(seen, " "))
arr[2] = nil
arr[1.0] = "one"
print(arr[1], arr[2], #arr >= 1)

-- clearing fields while traversing
local t = setmetatable({a = 1, b = 2, c = 3, d = 4}, {__mode = "k"})
for k in pairs(t) do t[k] = nil end
print(next(t))

-- methods stored in a weak-valued table
local cls = setmetatable({}, {__mode = "v"})
local function hello(self) return "hello " .. self.who end
cls.greet = hello
cls.who = "weak"
print(cls:greet())

-- a table made strong again
local back = setmetatable({}, {__mode = "k"})
back[keep] = 1
back.x = 2
setmetatable(back, nil)
print(back[keep], back.x, count(back))
local strong = {}
F.fill(strong, 3)
setmetatable(strong, {__mode = "k"})
collectgarbage()
print(count(strong))
