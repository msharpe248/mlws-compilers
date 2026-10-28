-- Typed copies of local functions: arguments of known types are passed in
-- one register each, and a result that is always one value of one type is
-- returned alone.
local function fib(n)
  if n < 2 then return n end
  return fib(n - 1) + fib(n - 2)
end
print(fib(20), fib(20.0), fib(1), pcall(fib, "x"))

-- proper tail calls through a copy (would overflow the stack otherwise)
local function count(n, acc)
  if n == 0 then return acc end
  return count(n - 1, acc + 1)
end
print(count(1000000, 0), count(10, 0.5))

-- mutual recursion
local odd
local function even(n) if n == 0 then return true end return odd(n - 1) end
function odd(n) if n == 0 then return false end return even(n - 1) end
print(even(10), even(7))
local function isodd(n) if n == 0 then return false end return not isodd(n - 1) end
print(isodd(9), isodd(12))

-- arguments in every bank, in mixed orders
local function mix(a, t, x, s, b)
  return a + t.v + x + #s + b
end
local tt = {v = 100}
print(mix(1, tt, 2.5, "abc", 7), mix(1.5, tt, 2, "", 0))
local function tfirst(t, i) return t[i] end
print(tfirst({10, 20, 30}, 2), tfirst({x = 1}, 1))
local function flt(a, b, c) return a * b - c end
print(flt(1.5, 2.0, 0.25), flt(3, 4, 5), flt(1, 2.5, 3))

-- integers wrap; floats stay floats; division makes floats
local function sq(x) return x * x end
print(sq(3037000500), sq(1.5), sq(-7))
local function half(x) return x / 2 end
print(half(7), half(8), math.type(half(8)))
local function idiv(a, b) return a // b end
print(idiv(7, 2), idiv(-7, 2), pcall(idiv, 1, 0))

-- results that are not one value of one type stay generic
local function maybe(n) if n > 0 then return n end end
local function two(n) return n, n + 1 end
local function either(n) if n > 0 then return n else return "neg" end end
print(maybe(1), maybe(-1), two(5), either(1), either(-1))
print(select("#", maybe(-1)), select("#", fib(5)), select("#", two(1)))

-- a typed result in multi-value places
local function inc(x) return x + 1 end
local a, b, c = inc(1)
print(a, b, c)
local t = {inc(1), inc(2), inc(3)}
print(#t, t[1], t[3])
print(inc(1), inc(inc(inc(1))))
print(string.format("%d %d", inc(1), inc(2)))
local function pass(...) return select("#", ...), ... end
print(pass(inc(1)))
print(math.max(inc(5), inc(2)))

-- strings, tables, booleans and nil as known types
local function greet(name) return "hi " .. name end
print(greet("bob"), greet(5))
local function mk(n) return {n = n} end
print(mk(3).n, type(mk("x")))
local function neg(x) return not x end
print(neg(true), neg(nil), neg(0))

-- errors inside a copy report the right line
local function bad(n)
  if n > 3 then
    return n + nil
  end
  return bad(n + 1)
end
print(pcall(bad, 1))
local function deep(n) if n == 0 then error("bottom") end return deep(n - 1) + 1 end
print(pcall(deep, 50))

-- to-be-closed variables in a copy
local function closing(n)
  local c <close> = setmetatable({}, {__close = function() print("closed", n) end})
  return n * 2
end
print(closing(21))

-- loops and locals that change type
local function sum(n)
  local s = 0
  for i = 1, n do s = s + i end
  return s
end
local function fsum(n)
  local s = 0
  for i = 1, n do s = s + i / 2 end
  return s
end
local function swap(n)
  local x = 1
  if n > 5 then x = "big" end
  return x
end
print(sum(100), fsum(4), swap(3), swap(9))
local function loopret(n)
  while true do
    if n > 10 then return n end
    n = n * 2
  end
end
print(loopret(3), loopret(3.5))
