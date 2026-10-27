local function lcg(seed) return function() seed = (seed * 1103515245 + 12345) % 2147483648 return seed end end
local rnd = lcg(42)
local a = {} for i = 1, 200 do a[i] = rnd() % 1000 end
local function heapsort(a)
  local n = #a
  local function sift(start, stop)
    local root = start
    while root * 2 <= stop do
      local child = root * 2
      if child < stop and a[child] < a[child + 1] then child = child + 1 end
      if a[root] < a[child] then a[root], a[child] = a[child], a[root] root = child else return end
    end
  end
  for start = n // 2, 1, -1 do sift(start, n) end
  for stop = n, 2, -1 do a[stop], a[1] = a[1], a[stop] sift(1, stop - 1) end
end
local b = {table.unpack(a)}
heapsort(a)
table.sort(b)
local same = true for i = 1, #a do if a[i] ~= b[i] then same = false end end
print(same, a[1], a[100], a[200], #a)
local words = {"pear", "Apple", "fig", "banana", "cherry", "apple"}
table.sort(words, function(x, y) return x:lower() < y:lower() or (x:lower() == y:lower() and x < y) end)
print(table.concat(words, " "))
local recs = {} for i = 1, 10 do recs[i] = {id = i, key = (i * 7) % 4} end
table.sort(recs, function(p, q) if p.key ~= q.key then return p.key < q.key end return p.id < q.id end)
local ids = {} for i, r in ipairs(recs) do ids[i] = r.id end print(table.concat(ids, ","))
local function qsort(t, lo, hi)
  if lo < hi then
    local p = t[hi] local i = lo - 1
    for j = lo, hi - 1 do if t[j] <= p then i = i + 1 t[i], t[j] = t[j], t[i] end end
    t[i + 1], t[hi] = t[hi], t[i + 1]
    qsort(t, lo, i) qsort(t, i + 2, hi)
  end
end
local c = {5.5, -1, 3, 2.25, 0, 10, -7.5} qsort(c, 1, #c) print(table.concat(c, " "))
