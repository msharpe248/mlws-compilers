-- string.format, gmatch, gsub, rep and concat
local t = {}
for i = 1, 200000 do t[#t + 1] = string.format("%d:%s", i, tostring(i * 2)) end
local s = table.concat(t, ",")
local n = 0
for w in s:gmatch("%d+:") do n = n + 1 end
local r = s:gsub("1", "one")
print(#s, n, #r, s:sub(1, 20), s:find("9999:", 1, true))
local parts = {}
for i = 1, 100000 do parts[i] = ("x"):rep(i % 10) .. i end
print(#table.concat(parts))
