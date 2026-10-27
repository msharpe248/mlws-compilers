local function counter()
  local n = 0
  return function() n = n + 1 return n end
end
local c1, c2 = counter(), counter()
print(c1(), c1(), c2(), c1())
local fns = {}
for i = 1, 3 do fns[i] = function() return i end end
print(fns[1](), fns[2](), fns[3]())
local acc = {}
local j = 0
while j < 3 do j = j + 1 local k = j acc[j] = function() return k * 10 end end
print(acc[1](), acc[2](), acc[3]())
local function fib(n) if n < 2 then return n end return fib(n - 1) + fib(n - 2) end
print(fib(20))
local function make_adder(x) return function(y) return x + y end end
local add5 = make_adder(5) print(add5(10), make_adder(1.5)(2))
local function outer() local a = 1 local function mid() local function inner() a = a + 1 return a end return inner end return mid() end
local inc = outer() print(inc(), inc())
local function va(...) return select("#", ...), ... end
print(va(1, nil, 3))
local function sum(...) local s = 0 for _, v in ipairs({...}) do s = s + v end return s end
print(sum(1, 2, 3, 4, 5))
local function multi() return 1, 2, 3 end
local x, y, z, w = multi() print(x, y, z, w)
print(multi(), multi())
print((multi()))
local t = {multi(), multi()} print(#t)
local function tail(n) if n == 0 then return "done" end return tail(n - 1) end
print(tail(100000))
local function many(a, b, c, d, e, f, g, h, i, j) return a + b + c + d + e + f + g + h + i + j end
print(many(1, 2, 3, 4, 5, 6, 7, 8, 9, 10))
local function opt(a, b) b = b or "default" return a, b end
print(opt(1), opt(1, 2))
local shared = 0
local function a() shared = shared + 1 end
local function b() shared = shared * 10 end
a() b() a() print(shared)
local obj = {v = 7}
function obj:get() return self.v end
function obj.static(x) return x * 2 end
print(obj:get(), obj.static(4))
local function rec(n, ...) if n == 0 then return ... end return rec(n - 1, n, ...) end
print(rec(5))
print(select("#", nil, nil), select("#"))
local function named(...t) return t.n, t[1], #t end
print(named(4, 5, 6))
