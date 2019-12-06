# PiX - a minimalistic Lua pixel game engine

## Abstract

- **software rendered** pixel screen
- limited **16 color** palette
- built-in **8x8 pixel font**
- newest **Lua 5.4.0**
- **LZ4 compression** capabilities

## API

### pix.quit()
```lua
pix.quit() -- causes PiX to quit
```

### pix.emit(name [, ...])
Find a function named *name* in the module *pix* and executes it with the given additional arguments.
Returns *true* when the event handler was executed successfully.
```lua
pix.emit('on_mousedown', 0) -- fake a mouse down event
pix.emit('on_myevent', true, 1) -- call pix.on_myevent(true, 1)
```


### pix.screen([width, height [, title]])
Create a new screen with the given dimensions *width* and *height*. If *title* is given it will set the window title, too.

This function will return the current *width* and *height* of the screen.
```lua
local w, h = pix.screen() -- get current width and height
pix.screen(256, 240, 'My Game') -- create a new screen 256x240
```

### pix.palette([index [, color]])
Remap the color at *index* to the color index *color* for all subsequent draw calls.


### pix.color(index [, r, g, b])
Set the color for the given *index*. The RGB values must be 0-255.
```lua
local r, g, b = pix.color(0) -- get the rgb values for color index 0
pix.color(0, 255, 255, 255) -- set color index 0 to #ffffff
```


### pix.fullscreen([fullscreen])
Toggle fullscreen mode for the screen.
```lua
local is_fullscreen = pix.fullscreen() -- get current fullscreen state
pix.fullscreen(true) -- go to fullscreen
```


### pix.mousecursor([visible])
Make the systems mouse cursor visible on the screen window.
```lua
local visible = pix.mousecursor() -- is the mouse cursor visible
pix.mousecursor(false) -- hide the system mouse cursor
```


### pix.clear([color])
Clear the whole screen with the given *color*. It will be 0 on default.
```lua
pix.clear() -- clear the whole screen
pix.clear(15) -- clear the screen with color 15
```


### pix.pixel(color, x, y)
Draw a single pixel on the screen.
```lua
pix.pixel(15, 100, 100) -- draw at 100,100 color 15
```


### pix.line(color, x0, y0, x1, y1)
Draw a line from *x0*, *y0* to *x1*, *y1* with color *color*.


### pix.rect(color, x0, y0, x1, y1 [, fill])
Draw a rectangle from *x0*, *y0* to *x1*, *y1* with color *color*.


### pix.circle(color, x0, y0, radius [, fill])
Draw a circle around *x0*, *y0* with *radius* and color *color*.


### pix.print(color, x, y, text)
Draw the given text at *x*, *y* with color *color* using the builtin 8x8 pixel font.


### pix.draw(x, y, width, height, pixels [, transparent_color])
Draw the given string of *pixels* at *x*, *y* with the *width*, *height*.
The string must contain a hex character (0-9,a-f,A-F) per pixel.


### pix.xxhash(binary)
Calculate the xxhash of the given *binary* string.


### pix.compress(binary [, compression_level])
Perform a LZ4 compression on the given *binary* string.


### pix.decompress(binary)
Decompress the given LZ4 *binary* string and return a decompressed string.
