local Account = {}
Account.__index = Account
function Account.new(owner, balance) return setmetatable({owner = owner, balance = balance or 0, log = {}}, Account) end
function Account:deposit(v) self.balance = self.balance + v self.log[#self.log + 1] = "+" .. v end
function Account:withdraw(v)
  if v > self.balance then error("insufficient funds", 2) end
  self.balance = self.balance - v
  self.log[#self.log + 1] = "-" .. v
end
function Account:__tostring() return self.owner .. ": " .. self.balance end
local Savings = setmetatable({}, {__index = Account})
Savings.__index = Savings
Savings.__tostring = Account.__tostring
function Savings.new(owner, balance, rate) local s = Account.new(owner, balance) s.rate = rate return setmetatable(s, Savings) end
function Savings:interest() self:deposit(self.balance * self.rate) end
local a = Account.new("ann", 100)
a:deposit(50) a:withdraw(30)
print(tostring(a), table.concat(a.log, " "))
local s = Savings.new("sam", 1000, 0.05)
s:interest() s:withdraw(25)
print(tostring(s), s.balance, getmetatable(s) == Savings)
print(pcall(a.withdraw, a, 1000))
print(select(2, pcall(function() a:withdraw(1e6) end)))
local Stack = {}
Stack.__index = Stack
function Stack.new() return setmetatable({n = 0}, Stack) end
function Stack:push(...) for i = 1, select("#", ...) do self.n = self.n + 1 self[self.n] = select(i, ...) end return self end
function Stack:pop() local v = self[self.n] self[self.n] = nil self.n = self.n - 1 return v end
local st = Stack.new():push(1, 2, 3)
print(st:pop(), st:pop(), st.n)
local Set = {}
Set.__index = Set
local function set(t) local s = setmetatable({}, Set) for _, v in ipairs(t) do s[v] = true end return s end
Set.__add = function(a, b) local r = set{} for k in pairs(a) do r[k] = true end for k in pairs(b) do r[k] = true end return r end
Set.__mul = function(a, b) local r = set{} for k in pairs(a) do if b[k] then r[k] = true end end return r end
Set.__le = function(a, b) for k in pairs(a) do if not b[k] then return false end end return true end
Set.__lt = function(a, b) return a <= b and not (b <= a) end
Set.__eq = function(a, b) return a <= b and b <= a end
Set.__tostring = function(s) local l = {} for k in pairs(s) do l[#l + 1] = k end table.sort(l) return "{" .. table.concat(l, ", ") .. "}" end
local x, y = set{1, 2, 3}, set{2, 3, 4}
print(tostring(x + y), tostring(x * y), x <= x + y, x < x, x == set{3, 2, 1}, set{2} < x)
