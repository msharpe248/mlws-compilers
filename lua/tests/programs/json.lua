-- a small JSON encoder and decoder
local json = {}
local escapes = {['"'] = '\\"', ['\\'] = '\\\\', ['\b'] = '\\b', ['\f'] = '\\f', ['\n'] = '\\n', ['\r'] = '\\r', ['\t'] = '\\t'}
local function encode(v, out)
  local t = type(v)
  if t == "nil" then out[#out + 1] = "null"
  elseif t == "boolean" then out[#out + 1] = tostring(v)
  elseif t == "number" then
    if math.type(v) == "integer" then out[#out + 1] = tostring(v) else out[#out + 1] = string.format("%.14g", v) end
  elseif t == "string" then
    out[#out + 1] = '"' .. v:gsub('[%c"\\]', function(c) return escapes[c] or string.format("\\u%04x", c:byte()) end) .. '"'
  elseif t == "table" then
    if #v > 0 or next(v) == nil then
      out[#out + 1] = "["
      for i, x in ipairs(v) do if i > 1 then out[#out + 1] = "," end encode(x, out) end
      out[#out + 1] = "]"
    else
      local keys = {}
      for k in pairs(v) do keys[#keys + 1] = tostring(k) end
      table.sort(keys)
      out[#out + 1] = "{"
      for i, k in ipairs(keys) do
        if i > 1 then out[#out + 1] = "," end
        encode(k, out) out[#out + 1] = ":" encode(v[k], out)
      end
      out[#out + 1] = "}"
    end
  else error("cannot encode " .. t) end
end
function json.encode(v) local out = {} encode(v, out) return table.concat(out) end
local function skip(s, i) return s:find("%S", i) or #s + 1 end
local decode
local function decode_string(s, i)
  local out = {}
  i = i + 1
  while true do
    local c = s:sub(i, i)
    if c == '"' then return table.concat(out), i + 1 end
    if c == "\\" then
      local n = s:sub(i + 1, i + 1)
      local map = {b = "\b", f = "\f", n = "\n", r = "\r", t = "\t"}
      if n == "u" then out[#out + 1] = utf8.char(tonumber(s:sub(i + 2, i + 5), 16)) i = i + 6
      else out[#out + 1] = map[n] or n i = i + 2 end
    else out[#out + 1] = c i = i + 1 end
  end
end
decode = function(s, i)
  i = skip(s, i)
  local c = s:sub(i, i)
  if c == "{" then
    local t = {}
    i = skip(s, i + 1)
    if s:sub(i, i) == "}" then return t, i + 1 end
    while true do
      local k
      k, i = decode_string(s, skip(s, i))
      i = skip(s, i) + 1
      t[k], i = decode(s, i)
      i = skip(s, i)
      local d = s:sub(i, i)
      i = i + 1
      if d == "}" then return t, i end
    end
  elseif c == "[" then
    local t = {}
    i = skip(s, i + 1)
    if s:sub(i, i) == "]" then return t, i + 1 end
    while true do
      t[#t + 1], i = decode(s, i)
      i = skip(s, i)
      local d = s:sub(i, i)
      i = i + 1
      if d == "]" then return t, i end
    end
  elseif c == '"' then return decode_string(s, i)
  elseif s:sub(i, i + 3) == "true" then return true, i + 4
  elseif s:sub(i, i + 4) == "false" then return false, i + 5
  elseif s:sub(i, i + 3) == "null" then return nil, i + 4
  else
    local num = s:match("^-?%d+%.?%d*[eE]?[-+]?%d*", i)
    return tonumber(num), i + #num
  end
end
function json.decode(s) return (decode(s, 1)) end
local doc = {name = "mlws", version = 5.5, tags = {"vm", "lua", "compiler"}, nested = {a = {1, 2, {b = false}}, e = "tab\there"}, count = 42}
local text = json.encode(doc)
print(text)
local back = json.decode(text)
print(back.name, back.version, back.tags[3], back.nested.a[3].b, back.nested.e, back.count, math.type(back.count))
print(json.encode(json.decode('[1, 2.5, "x\\u0041y", {"k": [true, null, false]}, []]')))
