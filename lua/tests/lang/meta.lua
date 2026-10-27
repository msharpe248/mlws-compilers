local V = {}
V.__index = V
function V.new(x, y) return setmetatable({x = x, y = y}, V) end
function V.__add(a, b) return V.new(a.x + b.x, a.y + b.y) end
function V.__eq(a, b) return a.x == b.x and a.y == b.y end
function V.__lt(a, b) return a.x < b.x end
function V.__le(a, b) return a.x <= b.x end
function V.__tostring(v) return "(" .. v.x .. ", " .. v.y .. ")" end
function V.__len(v) return 2 end
function V.__call(v, k) return v[k] end
function V.__concat(a, b) return tostring(a) .. "|" .. tostring(b) end
function V.__unm(v) return V.new(-v.x, -v.y) end
function V:len2() return self.x * self.x + self.y * self.y end
local a, b = V.new(1, 2), V.new(3, 4)
print(tostring(a + b), a == V.new(1, 2), a ~= b, a < b, a <= b, b < a, #a, a("y"), a .. b, tostring(-a), a:len2())
local Animal = {} Animal.__index = Animal
function Animal.new(name) local o = setmetatable({}, Animal) o.name = name return o end
function Animal:speak() return self.name .. " makes a sound" end
local Dog = setmetatable({}, {__index = Animal}) Dog.__index = Dog
function Dog.new(name) local o = Animal.new(name) return setmetatable(o, Dog) end
function Dog:speak() return self.name .. " barks" end
print(Animal.new("cat"):speak(), Dog.new("rex"):speak(), Dog.new("x").name)
local log = {}
local proxy = setmetatable({}, {__index = function(t, k) log[#log + 1] = k return k .. "!" end,
                                 __newindex = function(t, k, v) rawset(t, k, v * 2) end})
print(proxy.foo, proxy.bar) proxy.z = 21 print(proxy.z, #log)
local defaults = setmetatable({}, {__index = {color = "red"}}) print(defaults.color, defaults.size)
local ro = setmetatable({}, {__newindex = function() error("read-only table") end})
print(pcall(function() ro.x = 1 end))
print(getmetatable("abc").__index == string, ("x"):upper())
local prot = setmetatable({}, {__metatable = "locked"}) print(getmetatable(prot), pcall(setmetatable, prot, {}))
local arith = setmetatable({}, {__add = function(a, b) return "add" end, __mul = function() return "mul" end,
  __div = function() return "div" end, __mod = function() return "mod" end, __idiv = function() return "idiv" end,
  __pow = function() return "pow" end, __band = function() return "band" end, __shl = function() return "shl" end,
  __bnot = function() return "bnot" end})
print(arith + 1, 1 + arith, arith * 2, arith / 2, arith % 2, arith // 2, arith ^ 2, arith & 1, arith << 1, ~arith)
local named = setmetatable({}, {__name = "MyType"}) print((tostring(named):gsub("0x%x+", "ADDR")))
local cnt = setmetatable({}, {__index = function(t, k) return k * 2 end}) print(cnt[21], cnt[1.5])
local chain = setmetatable({}, {__index = setmetatable({}, {__index = setmetatable({}, {__index = {deep = "yes"}})})})
print(chain.deep)
local closed = {}
do
  local x <close> = setmetatable({}, {__close = function() closed[#closed + 1] = "x" end})
  local y <close> = setmetatable({}, {__close = function() closed[#closed + 1] = "y" end})
end
print(table.concat(closed, ","))
local ps = setmetatable({}, {__pairs = function(t) return function(_, k) if not k then return 1, "one" end end, t, nil end})
for k, v in pairs(ps) do print("pairs", k, v) end
