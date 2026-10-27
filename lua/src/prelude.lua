-- The part of the Lua library written in Lua (lua/src/prelude.lua). It is
-- compiled into every program and runs before it, with the table of
-- primitives P (see lib.masm) as its argument. Errors raised here are
-- reported at the line of the caller, as the C library's are.

local P = ...
local byte, sub, char, len = string.byte, string.sub, string.char, string.len
local concat, unpack = table.concat, table.unpack
local tostring, type, error, select, setmetatable = tostring, type, error, select, setmetatable
local mtype, floor, maxinteger = math.type, math.floor, math.maxinteger
local pfind = P.find

-- ------------------------------------------------------------- arguments

local function argerror(n, fname, msg)
  error("bad argument #" .. n .. " to '" .. fname .. "' (" .. msg .. ")")
end

local function typename(v)
  if v == nil then return "no value" end
  return type(v)
end

local function checkstr(v, n, fname)
  local t = type(v)
  if t == "string" then return v end
  if t == "number" then return tostring(v) end
  argerror(n, fname, "string expected, got " .. typename(v))
end

local function checkint(v, n, fname)
  if mtype(v) == "integer" then return v end
  local x = v
  if type(v) == "string" then x = tonumber(v) end
  if mtype(x) == "float" then
    local i = math.tointeger(x)
    if i then return i end
    argerror(n, fname, "number has no integer representation")
  elseif mtype(x) == "integer" then
    return x
  end
  argerror(n, fname, "number expected, got " .. typename(v))
end

local function optint(v, n, fname, def)
  if v == nil then return def end
  return checkint(v, n, fname)
end

-- ---------------------------------------------------------------- patterns

-- The matcher is P.match (match.masm); this is the rest of lstrlib.c.
local pmatch = P.match

local function isdigit(c) return c >= 48 and c <= 57 end
local function isspace(c) return c == 32 or (c >= 9 and c <= 13) end
local function isalnum(c) return (c >= 48 and c <= 57) or ((c | 32) >= 97 and (c | 32) <= 122) end

-- capture i (1-based) of a match of src from s to e (exclusive)
local function get_capture(src, caps, level, i, s, e)
  if i > level then
    if i ~= 1 then error("invalid capture index %" .. i) end
    return sub(src, s, e - 1)
  end
  local l = caps[2 * i]
  if l == -1 then error("unfinished capture") end
  local st = caps[2 * i - 1]
  if l == -2 then return st end
  return sub(src, st, st + l - 1)
end

local function captures(src, caps, level, s, e, whole)
  if level == 0 then
    if whole then return sub(src, s, e - 1) end
    return
  end
  if level == 1 then return get_capture(src, caps, 1, 1, s, e) end
  local t = {}
  for i = 1, level do t[i] = get_capture(src, caps, level, i, s, e) end
  return unpack(t, 1, level)
end

local SPECIALS = "^$*+?.([%-"

local function nospecials(pat)
  for i = 1, len(pat) do
    if pfind(SPECIALS, char(byte(pat, i)), 1) then return false end
  end
  return true
end

local function posrelat(i, n)
  if i > 0 then return i end
  if i == 0 then return 1 end
  if i < -n then return 1 end
  return n + i + 1
end

local function str_find_aux(s, pat, init, plain, find, fname)
  s = checkstr(s, 1, fname)
  pat = checkstr(pat, 2, fname)
  local ls = len(s)
  init = posrelat(optint(init, 3, fname, 1), ls)
  if init > ls + 1 then return nil end
  if find and (plain or nospecials(pat)) then
    local i = pfind(s, pat, init)
    if i then return i, i + len(pat) - 1 end
    return nil
  end
  local anchor = byte(pat, 1) == 94
  local caps = {}
  local e, level, s1 = pmatch(s, pat, init, anchor and 2 or 1, caps, not anchor, nil)
  if not e then return nil end
  if find then
    if level == 0 then return s1, e - 1 end
    return s1, e - 1, captures(s, caps, level, s1, e, false)
  end
  return captures(s, caps, level, s1, e, true)
end

function string.find(s, pat, init, plain)
  return str_find_aux(s, pat, init, plain, true, "find")
end

function string.match(s, pat, init)
  return str_find_aux(s, pat, init, false, false, "match")
end

function string.gmatch(s, pat, init)
  s = checkstr(s, 1, "gmatch")
  pat = checkstr(pat, 2, "gmatch")
  local ls = len(s)
  init = posrelat(optint(init, 3, "gmatch", 1), ls)
  if init > ls + 1 then init = ls + 1 end
  local caps = {}
  local src, lastmatch = init, nil
  return function()
    if src > ls + 1 then return nil end
    local e, level, start = pmatch(s, pat, src, 1, caps, true, lastmatch)
    if not e then
      src = ls + 2
      return nil
    end
    src = e
    lastmatch = e
    return captures(s, caps, level, start, e, true)
  end
end

local function add_value(src, caps, level, out, s, e, repl, tr)
  local whole = sub(src, s, e - 1)
  local v
  if tr == "string" then
    if not pfind(repl, "%", 1) then
      out[#out + 1] = repl
      return
    end
    local i, n = 1, len(repl)
    while i <= n do
      local j = pfind(repl, "%", i)
      if not j then
        out[#out + 1] = sub(repl, i)
        break
      end
      if j > i then out[#out + 1] = sub(repl, i, j - 1) end
      local c = byte(repl, j + 1)
      if c == 37 then
        out[#out + 1] = "%"
      elseif c and isdigit(c) then
        if c == 48 then
          out[#out + 1] = whole
        else
          out[#out + 1] = tostring(get_capture(src, caps, level, c - 48, s, e))
        end
      else
        error("invalid use of '%' in replacement string")
      end
      i = j + 2
    end
    return
  elseif tr == "table" then
    v = repl[captures(src, caps, level, s, e, true)]
  else
    v = repl(captures(src, caps, level, s, e, true))
  end
  if not v then
    out[#out + 1] = whole
  elseif type(v) == "string" or type(v) == "number" then
    out[#out + 1] = tostring(v)
  else
    error("invalid replacement value (a " .. type(v) .. ")")
  end
end

function string.gsub(s, pat, repl, max_n)
  s = checkstr(s, 1, "gsub")
  pat = checkstr(pat, 2, "gsub")
  local tr = type(repl)
  if tr == "number" then
    repl = tostring(repl)
    tr = "string"
  elseif not (tr == "string" or tr == "table" or tr == "function") then
    argerror(3, "gsub", "string/function/table expected, got " .. typename(repl))
  end
  local ls = len(s)
  max_n = optint(max_n, 4, "gsub", ls + 1)
  local out = {}
  local n = 0
  if tr == "string" and len(pat) > 0 and nospecials(pat) and not pfind(repl, "%", 1) then
    -- a plain string replaced by a plain string
    local src, lp = 1, len(pat)
    while n < max_n do
      local i = pfind(s, pat, src)
      if not i then break end
      if i > src then out[#out + 1] = sub(s, src, i - 1) end
      out[#out + 1] = repl
      n = n + 1
      src = i + lp
    end
    if src <= ls then out[#out + 1] = sub(s, src) end
    return concat(out), n
  end
  local anchor = byte(pat, 1) == 94
  local p = anchor and 2 or 1
  local caps = {}
  local src, lastmatch = 1, nil
  while n < max_n do
    local e, level, start = pmatch(s, pat, src, p, caps, not anchor, lastmatch)
    if not e then break end
    if start > src then out[#out + 1] = sub(s, src, start - 1) end
    n = n + 1
    add_value(s, caps, level, out, start, e, repl, tr)
    src = e
    lastmatch = e
    if anchor then break end
  end
  if src <= ls then out[#out + 1] = sub(s, src) end
  return concat(out), n
end

-- ------------------------------------------------------------------ format

local fmt = P.fmt

local function hexfloat(x, upper)
  if x ~= x then return upper and "NAN" or "nan" end
  if x == math.huge then return upper and "INF" or "inf" end
  if x == -math.huge then return upper and "-INF" or "-inf" end
  local sign = ""
  if x < 0 or (x == 0 and 1 / x < 0) then
    sign = "-"
    x = -x
  end
  if x == 0 then return sign .. (upper and "0X0P+0" or "0x0p+0") end
  local e = 0
  while x >= 2 do x = x / 2; e = e + 1 end
  while x < 1 do x = x * 2; e = e - 1 end
  local lead = 1
  if e < -1022 then
    -- subnormal
    while e < -1022 do x = x / 2; e = e + 1 end
    lead = 0
  end
  local m = floor((x - lead) * 2 ^ 52 + 0.5)
  local digits = fmt("{:013x}", m)
  digits = digits:gsub("0+$", "")
  local r = sign .. "0x" .. lead .. (digits ~= "" and ("." .. digits) or "") .. "p" .. (e >= 0 and "+" or "") .. e
  if upper then r = r:upper() end
  return r
end

local function quoted(v)
  local t = type(v)
  if t == "string" then
    local out = {'"'}
    local n = len(v)
    for i = 1, n do
      local c = byte(v, i)
      if c == 34 then out[#out + 1] = '\\"'
      elseif c == 92 then out[#out + 1] = "\\\\"
      elseif c == 10 then out[#out + 1] = "\\\n"
      elseif c == 13 then out[#out + 1] = "\\r"
      elseif c == 0 then
        local nx = byte(v, i + 1)
        if nx and isdigit(nx) then out[#out + 1] = "\\000" else out[#out + 1] = "\\0" end
      elseif c < 32 or c == 127 then
        local nx = byte(v, i + 1)
        if nx and isdigit(nx) then out[#out + 1] = fmt("\\{:03d}", c)
        else out[#out + 1] = "\\" .. c end
      else
        out[#out + 1] = char(c)
      end
    end
    out[#out + 1] = '"'
    return concat(out)
  elseif t == "number" then
    if mtype(v) == "integer" then
      if v == math.mininteger then return "0x8000000000000000" end
      return tostring(v)
    end
    if v == math.huge then return "1e9999" end
    if v == -math.huge then return "-1e9999" end
    if v ~= v then return "(0/0)" end
    if v == floor(v) and v >= -1e15 and v <= 1e15 then
      return fmt("{}", v) .. (fmt("{}", v):find("[%.e]") and "" or ".0")
    end
    return hexfloat(v)
  elseif t == "nil" or t == "boolean" then
    return tostring(v)
  end
  error("bad argument to 'format' (value has no literal form)")
end

local function pad(s, width, left, zero)
  local n = len(s)
  if not width or n >= width then return s end
  if left then return s .. string.rep(" ", width - n) end
  if zero then
    local sign = sub(s, 1, 1)
    if sign == "-" or sign == "+" or sign == " " then
      return sign .. string.rep("0", width - n) .. sub(s, 2)
    end
    return string.rep("0", width - n) .. s
  end
  return string.rep(" ", width - n) .. s
end

-- A format string, parsed: literal strings, and a table per conversion.
local function parse_format(fs)
  local items = {}
  local i, n = 1, len(fs)
  while i <= n do
    local j = pfind(fs, "%", i)
    if not j then
      items[#items + 1] = sub(fs, i)
      break
    end
    if j > i then items[#items + 1] = sub(fs, i, j - 1) end
    i = j + 1
    local c = byte(fs, i)
    if c == 37 then
      items[#items + 1] = "%"
      i = i + 1
    else
      local start = i
      local left, plus, space, alt, zero = false, false, false, false, false
      while true do
        c = byte(fs, i)
        if c == 45 then left = true
        elseif c == 43 then plus = true
        elseif c == 32 then space = true
        elseif c == 35 then alt = true
        elseif c == 48 then zero = true
        else break end
        i = i + 1
      end
      local width, prec
      local wd = 0
      while c and isdigit(c) do
        width = (width or 0) * 10 + c - 48
        wd = wd + 1
        i = i + 1
        c = byte(fs, i)
      end
      if c == 46 then
        i = i + 1
        c = byte(fs, i)
        prec = 0
        local pd = 0
        while c and isdigit(c) do
          prec = prec * 10 + c - 48
          pd = pd + 1
          i = i + 1
          c = byte(fs, i)
        end
        if pd > 2 then error("invalid conversion '%" .. sub(fs, start, i) .. "' to 'format'") end
      end
      if wd > 2 then error("invalid conversion '%" .. sub(fs, start, i) .. "' to 'format'") end
      i = i + 1
      local it = {c = c, left = left, plus = plus, space = space, alt = alt, zero = zero,
                  width = width, prec = prec, text = sub(fs, start, i - 1)}
      it.simple = not (left or plus or space or alt or zero or width or prec)
      if c then
        local lower = char(c):lower()
        it.fspec = "{:" .. (plus and "+" or "") .. (prec and ("." .. prec) or "") .. lower .. "}"
      end
      items[#items + 1] = it
    end
  end
  return items
end

local function format_one(it, arg, argi)
  local c, left, plus, space, alt, zero, width, prec =
    it.c, it.left, it.plus, it.space, it.alt, it.zero, it.width, it.prec
    local s
    if c == 100 or c == 105 then
      local v = checkint(arg, argi, "format")
      if prec then
        local digits = fmt("{}", v < 0 and -v or v)
        if len(digits) < prec then digits = string.rep("0", prec - len(digits)) .. digits end
        if prec == 0 and v == 0 then digits = "" end
        s = (v < 0 and "-" or (plus and "+" or (space and " " or ""))) .. digits
        s = pad(s, width, left, false)
      else
        s = fmt("{}", v)
        if v >= 0 then
          if plus then s = "+" .. s elseif space then s = " " .. s end
        end
        s = pad(s, width, left, zero)
      end
    elseif c == 120 or c == 88 or c == 111 then
      local v = checkint(arg, argi, "format")
      local conv = c == 120 and "x" or (c == 88 and "X" or "o")
      s = fmt("{:" .. conv .. "}", v)
      if v < 0 then
        -- two's complement, as C prints a 64-bit integer
        if conv == "o" then
          local hi = (v >> 33) & 0x7FFFFFFF
          local lo = v & 0x1FFFFFFFF
          s = fmt("{:o}", (v >> 63) & 1) .. fmt("{:021o}", (v >> 3) & 0x0FFFFFFFFFFFFFFF):sub(-21) .. fmt("{:o}", v & 7)
          s = s:gsub("^0+", "")
          local _ = hi + lo
        else
          local hi = (v >> 32) & 0xFFFFFFFF
          local lo = v & 0xFFFFFFFF
          s = fmt("{:" .. conv .. "}", hi) .. fmt("{:08" .. conv .. "}", lo)
        end
      end
      if prec and len(s) < prec then s = string.rep("0", prec - len(s)) .. s end
      if alt and v ~= 0 then
        if conv == "o" then s = "0" .. s else s = (conv == "x" and "0x" or "0X") .. s end
      end
      s = pad(s, width, left, zero and not prec)
    elseif c == 99 then
      s = char(checkint(arg, argi, "format"))
      s = pad(s, width, left, false)
    elseif c == 101 or c == 69 or c == 102 or c == 70 or c == 103 or c == 71 then
      local v = arg
      if type(v) ~= "number" then
        v = tonumber(v)
        if not v then argerror(argi, "format", "number expected, got " .. typename(arg)) end
      end
      v = v + 0.0
      local conv = char(c)
      if v ~= v or v == math.huge or v == -math.huge then
        if v ~= v then s = "nan" elseif v > 0 then s = "inf" else s = "-inf" end
        if (c == 69 or c == 70 or c == 71) then s = s:upper() end
        if v == v and v > 0 then
          if plus then s = "+" .. s elseif space then s = " " .. s end
        end
        s = pad(s, width, left, false)
      else
        local lower = conv:lower()
        s = fmt(it.fspec, v)
        if alt and lower ~= "g" and not s:find("%.") then
          local epos = s:find("e")
          if epos then s = s:sub(1, epos - 1) .. "." .. s:sub(epos) else s = s .. "." end
        end
        if conv ~= lower then s = s:upper() end
        if space and not plus and v >= 0 and not (1 / v < 0) then s = " " .. s end
        s = pad(s, width, left, zero)
      end
    elseif c == 97 or c == 65 then
      local v = arg
      if type(v) ~= "number" then argerror(argi, "format", "number expected, got " .. typename(arg)) end
      s = hexfloat(v + 0.0, c == 65)
      s = pad(s, width, left, zero)
    elseif c == 115 then
      s = tostring(arg)
      if prec then s = sub(s, 1, prec) end
      s = pad(s, width, left, false)
    elseif c == 113 then
      if width or prec or left or plus then
        error("specifier '%q' cannot have modifiers")
      end
      s = quoted(arg)
    elseif c == 112 then
      local t = type(arg)
      if t == "table" or t == "function" or t == "thread" then
        s = tostring(arg):match("0x%x+")
      else
        s = "(null)"
      end
      s = pad(s, width, left, false)
    else
      error("invalid conversion '%" .. it.text .. "' to 'format'")
    end
  return s
end

local fcache, fcount = {}, 0

function string.format(fs, ...)
  fs = checkstr(fs, 1, "format")
  local items = fcache[fs]
  if not items then
    items = parse_format(fs)
    if fcount < 1000 then
      fcache[fs] = items
      fcount = fcount + 1
    end
  end
  local nargs = select("#", ...)
  local args = {...}
  local out = {}
  local argi = 1
  for k = 1, #items do
    local it = items[k]
    if type(it) == "string" then
      out[k] = it
    else
      if argi > nargs then argerror(argi + 1, "format", "no value") end
      local arg = args[argi]
      argi = argi + 1
      local c = it.c
      if it.simple and c == 100 and mtype(arg) == "integer" then
        out[k] = tostring(arg)
      elseif it.simple and c == 115 and type(arg) == "string" then
        out[k] = arg
      else
        out[k] = format_one(it, arg, argi)
      end
    end
  end
  return concat(out)
end

-- ------------------------------------------------------------------- table

local function auxsort(t, lo, up, lt)
  while lo < up do
    if lt(t[up], t[lo]) then t[lo], t[up] = t[up], t[lo] end
    if up - lo == 1 then return end
    local p = (lo + up) // 2
    if lt(t[p], t[lo]) then
      t[p], t[lo] = t[lo], t[p]
    elseif lt(t[up], t[p]) then
      t[p], t[up] = t[up], t[p]
    end
    if up - lo == 2 then return end
    local pivot = t[p]
    t[p], t[up - 1] = t[up - 1], t[p]
    local i, j = lo, up - 1
    while true do
      i = i + 1
      while lt(t[i], pivot) do
        if i == up - 1 then error("invalid order function for sorting") end
        i = i + 1
      end
      j = j - 1
      while lt(pivot, t[j]) do
        if j < i then error("invalid order function for sorting") end
        j = j - 1
      end
      if j < i then break end
      t[i], t[j] = t[j], t[i]
    end
    t[up - 1], t[i] = t[i], t[up - 1]
    p = i
    if p - lo < up - p then
      auxsort(t, lo, p - 1, lt)
      lo = p + 1
    else
      auxsort(t, p + 1, up, lt)
      up = p - 1
    end
  end
end

local function default_lt(a, b) return a < b end

function table.sort(t, comp)
  if type(t) ~= "table" then argerror(1, "sort", "table expected, got " .. typename(t)) end
  local n = #t
  if n > 1 then
    if comp ~= nil and type(comp) ~= "function" then
      argerror(2, "sort", "function expected, got " .. typename(comp))
    end
    auxsort(t, 1, n, comp or default_lt)
  end
end

function table.move(a1, f, e, t, a2)
  if a2 == nil then a2 = a1 end
  if type(a1) ~= "table" then argerror(1, "move", "table expected, got " .. typename(a1)) end
  f = checkint(f, 2, "move")
  e = checkint(e, 3, "move")
  t = checkint(t, 4, "move")
  if type(a2) ~= "table" then argerror(5, "move", "table expected, got " .. typename(a2)) end
  if e >= f then
    if not (f > 0 or e < maxinteger + f) then argerror(3, "move", "too many elements to move") end
    if t > e or t <= f or a1 ~= a2 then
      for i = 0, e - f do a2[t + i] = a1[f + i] end
    else
      for i = e - f, 0, -1 do a2[t + i] = a1[f + i] end
    end
  end
  return a2
end

-- -------------------------------------------------------------------- math

function math.deg(x) return x * (180.0 / math.pi) end
function math.rad(x) return x * (math.pi / 180.0) end

-- -------------------------------------------------------------------- utf8

local utf8 = utf8 or {}
_ENV.utf8 = utf8
utf8.charpattern = "[\0-\x7F\xC2-\xFD][\x80-\xBF]*"

local function utf8_encode(c)
  if c < 0x80 then return char(c) end
  if c < 0x800 then return char(0xC0 | (c >> 6), 0x80 | (c & 0x3F)) end
  if c < 0x10000 then return char(0xE0 | (c >> 12), 0x80 | ((c >> 6) & 0x3F), 0x80 | (c & 0x3F)) end
  if c < 0x200000 then
    return char(0xF0 | (c >> 18), 0x80 | ((c >> 12) & 0x3F), 0x80 | ((c >> 6) & 0x3F), 0x80 | (c & 0x3F))
  end
  if c < 0x4000000 then
    return char(0xF8 | (c >> 24), 0x80 | ((c >> 18) & 0x3F), 0x80 | ((c >> 12) & 0x3F),
                0x80 | ((c >> 6) & 0x3F), 0x80 | (c & 0x3F))
  end
  return char(0xFC | (c >> 30), 0x80 | ((c >> 24) & 0x3F), 0x80 | ((c >> 18) & 0x3F),
              0x80 | ((c >> 12) & 0x3F), 0x80 | ((c >> 6) & 0x3F), 0x80 | (c & 0x3F))
end

function utf8.char(...)
  local n = select("#", ...)
  local out = {}
  for i = 1, n do
    local c = checkint((select(i, ...)), i, "char")
    if c < 0 or c > 0x7FFFFFFF then argerror(i, "char", "value out of range") end
    out[i] = utf8_encode(c)
  end
  return concat(out)
end

local limits = {[0] = 0xFFFFFFFF, 0x80, 0x800, 0x10000, 0x200000, 0x4000000}

-- decode the character at byte i: code point and the next position, or nil
local function utf8_decode(s, i, strict)
  local c = byte(s, i)
  if c == nil then return nil end
  if c < 0x80 then return c, i + 1 end
  if c < 0xC0 then return nil end
  local count = 0
  local res = 0
  while c & 0x40 ~= 0 do
    count = count + 1
    local cc = byte(s, i + count)
    if cc == nil or cc & 0xC0 ~= 0x80 then return nil end
    res = (res << 6) | (cc & 0x3F)
    c = c << 1
  end
  res = res | ((c & 0x7F) << (count * 5))
  if count > 5 or res > 0x7FFFFFFF or res < limits[count] then return nil end
  if strict then
    if res > 0x10FFFF or (res >= 0xD800 and res <= 0xDFFF) then return nil end
  end
  return res, i + count + 1
end

function utf8.codepoint(s, i, j, lax)
  s = checkstr(s, 1, "codepoint")
  local n = len(s)
  i = posrelat(optint(i, 2, "codepoint", 1), n)
  j = optint(j, 3, "codepoint", i)
  if j < 0 then j = n + j + 1 elseif j > n then j = n end
  if i < 1 then argerror(2, "codepoint", "out of bounds") end
  if j > n then argerror(3, "codepoint", "out of bounds") end
  if i > j then return end
  local out = {}
  local pos = i
  while pos <= j do
    local c, nx = utf8_decode(s, pos, not lax)
    if not c then error("invalid UTF-8 code") end
    out[#out + 1] = c
    pos = nx
  end
  return unpack(out)
end

function utf8.len(s, i, j, lax)
  s = checkstr(s, 1, "len")
  local n = len(s)
  i = posrelat(optint(i, 2, "len", 1), n)
  j = optint(j, 3, "len", -1)
  if j < 0 then j = n + j + 1 end
  if i < 1 or i > n + 1 then argerror(2, "len", "initial position out of bounds") end
  if j > n then argerror(3, "len", "final position out of bounds") end
  local count = 0
  local pos = i
  while pos <= j do
    local c, nx = utf8_decode(s, pos, not lax)
    if not c then return nil, pos end
    pos = nx
    count = count + 1
  end
  return count
end

local function iscont(s, i)
  local c = byte(s, i)
  return c ~= nil and c & 0xC0 == 0x80
end

function utf8.offset(s, n, i)
  s = checkstr(s, 1, "offset")
  n = checkint(n, 2, "offset")
  local ls = len(s)
  local defi = (n >= 0) and 1 or ls + 1
  i = optint(i, 3, "offset", defi)
  if i < 0 then i = ls + i + 1 end
  if i < 1 or i > ls + 1 then argerror(3, "offset", "position out of bounds") end
  local posi = i
  if n == 0 then
    while posi > 1 and iscont(s, posi) do posi = posi - 1 end
  else
    if iscont(s, posi) then error("initial position is a continuation byte") end
    if n < 0 then
      while n < 0 and posi > 1 do
        repeat posi = posi - 1 until not (posi > 1 and iscont(s, posi))
        n = n + 1
      end
    else
      n = n - 1
      while n > 0 and posi <= ls do
        repeat posi = posi + 1 until not iscont(s, posi)
        n = n - 1
      end
    end
  end
  if n == 0 then
    local e = posi
    if posi <= ls then
      e = posi + 1
      while iscont(s, e) do e = e + 1 end
      e = e - 1
    end
    return posi, e
  end
  return nil
end

function utf8.codes(s, lax)
  s = checkstr(s, 1, "codes")
  local n = len(s)
  return function(str, pos)
    if pos > 0 then
      pos = pos + 1
      while iscont(str, pos) do pos = pos + 1 end
    else
      pos = 1
    end
    if pos > n then return nil end
    local c, nx = utf8_decode(str, pos, not lax)
    if not c or iscont(str, nx) then error("invalid UTF-8 code") end
    return pos, c
  end, s, 0
end

-- ---------------------------------------------------------------------- io

local FILE = {}
FILE.__index = FILE
FILE.__name = "FILE*"

local function newfile(h)
  return setmetatable({h = h, closed = false, buf = "", pos = 1, eof = false}, FILE)
end

FILE.__tostring = function(f)
  if f.closed then return "file (closed)" end
  return "file (" .. (tostring(f.h):match("0x%x+") or "0x0") .. ")"
end

local function checkfile(f, fname)
  if getmetatable(f) ~= FILE then argerror(1, fname, "FILE* expected, got " .. typename(f)) end
  if f.closed then error("attempt to use a closed file") end
  return f
end

function FILE.write(f, ...)
  checkfile(f, "write")
  local n = select("#", ...)
  for i = 1, n do
    local v = select(i, ...)
    local ok, err = P.write(f.h, checkstr(v, i, "write"))
    if not ok then return nil, err end
  end
  return f
end

-- Reading: each file keeps a buffer (f.buf from f.pos) filled by P.read.
local CHUNK = 4096

local function fill(f)
  if f.eof then return false end
  local data = P.read(f.h, CHUNK)
  if not data then
    f.eof = true
    return false
  end
  if f.pos > len(f.buf) then f.buf = data else f.buf = sub(f.buf, f.pos) .. data end
  f.pos = 1
  return true
end

local function read_line(f, keep)
  while true do
    local nl = pfind(f.buf, "\n", f.pos)
    if nl then
      local line = sub(f.buf, f.pos, keep and nl or nl - 1)
      f.pos = nl + 1
      return line
    end
    if not fill(f) then
      if f.pos > len(f.buf) then return nil end
      local line = sub(f.buf, f.pos)
      f.pos = len(f.buf) + 1
      return line
    end
  end
end

local function peek(f)
  if f.pos > len(f.buf) and not fill(f) then return nil end
  return byte(f.buf, f.pos)
end

local function read_number(f)
  local c = peek(f)
  while c and isspace(c) do
    f.pos = f.pos + 1
    c = peek(f)
  end
  local digits = {}
  local n = 0
  while c and n < 200 and (isalnum(c) or c == 46 or c == 43 or c == 45) do
    -- a sign only at the start or after an exponent
    if (c == 43 or c == 45) and n > 0 then
      local prev = digits[n]
      if not (prev == "e" or prev == "E" or prev == "p" or prev == "P") then break end
    end
    n = n + 1
    digits[n] = char(c)
    f.pos = f.pos + 1
    c = peek(f)
  end
  return tonumber(concat(digits))
end

local function read_one(f, what)
  if type(what) == "number" then
    if what <= 0 then
      if peek(f) then return "" end
      return nil
    end
    while len(f.buf) - f.pos + 1 < what and fill(f) do end
    if f.pos > len(f.buf) then return nil end
    local s = sub(f.buf, f.pos, f.pos + what - 1)
    f.pos = f.pos + len(s)
    return s
  end
  if type(what) == "string" then what = what:gsub("^%*", "") end
  local c = type(what) == "string" and byte(what, 1)
  if c == 108 then
    return read_line(f, false)
  elseif c == 76 then
    return read_line(f, true)
  elseif c == 97 then
    while fill(f) do end
    local s = sub(f.buf, f.pos)
    f.pos = len(f.buf) + 1
    return s
  elseif c == 110 then
    return read_number(f)
  end
  argerror(1, "read", "invalid format")
end

function FILE.read(f, ...)
  checkfile(f, "read")
  local n = select("#", ...)
  if n == 0 then return read_one(f, "l") end
  local out = {}
  for i = 1, n do
    local v = read_one(f, (select(i, ...)))
    out[i] = v
    if v == nil then return unpack(out, 1, i) end
  end
  return unpack(out, 1, n)
end

function FILE.lines(f, ...)
  checkfile(f, "lines")
  local fmts = {...}
  local nf = select("#", ...)
  return function()
    if nf == 0 then return read_one(f, "l") end
    return f:read(unpack(fmts, 1, nf))
  end
end

function FILE.close(f)
  checkfile(f, "close")
  if f.h == 1 then return nil, "cannot close standard file" end
  P.close(f.h)
  f.closed = true
  return true
end

function FILE.flush(f) return f end
function FILE.setvbuf(f) return true end

function FILE.seek(f, whence, offset)
  checkfile(f, "seek")
  whence = whence or "cur"
  offset = offset or 0
  local w = whence == "set" and 0 or (whence == "cur" and 1 or (whence == "end" and 2))
  if not w then argerror(2, "seek", "invalid option '" .. tostring(whence) .. "'") end
  -- the OS's position is past what is still buffered
  if w == 1 then offset = offset - (len(f.buf) - f.pos + 1) end
  f.buf, f.pos, f.eof = "", 1, false
  return P.seek(f.h, offset, w)
end

io.stdin = newfile(P.std(0))
io.stdout = newfile(1)
io.stderr = newfile(P.std(2))
P.setstdout(io.stdout)
local default_in, default_out = io.stdin, io.stdout

function io.open(path, mode)
  path = checkstr(path, 1, "open")
  mode = mode or "r"
  local m = mode:gsub("b", "")
  if not (m == "r" or m == "w" or m == "a" or m == "r+" or m == "w+" or m == "a+") then
    argerror(2, "open", "invalid mode '" .. mode .. "'")
  end
  local h, err, code = P.open(path, m)
  if not h then return nil, err, code end
  return newfile(h)
end

function io.close(f)
  return (f or default_out):close()
end

function io.read(...) return default_in:read(...) end

function io.lines(path, ...)
  if path == nil then return default_in:lines(...) end
  local f, err = io.open(path, "r")
  if not f then error(err) end
  local fmts = {...}
  local nf = select("#", ...)
  return function()
    local v
    if nf == 0 then v = read_one(f, "l") else v = f:read(unpack(fmts, 1, nf)) end
    if v == nil then f:close() end
    return v
  end
end

function io.input(f)
  if f == nil then return default_in end
  if type(f) == "string" then
    local nf, err = io.open(f, "r")
    if not nf then error(err) end
    f = nf
  end
  default_in = f
  return f
end

function io.output(f)
  if f == nil then return default_out end
  if type(f) == "string" then
    local nf, err = io.open(f, "w")
    if not nf then error(err) end
    f = nf
  end
  default_out = f
  return f
end

function io.type(f)
  if getmetatable(f) ~= FILE then return nil end
  return f.closed and "closed file" or "file"
end

-- ---------------------------------------------------------------------- os

function os.time(t)
  if t == nil then return P.now() end
  if type(t) ~= "table" then argerror(1, "time", "table expected, got " .. typename(t)) end
  local function field(k, d)
    local v = t[k]
    if v == nil then
      if d == nil then error("field '" .. k .. "' missing in date table") end
      return d
    end
    local i = math.tointeger(v)
    if not i then error("field '" .. k .. "' is not an integer") end
    return i
  end
  return P.time(field("year"), field("month"), field("day"), field("hour", 12), field("min", 0), field("sec", 0))
end

function os.difftime(a, b)
  return (a + 0.0) - (b or 0)
end

local wdays = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"}
local months = {"January", "February", "March", "April", "May", "June", "July", "August",
                "September", "October", "November", "December"}

function os.date(format, t)
  format = format or "%c"
  t = t or P.now()
  local utc = false
  if sub(format, 1, 1) == "!" then
    utc = true
    format = sub(format, 2)
  end
  local d = P.date(t, utc)
  if sub(format, 1, 2) == "*t" then return d end
  local function two(n) return fmt("{:02d}", n) end
  local conv
  conv = {
    a = function() return sub(wdays[d.wday], 1, 3) end,
    A = function() return wdays[d.wday] end,
    b = function() return sub(months[d.month], 1, 3) end,
    B = function() return months[d.month] end,
    c = function() return conv.a() .. " " .. conv.b() .. " " .. fmt("{:2d}", d.day) .. " " .. conv.X() .. " " .. d.year end,
    d = function() return two(d.day) end,
    e = function() return fmt("{:2d}", d.day) end,
    D = function() return conv.m() .. "/" .. conv.d() .. "/" .. conv.y() end,
    F = function() return d.year .. "-" .. conv.m() .. "-" .. conv.d() end,
    H = function() return two(d.hour) end,
    I = function() return two((d.hour + 11) % 12 + 1) end,
    j = function() return fmt("{:03d}", d.yday) end,
    m = function() return two(d.month) end,
    M = function() return two(d.min) end,
    n = function() return "\n" end,
    p = function() return d.hour < 12 and "AM" or "PM" end,
    S = function() return two(d.sec) end,
    t = function() return "\t" end,
    T = function() return conv.X() end,
    x = function() return conv.m() .. "/" .. conv.d() .. "/" .. conv.y() end,
    X = function() return two(d.hour) .. ":" .. two(d.min) .. ":" .. two(d.sec) end,
    y = function() return two(d.year % 100) end,
    Y = function() return tostring(d.year) end,
    w = function() return tostring(d.wday - 1) end,
    ["%"] = function() return "%" end,
  }
  return (format:gsub("%%(.)", function(c)
    local f = conv[c]
    if not f then error("bad argument #1 to 'date' (invalid conversion specifier '%" .. c .. "')") end
    return f()
  end))
end

-- ------------------------------------------------------------------- debug

-- A little of the debug library, for the common uses: debug.traceback as an
-- xpcall handler, and debug.getinfo for a function's kind.
local debug = {}
_ENV.debug = debug

function debug.traceback(msg, level)
  if msg ~= nil and type(msg) ~= "string" and type(msg) ~= "number" then return msg end
  local s = msg and (tostring(msg) .. "\n") or ""
  return s .. "stack traceback:\n\t[C]: in ?"
end

function debug.getinfo(f)
  local t = {source = "=?", short_src = "?", currentline = -1, linedefined = -1, what = "Lua", nups = 0}
  if type(f) == "function" then t.func = f end
  return t
end

function debug.sethook() end
function debug.gethook() return nil end
function debug.getmetatable(v) return getmetatable(v) end
function debug.setmetatable(v, mt)
  if type(v) ~= "table" then error("debug.setmetatable is only supported for tables") end
  return setmetatable(v, mt)
end
