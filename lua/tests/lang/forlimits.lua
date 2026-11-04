-- Numeric for loops at the integer limits, with closures, goto and break.
local M, m = math.maxinteger, math.mininteger
local function run(a, b, c)
  local n, last = 0, nil
  if c then for i = a, b, c do n = n + 1; last = i end
  else for i = a, b do n = n + 1; last = i end end
  return n, last
end
print(run(M - 2, M))
print(run(m, m + 2))
print(run(M - 2, M, 1))
print(run(m + 2, m, -1))
print(run(M, M - 2, -1))
print(run(M, M))
print(run(m, m, -1))
print(run(3, 1))
print(run(1, 3, -1))
for i = M - 1, M do io.write(i, " ") end print()
for i = m + 1, m, -1 do io.write(i, " ") end print()
local fs = {}
for i = 1, 3 do fs[i] = function() return i end end
print(fs[1](), fs[2](), fs[3]())
for i = 1, 10 do if i % 2 == 0 then goto cont end; io.write(i, " "); if i > 6 then break end ::cont:: end print()
local s = 0
for i = 10, 1, -1 do for j = i, 1, -1 do s = s + j end end print(s)
