-- Lua patterns: find, match, gmatch and gsub, including the matcher's
-- shortcuts (skipping runs, filtering by a first character)
local function show(...) local t = table.pack(...) for i = 1, t.n do t[i] = tostring(t[i]) end return table.concat(t, " ") end
local s = "1:2,22:44,333:666,4444:8888"
local n = 0
for w in s:gmatch("%d+:") do n = n + 1 io.write(w, " ") end print(n)
print(show(s:find("%d+,%d+:")), show(s:find("4+:8+$")), show(s:find("%d*x")))
print(show(s:match("(%d+):(%d+),?$")), show(s:match("^(%d)")), show(s:match("()4()")))
print(show(("aaab"):find("a*b")), show(("aaab"):find("a-b")), show(("aaa"):find("a*$")))
print(show(("xaaay"):find("a+y")), show(("aaa"):match("^(a*)(a)$")), show(("  x"):find("%s*x")))
print(show(("hello world"):match("^(%w+)")), show(("[[x]]"):match("%[(%b[])%]")), show(("THE (quick) fox"):find("%f[%a]%a+")))
for k, v in ("a=1, bb=22, ccc=333"):gmatch("(%w+)=(%w+)") do io.write(k, "->", v, ";") end print()
for p, q in ("abc"):gmatch("()(.)") do io.write(p, q, " ") end print()
for w in ("one;;two;"):gmatch("([^;]*)") do io.write("[", w, "]") end print()
local it = ("a b"):gmatch("%a") print(it(), it(), it(), it())
print(select("#", ("x"):gmatch("y")()))
print(show(("abcabc"):gsub("b", "X")), show(("abc"):gsub("", "-")), show(("abc"):gsub("b*", "-")))
print(show(("hello world"):gsub("(%w+)", "<%1>")), show(("abc"):gsub("%w", "%0%0", 2)), show(("a.b.c"):gsub("%.", "/")))
print(show(("aaa"):gsub("a", "b", 0)), show(("x1y22z"):gsub("%d+", {["1"] = "one"})), show(("ab"):gsub(".", function(c) return c:byte() end)))
print(show(("plain text plain"):gsub("plain", "P")), show(("aaaa"):gsub("aa", "b")), show(("abc"):gsub("abc", "")))
print(show(("%d%%"):gsub("%%", "pct")), show(("a+b"):find("+", 1, true)), show(("a+b"):find("%+")))
-- a stateful iterator called through a generic for, once per step
local obj = {}
function obj:iter(m) local i = 0 return function(_, _) i = i + 1 if i <= m then return i end end end
for i in obj:iter(5) do io.write(i, " ") end print()
-- errors
print(pcall(string.find, "a", "%"))
print(pcall(string.find, "a", "[a"))
print(pcall(string.match, "a", "(a"))
print(pcall(string.gsub, "a", "(", "x"))
print(pcall(function() for w in ("abc"):gmatch("%b") do end end))
print(pcall(string.match, ("a"):rep(40), ("(a)"):rep(33)))
