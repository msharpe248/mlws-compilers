-- Arithmetic guessed to be on floats (from float constants, `/`, math
-- functions, fields and parameters), run on integers, strings and others.
local sqrt = math.sqrt
local function scale(v, k) return v.x * k + v.y / 2 end
local function mix(a, b)
  local s = a + b
  local d = a - b
  local p = a * b
  return s, d, p, a // b, a % b, -a, a * a
end
local fv = {x = 1.5, y = 2.0}
print(scale(fv, 0.5))
print(scale({x = 3, y = 4}, 2), scale({x = math.maxinteger, y = 0}, 2))
print(mix(7.5, 2.0))
print(mix(7, 2))
print(mix(-7, 2))
print(mix(math.maxinteger, 1))
print(mix(7, 2.5))
print(mix("10", 3))
print(pcall(mix, 7, 0))
print(mix(7.0, 0))
print(pcall(mix, {}, 1))
print(pcall(mix, 1, nil))
local mt = {__add = function() return "add" end, __sub = function() return "sub" end,
            __mul = function() return "mul" end, __idiv = function() return "idiv" end,
            __mod = function() return "mod" end, __unm = function() return "unm" end}
print(mix(setmetatable({}, mt), 1))

-- elements of a table passed to a function that stores floats
local function fill(t, n) for i = 1, n do t[i] = i / 2 end end
local function sum(t, n) local s = 0 for i = 1, n do s = s + t[i] * 2 end return s end
local u = {}
fill(u, 4)
print(sum(u, 4))
local w = {1, 2, 3, 4}
print(sum(w, 4), math.type(sum(w, 4)))
local r = sqrt(16)
print(r + 1, r * 2 // 1, math.type(r))
local n = 0
for i = 1, 10 do n = n + i * 0.5 end
print(n)
