local t = {10, 20, 30, x = 1, ["y z"] = 2, [1.0 + 1] = "two?"}
print(#t, t[1], t[2], t[3], t.x, t["y z"])
t[#t + 1] = 40
t[5] = 50 t[6] = 60
print(#t, t[5], t[6])
t[7] = nil
print(#t)
local keys = {}
for k, v in pairs({a = 1, b = 2, c = 3}) do keys[#keys + 1] = k .. "=" .. v end
table.sort(keys)
print(table.concat(keys, " "))
for i, v in ipairs({"a", "b", "c", nil, "e"}) do io.write(i, v, " ") end print()
local s = {}
for i = 10, 1, -1 do s[i] = i end
print(#s, s[1], s[10])
table.insert(s, 11) table.insert(s, 1, 0)
print(#s, s[1], s[2], s[12], table.remove(s), table.remove(s, 1), #s)
print(table.unpack({1, 2, 3}))
print(select("#", table.unpack({1, nil, 3}, 1, 3)), select(2, "a", "b", "c"), select(-1, "x", "y"))
local p = table.pack(1, nil, 3) print(p.n, p[1], p[2], p[3])
local m = {} m[1] = "a" m[3] = "c" m[2] = "b" print(#m, table.concat(m))
local f = {} f[1.5] = "f" f[-1] = "neg" f[0] = "zero" print(f[1.5], f[-1], f[0], f[3/2])
local n = 0 for _ in pairs(t) do n = n + 1 end print(n)
local big = {} for i = 1, 1000 do big[i] = i end local sum = 0 for i = 1, #big do sum = sum + big[i] end print(sum, #big)
for i = 1, 500 do big[i] = nil end print(#big == 1000 or #big == 0, big[1000])
local nested = {a = {b = {c = "deep"}}} print(nested.a.b.c)
local arr = {"x", "y", "z"} table.sort(arr, function(a, b) return a > b end) print(table.concat(arr))
local nums = {5, 2, 8, 1, 9, 3} table.sort(nums) print(table.concat(nums, ","))
print(next({}), type(next), rawlen({1, 2}), rawequal(t, t), rawget(t, "x"))
local mt = {}
local tt = setmetatable({}, mt) print(getmetatable(tt) == mt)
print(table.move({1, 2, 3}, 1, 3, 2)[3], table.move({1, 2, 3}, 1, 3, 3, {})[5])
local str_keys = {} str_keys["10"] = "s" str_keys[10] = "i" print(str_keys["10"], str_keys[10])
local tk = {} tk[true] = "T" tk[false] = "F" print(tk[true], tk[false])
