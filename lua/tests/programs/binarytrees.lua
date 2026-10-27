-- binary trees (the Computer Language Benchmarks Game)
local function bottom_up(depth)
  if depth == 0 then return {} end
  depth = depth - 1
  return {bottom_up(depth), bottom_up(depth)}
end
local function check(tree)
  if tree[1] then return 1 + check(tree[1]) + check(tree[2]) end
  return 1
end
local N = tonumber(arg and arg[1]) or 12
local maxdepth = math.max(6, N)
local stretch = maxdepth + 1
print(string.format("stretch tree of depth %d\t check: %d", stretch, check(bottom_up(stretch))))
local long_lived = bottom_up(maxdepth)
for d = 4, maxdepth, 2 do
  local iters = 2 ^ (maxdepth - d + 4)
  local c = 0
  for _ = 1, iters do c = c + check(bottom_up(d)) end
  print(string.format("%d\t trees of depth %d\t check: %d", iters, d, c))
end
print(string.format("long lived tree of depth %d\t check: %d", maxdepth, check(long_lived)))
