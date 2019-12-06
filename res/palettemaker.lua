-- create a C palette
local color = 0
for line in io.open('dawnbringer-16.hex'):lines() do
	local r, g, b = string.match(line, '(%x%x)(%x%x)(%x%x)')
	print(string.format('{ 0x%s, 0x%s, 0x%s, 0xff },\t/* %2d - #%s%s%s */', r, g, b, color, r, g, b))
	color = color + 1
end
