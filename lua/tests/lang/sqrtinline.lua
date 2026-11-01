-- math.sqrt inline: floats, integers, a local alias, a replaced math.sqrt
local sqrt = math.sqrt
print(sqrt(2.25), sqrt(16), math.sqrt(0), math.sqrt(-1) ~= math.sqrt(-1))
print(math.type(sqrt(4)), sqrt("9"))
local s = 0
for i = 1, 100 do s = s + sqrt(i * i) end
print(s)
local function f(x) return sqrt(x) end
print(f(81))
local saved = math.sqrt
math.sqrt = function(x) return "mine " .. x end
print(math.sqrt(4))
math.sqrt = saved
print(math.sqrt(4))
