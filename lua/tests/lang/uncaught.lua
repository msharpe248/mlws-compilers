print("before")
local function f() error("boom") end
f()
print("never")
