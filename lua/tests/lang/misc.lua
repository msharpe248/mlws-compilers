local a, b, c = (function() return 1, 2, 3 end)()
print(a, b, c)
local x, y = 1
print(x, y)
local t = {}
t.a, t.b = 1, 2
local i = 1
i, t[i] = i + 1, 20
print(i, t[1], t[2])
local s = "x" .. 1 .. 2.0 .. tostring(true and "y")
print(s)
print(2^3^2, -2^2, not nil == true, 1 .. 2 == "12", "a" .. "b" == "ab")
print(1 < 2 == true, (1 < 2) == true, #"abc" + 1, -(-3), - -3)
local long = 0 for k = 1, 100 do long = long + k end print(long)
local function deep(n) if n == 0 then return 0 end return 1 + deep(n - 1) end
print(deep(10000))
print(pcall(function() local function inf(n) return 1 + inf(n + 1) end return inf(1) end))
local big = {} for k = 1, 30 do big[k] = k end print(select("#", table.unpack(big)), (select(30, table.unpack(big))))
local function many(...) local n = select("#", ...) return n, select(n, ...) end
print(many(table.unpack(big)))
local mt = setmetatable({}, {__index = string}) print(mt.upper("q"))
print(("%d"):rep(3, " "):format(1, 2, 3))
print(math.maxinteger // -1, math.mininteger // -1, 5 // -2, -5 // 2, 5 % -2, -5 % 2, 5.5 % -2)
print(1e15, 1e16, 123456.789e3, 0.1 + 0.2 == 0.3, 3 == 3.0000000000000001)
local n1, n2 = 10, 3 print(n1 / n2, n1 // n2, n1 % n2, n1 ^ 0.5)
goto done
print("skipped")
::done::
print("end")
