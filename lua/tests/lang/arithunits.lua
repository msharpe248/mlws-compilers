-- Arithmetic expressions of several operators, generated as a unit: each
-- operand's type checked once, then the operators; anything else falls
-- back to one operator at a time.
local function poly(x, y) return x * x + 2 * x * y - y / 4 + -x end
local function sq(a, b, c) return a * a + b * b + c * c end
local function pw(x) return x ^ 2 * 3 - 1 end
for _, args in ipairs({{3, 4}, {1.5, 2.5}, {3, 2.5}, {"2", 3}, {math.maxinteger, 1},
                       {math.mininteger, -1}, {0.0, -0.0}, {1/0, 1}}) do
  print(poly(args[1], args[2]), sq(args[1], args[2], args[1]), pw(args[1]))
end
print(math.type(sq(1, 2, 3)), math.type(sq(1, 2, 3.0)), math.type(poly(1, 4)))
print(pcall(poly, nil, 1))
print(pcall(poly, 1, {}))
print(pcall(sq, 1, 2, "x"))
print(pcall(function(t) return t.a * t.b + t.c end, {a = 1, b = 2}))

-- metamethods, in Lua's order
local log = {}
local mt = {}
for _, ev in ipairs({"add", "sub", "mul", "div", "unm", "pow"}) do
  mt["__" .. ev] = function(a, b)
    log[#log + 1] = ev
    return setmetatable({}, mt)
  end
end
local v = setmetatable({}, mt)
local _ = v * 2 + v - -v / 3 ^ v
print(table.concat(log, " "))

-- operands with side effects come first, before any operator
local order = {}
local function f(n) order[#order + 1] = n; return n end
local t = setmetatable({}, {__index = function(_, k) order[#order + 1] = k; return 10 end})
print(f(1) * f(2) + 3, t.x * t.y - 1, (f(3) + 1) * f(4))
print(table.concat(order, " "))
print(pcall(function() local a; return (a + 1) * f(5) end))
print(table.concat(order, " "))

-- in loops, with the variables changing type
local acc, k = 0, 1
for i = 1, 6 do
  acc = acc * 2 + i - k * 0.5
  if i == 3 then k = 2 end
  if i == 4 then acc = "7" end
end
print(acc, math.type(acc))
local s = 0
for i = 1, 100 do s = s + i * i - i / 2 end
print(s)
