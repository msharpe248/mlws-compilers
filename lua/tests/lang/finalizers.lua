-- __gc: a table whose metatable has __gc when setmetatable is called is
-- finalized once nothing holds it, and the ones left when the program ends
-- are finalized then, the last marked first. (The tables are made by a
-- function that has returned, called through a table so that the optimizer
-- cannot inline it: a register of a running function keeps what it last
-- held.)
local log = {}
local mt = {__gc = function(o) log[#log + 1] = o.name end}
local F = {make = function(name, m) setmetatable({name = name}, m) end}
local function make(name, m) F.make(name, m) end

-- unreachable tables, collected explicitly
for i = 1, 3 do make("tmp" .. i, mt) end
collectgarbage()
table.sort(log)
print(table.concat(log, " "))

-- __gc added after setmetatable does not mark the table
log = {}
local late = {}
make("late", late)
late.__gc = function(o) log[#log + 1] = o.name end
collectgarbage()
print(#log)

-- __gc that is not a function is ignored
make("odd", {__gc = 42})
collectgarbage()

-- the finalizer can bring the object back; it is not finalized again
-- unless setmetatable marks it again
local saved, count = nil, 0
local mt2 = {__gc = function(o) saved = o; count = count + 1 end}
make("back", mt2)
collectgarbage()
print(saved and saved.name, count)
saved = nil
collectgarbage()
print(saved, count)
local function again()
  local o = saved
  saved = nil
  setmetatable(o, mt2)
end
make("back2", mt2)
collectgarbage()
again()
collectgarbage()
print(saved and saved.name, count)

-- errors in a finalizer are dropped, even caught ones
make("err", {__gc = function() error("boom") end})
make("perr", {__gc = function() log[#log + 1] = select(2, pcall(error, "caught", 0)) end})
collectgarbage()
print(log[#log])

-- results in flight are kept
local function three() return 1, 2, 3 end
local a, b, c = three()
collectgarbage()
print(a, b, c)

-- reachable ones run when the program ends, the last marked first
local keep = {}
for i = 1, 3 do
  keep[i] = setmetatable({}, {__gc = function() print("end " .. i) end})
end
print("done")
