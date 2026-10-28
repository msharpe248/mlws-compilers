-- string.upper and lower change ASCII letters only, byte for byte (the C
-- locale), whatever else the string holds
local s = "héllo ÉTÉ straße ΣΑΣ ﬁ ǅ İı 123 _-"
print(s:upper())
print(s:lower())
print(("\0a\255Z\128"):upper() == "\0A\255Z\128", ("\0A\255z\128"):lower() == "\0a\255z\128")
print(#("ß"):upper(), #("ẞ"):lower(), ("ß"):upper() == "ß")
print(string.upper(""), string.lower("ABC xyz"), string.upper(12.5))
