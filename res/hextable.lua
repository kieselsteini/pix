-- create a hex-decoder table
local hex = {}
for i = 1, 256 do
	hex[i] = ' 0'
end
for ch in string.gmatch('0123456789abcdefABCDEF', '.') do
	hex[string.byte(ch) + 1] = string.format('%2d', tonumber(ch, 16))
end
for i = 1, 256, 16 do
	print(table.concat(hex, ', ', i, i + 15) .. ',')
end
