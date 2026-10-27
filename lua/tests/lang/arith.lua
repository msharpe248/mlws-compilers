-- integers, floats and their operators
print(1 + 2, 7 - 10, 6 * 7, 7 / 2, 7 // 2, 7 % 3, 2 ^ 10)
print(-7 // 2, -7 % 3, 7 % -3, -7.5 % 2, 7.5 // 2, -0.0, 1e300 * 1e10)
print(3 | 5, 3 & 5, 3 ~ 5, ~0, 1 << 62, 1 << 63, 1 << 64, -1 >> 1, 256 >> 4, 2 << -1)
print(math.maxinteger + 1 == math.mininteger, math.mininteger // -1, math.mininteger % -1)
print(10 / 0, -10 / 0, 0/0 ~= 0/0, 5 // 0.0, -5 // 0.0, 5 % math.huge, -5 % math.huge)
print("10" + 1, "3.5" * 2, "0x10" + 0, 10 .. 20, 1.5 .. "x", 2^53, 2^63, -2^63)
print(1 == 1.0, 1 < 1.5, 2 > 1.5, "a" < "b", "abc" < "abd", "Z" < "a", 1 <= 1, 3 >= 4)
print(math.type(1), math.type(1.0), math.type("1"), math.type(2^31), 3 // 1, 3.0 // 1)
local a, b = 17, 5
print(a + b, a - b, a * b, a / b, a // b, a % b, a ^ 2, -a, a & b, a | b, a ~ b, a << 2, a >> 1)
local x, y = 17.5, 5
print(x + y, x - y, x * y, x / y, x // y, x % y, -x)
print(0.1 + 0.2, 100 * 1.1, 1/3, 123456789012345.0, 1e15, 1e16, 2^-1074, 12345678901234567)
print(math.huge, -math.huge, math.pi, 255 // 16, 7.0 % 0 ~= 7.0 % 0)
for i = 1, 3 do print(i, i * 1.5, i // 2, i % 2 == 0) end
print(8 // 3 * 3 + 8 % 3, (-8) // 3 * 3 + (-8) % 3)
print(string.format("%5.2f|%d|%x|%5s|%-5s|%g", 3.14159, 42, 255, "ab", "cd", 1e20))
