local function permgen(a, n)
  n = n or #a
  if n <= 1 then coroutine.yield(a) else
    for i = 1, n do
      a[n], a[i] = a[i], a[n]
      permgen(a, n - 1)
      a[n], a[i] = a[i], a[n]
    end
  end
end
local function permutations(a)
  local co = coroutine.create(function() permgen(a) end)
  return function()
    local _, res = coroutine.resume(co)
    return res
  end
end
for p in permutations({"a", "b", "c"}) do print(table.concat(p)) end
local function producer()
  return coroutine.wrap(function() for i = 1, 5 do coroutine.yield("item" .. i) end end)
end
local function filter(src)
  return coroutine.wrap(function() for x in src do if not x:find("3") then coroutine.yield(x:upper()) end end end)
end
for x in filter(producer()) do io.write(x, " ") end print()
local sched = {}
local log = {}
local function task(name, n) return coroutine.create(function() for i = 1, n do log[#log + 1] = name .. i coroutine.yield() end end) end
sched[1], sched[2], sched[3] = task("a", 3), task("b", 2), task("c", 4)
while #sched > 0 do
  for i = #sched, 1, -1 do
    local co = sched[i]
    coroutine.resume(co)
    if coroutine.status(co) == "dead" then table.remove(sched, i) end
  end
end
print(table.concat(log, ","))
local fibgen = coroutine.wrap(function() local a, b = 0, 1 while true do coroutine.yield(a) a, b = b, a + b end end)
local fibs = {} for i = 1, 15 do fibs[i] = fibgen() end print(table.concat(fibs, " "))
local count = 0
for i = 1, 1000 do local co = coroutine.wrap(function(x) local y = coroutine.yield(x + 1) return y * 2 end) count = count + co(i) + co(3) end
print(count)
