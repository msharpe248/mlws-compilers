local N = 8
local count = 0
local function ok(a, n, c)
  for i = 1, n - 1 do
    if a[i] == c or a[i] - i == c - n or a[i] + i == c + n then return false end
  end
  return true
end
local function addqueen(a, n)
  if n > N then
    count = count + 1
    if count <= 2 then
      local row = {}
      for i = 1, N do row[i] = a[i] end
      print(table.concat(row, " "))
    end
  else
    for c = 1, N do
      if ok(a, n, c) then a[n] = c addqueen(a, n + 1) end
    end
  end
end
addqueen({}, 1)
print("solutions", count)
