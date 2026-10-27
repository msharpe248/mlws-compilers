for i = 1, 10 do
  if i % 2 == 0 then goto continue end
  io.write(i, " ")
  ::continue::
end
print()
local i = 1
::top::
if i <= 3 then io.write("g", i, " ") i = i + 1 goto top end
print()
for i = 10, 1, -3 do io.write(i, " ") end print()
for i = 1, 0 do print("never") end
for i = 1.0, 2.0, 0.5 do io.write(i, " ") end print()
for i = math.maxinteger - 2, math.maxinteger do io.write(i, " ") end print()
for i = math.mininteger, math.mininteger + 2 do io.write(i, " ") end print()
for i = 1, 3.7 do io.write(i, " ") end print()
for i = 5, 1, -2.5 do io.write(i, " ") end print()
local n = 0 repeat n = n + 1 local done = n >= 3 until done print(n)
local w = 0 while true do w = w + 1 if w > 5 then break end end print(w)
for _, v in ipairs({1, 2, 3}) do for _, u in ipairs({1, 2}) do if u == 2 then break end io.write(v, u, " ") end end print()
local x = 5
if x > 10 then print("big") elseif x > 3 then print("medium") else print("small") end
print(x > 3 and "yes" or "no", nil and 1, false or nil, 1 and 2, nil or "d", not nil, not 0)
local t = {} for k = 1, 3 do local v = k * k t[k] = function() return v end end print(t[1](), t[2](), t[3]())
do local a = 1 do local a = 2 print(a) end print(a) end
local s = 0 for i = 1, 100 do if i > 50 then break end s = s + i end print(s)
local function f(n) for i = 1, n do if i == 3 then return i end end return -1 end print(f(5), f(2))
local start, stop, step = 1, 5, 2 for i = start, stop, step do io.write(i, " ") end print()
local fs = 0.1 for k = fs, 0.35, 0.1 do io.write(string.format("%.1f ", k)) end print()
do global function gf() return "global fn" end end
print(gf())
local k <const> = 42 print(k)
print(#arg, arg[0] ~= nil)
