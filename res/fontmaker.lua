-- convert font.pnm to bitfields

-- load the PNM file
local f = assert(io.open('font.pnm', 'rb'))
assert(f:read('l') == 'P6')
assert(f:read('l') == '128 128')
assert(f:read('l') == '255')
local pixels = f:read(128 * 128 * 3)
assert(#pixels == 128 * 128 * 3)
f:close()
f = nil

-- create pixel bits
for i = 0, 255 do
	local px, py = i % 16, i // 16
	bit_patterns = {}
	for y = 0, 7 do
		local bits = 0
		for x = 0, 7 do
			local ptr = (((px * 8) + x) + (((py * 8) + y) * 128)) * 3 + 1
			local r, g, b = string.byte(pixels, ptr, ptr + 2)
			if r == 255 and g == 255 and b == 255 then
				bits = bits | (1 << x)
			end
		end
		bit_patterns[#bit_patterns + 1] = string.format('0x%02x', bits)
	end
	print('{ ' .. table.concat(bit_patterns, ', ') .. string.format(' },\t/* %3d */', i))
end
