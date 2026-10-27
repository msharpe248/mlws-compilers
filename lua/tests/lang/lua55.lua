local function f(...args) return args.n, args[1], #args end
print(f(), f("a", "b"))
local function g(x, ...rest) return x, rest.n end
print(g(1, 2, 3))
do
  global print, string
  global<const> MAXV = 10
  print(MAXV)
end
local ok = pcall(function() global AlreadyHere = 1 end)
AlreadyHere2 = 5
print(pcall(function() global AlreadyHere2 = 6 end))
do
  global function gf() return "gf" end
  global print
  print(gf())
end
print(gf())
