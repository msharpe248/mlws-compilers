-- while loops, whose condition is tested before the loop and at its end.
local i, s = 0, 0
while i < 10 do s = s + i; i = i + 1 end
print(i, s)

-- a condition that fails at its second test reports the while's line
local t = {1, 2, 3}
local n = 0
print(pcall(function()
  while t[n + 1] < 10 do
    n = n + 1
  end
end))
print(n)

-- floats and NaN
local x = 0.5
while x < 4 do x = x * 2 end
print(x)
local nan = 0 / 0
local runs = 0
while nan < 1 do runs = runs + 1; break end
while not (nan >= 1) do runs = runs + 10; break end
print(runs)

-- and, or, not, strings
local a, b = 1, 10
while a < b and b > 5 or a == 0 do a = a + 1; b = b - 1 end
print(a, b)
local str = "aaaa"
while #str > 1 and str ~= "a" do str = str:sub(2) end
print(str)

-- break, goto continue, nested loops
local out = {}
local p = 0
while p < 10 do
  p = p + 1
  if p % 3 == 0 then goto continue end
  if p > 8 then break end
  local q = 0
  while q < p do q = q + 2 end
  out[#out + 1] = p .. ":" .. q
  ::continue::
end
print(table.concat(out, " "))

-- a global condition, changed in the body
G = 3
while G do G = G > 1 and G - 1 or nil end
print(G)

-- a condition that becomes a float
local k = 1
while k < 5 do k = k + 1.5 end
print(k)
