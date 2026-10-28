-- setmetatable, made inline when it is the library's
local A = {__index = {name = "A"}}
local B = {__index = {name = "B"}}

local function new(x) return setmetatable({x = x}, A) end
local o = new(1)
print(o.name, o.x, getmetatable(o) == A)

-- not a tail call, with the result used
local t = {}
local r = setmetatable(t, B)
print(r == t, t.name)
-- changing a metatable, and removing it
print(setmetatable(t, A).name, setmetatable(t, nil).name, getmetatable(t))
local none = nil
print(setmetatable(t, none) == t, getmetatable(t))

-- a protected metatable
local P = setmetatable({}, {__metatable = "locked"})
print(pcall(setmetatable, P, A))
print(pcall(function() return setmetatable(P, A) end))
print(pcall(function() local x = setmetatable(P, nil); return x end))
print(getmetatable(P))

-- bad arguments
print(pcall(function() return setmetatable(5, A) end))
print(pcall(function() return setmetatable({}, 5) end))
print(pcall(function() local m = "str"; return setmetatable({}, m) end))
print(pcall(function() local v; return setmetatable(v, A) end))

-- a value of either kind for the metatable
local function setm(v, m) return setmetatable(v, m) end
for _, m in ipairs({A, B}) do print(setm({}, m).name) end
print(getmetatable(setm({}, nil)))

-- a local alias, and a replacement that is not the library's
local smt = setmetatable
print(smt({}, B).name)
local real = setmetatable
setmetatable = function(t, m) print("mine", type(t), type(m)); return real(t, m) end
print(new(2).name)
local smt2 = setmetatable
print(smt2({}, A).name)
setmetatable = real
print(new(3).name)

-- many live values around it
local a1, a2, a3, a4, a5, a6, a7, a8 = 1, 2, 3, 4, 5, 6, 7, 8
local w = setmetatable({}, {__index = function(_, k) return k .. "!" end})
print(w.hi, a1 + a2 + a3 + a4 + a5 + a6 + a7 + a8)
