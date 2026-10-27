-- A call as the last argument: all of its results are passed, however many.
local function none() end
local function one() return 1 end
local function two() return 1, 2 end
local function three() return "a", nil, "c" end
local function nils() return nil, nil end
local function count(...) return select("#", ...) end
local function show(a, b, c, d) return tostring(a) .. tostring(b) .. tostring(c) .. tostring(d) end
local function fixed(a, b) return a, b end

for _, g in ipairs({none, one, two, three, nils}) do
  print(count(g()), count(0, g()), count(0, 0, g()),
        show(g()), show("x", g()), fixed(g()), fixed(9, g()))
  print(g())
  print("p", g())
end

-- methods and their objects
local Obj = {}
Obj.__index = Obj
function Obj:sum(a, b, c) return (a or 0) + (b or 0) + (c or 0) end
local o = setmetatable({}, Obj)
print(o:sum(one()), o:sum(two()), o:sum(none()), o:sum(5, two()))
print(o:sum(o:sum(1, 2)), o:sum(1, o:sum(2, two())))

-- tail position, nested, library functions, and a value that is not a function
local function tail(f, ...) return f(...) end
local function t1(x) return show(x, one()) end
local function t2(x) return fixed(x, two()) end
print(t1("q"), t2("r"))
print(tail(show, two()))
print(math.max(two()), math.max(5, two()), string.rep("ab", one()))
print(select(2, two()), select(-1, three()))
print(string.format("%s %s", two()))
print(pcall(function() local f = 5; return f(one()) end))
print(pcall(function() local t = {}; return t.nope(one()) end))
print(pcall(function() return o:missing(two()) end))

-- many arguments and many live values around the call
local function many(a, b, c, d, e, f, g)
  return a + b + c + d + e + f + (g or 100)
end
local x1, x2, x3, x4, x5, x6 = 1, 2, 3, 4, 5, 6
print(many(x1, x2, x3, x4, x5, x6, one()), many(x1, x2, x3, x4, x5, two()))
print(many(x1, x2, x3, x4, x5, x6, none()))

-- coroutines yielding inside the inner call
local co = coroutine.wrap(function()
  local function y() return coroutine.yield("in") end
  print("got", show("a", y()))
  print("got", count(y()))
end)
print(co())
print(co(1))
print(co(1, 2, 3))
