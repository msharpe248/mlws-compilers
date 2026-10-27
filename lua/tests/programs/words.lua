local text = [[
It was the best of times, it was the worst of times, it was the age of wisdom,
it was the age of foolishness, it was the epoch of belief, it was the epoch of
incredulity, it was the season of Light, it was the season of Darkness, it was
the spring of hope, it was the winter of despair.
]]
local freq = {}
for word in text:lower():gmatch("%a+") do freq[word] = (freq[word] or 0) + 1 end
local words = {}
for w, n in pairs(freq) do words[#words + 1] = {w = w, n = n} end
table.sort(words, function(a, b) if a.n ~= b.n then return a.n > b.n end return a.w < b.w end)
for i = 1, 8 do print(string.format("%-12s %3d", words[i].w, words[i].n)) end
local caps = text:gsub("(%a)(%a*)", function(a, b) return a:upper() .. b end)
print(caps:sub(1, 60))
print(select(2, text:gsub("was", "was")), #text, text:find("wisdom"), text:match("season of (%a+)"))
local csv = "name,age,city\nAlice,30,Paris\nBob,25,Berlin"
for line in csv:gmatch("[^\n]+") do
  local fields = {}
  for f in line:gmatch("[^,]+") do fields[#fields + 1] = f end
  print(#fields, table.concat(fields, "|"))
end
print(("%d items at $%.2f = $%8.2f"):format(3, 9.99, 3 * 9.99))
print(("hello world"):rep(2, " - "), ("abc"):byte(-1), ("x=1, y=2"):gsub("(%w+)=(%w+)", "%2=%1"))
