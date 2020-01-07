/*
================================================================================

	PiX - a minimalistic Lua pixel game engine
	written by Sebastian Steinhauer <s.steinhauer@yahoo.de>

	This is free and unencumbered software released into the public domain.

	Anyone is free to copy, modify, publish, use, compile, sell, or
	distribute this software, either in source code form or as a compiled
	binary, for any purpose, commercial or non-commercial, and by any
	means.

	In jurisdictions that recognize copyright laws, the author or authors
	of this software dedicate any and all copyright interest in the
	software to the public domain. We make this dedication for the benefit
	of the public at large and to the detriment of our heirs and
	successors. We intend this dedication to be an overt act of
	relinquishment in perpetuity of all present and future rights to this
	software under copyright law.

	THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
	EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
	MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
	IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR
	OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
	ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
	OTHER DEALINGS IN THE SOFTWARE.

	For more information, please refer to <https://unlicense.org>

================================================================================
*/
/*
================================================================================

		INCLUDES

================================================================================
*/
/*----------------------------------------------------------------------------*/
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"


/*----------------------------------------------------------------------------*/
#include "xxhash.h"
#include "lz4frame.h"


/*----------------------------------------------------------------------------*/
#include "SDL.h"


/*----------------------------------------------------------------------------*/
#include "pix.h"	/* include static data */


/*
================================================================================

		DEFINES

================================================================================
*/
/*----------------------------------------------------------------------------*/
#define PIX_AUTHOR				"Sebastian Steinhauer <s.steinhauer@yahoo.de>"
#define PIX_VERSION				"0.2.0"


/*----------------------------------------------------------------------------*/
#define PIX_WINDOW_WIDTH		256
#define PIX_WINDOW_HEIGHT		256
#define PIX_WINDOW_TITLE		"PiX Window"
#define PIX_WINDOW_PADDING		64

#define PIX_MAX_WINDOW_WIDTH	1024
#define PIX_MAX_WINDOW_HEIGHT	1024


/*----------------------------------------------------------------------------*/
#define PIX_FPS					30
#define PIX_FPS_TICKS			(1000 / PIX_FPS)


/*
================================================================================

		GLOBAL VARIABLES

================================================================================
*/
/*----------------------------------------------------------------------------*/
static int event_loop_running = -1;
static SDL_Point clip_tl, clip_br;
static Uint8 palette_mapping[16];
static int palette_modified = 0;
static int screen_modified = 0;


/*----------------------------------------------------------------------------*/
static SDL_Window *window = NULL;
static SDL_Renderer *renderer = NULL;
static SDL_Texture *texture = NULL;
static SDL_Surface *surface32 = NULL;
static SDL_Surface *surface8 = NULL;


/*
================================================================================

		HELPER FUNCTIONS

================================================================================
*/
/*----------------------------------------------------------------------------*/
#define minimum(a, b)		((a) < (b) ? (a) : (b))
#define maximum(a, b)		((a) > (b) ? (a) : (b))
#define clamp(x, min, max)	maximum(minimum(x, max), min)
#define swap(T, a, b)		do { T __tmp = a; a = b; a = __tmp; } while (0)


/*----------------------------------------------------------------------------*/
static int push_error(lua_State *L, const char *fmt, ...) {
	va_list va;

	va_start(va, fmt);
	lua_pushnil(L);
	lua_pushvfstring(L, fmt, va);
	va_end(va);

	return 2;
}


/*----------------------------------------------------------------------------*/
static int push_callback(lua_State *L, const char *name) {
	if (lua_getfield(L, LUA_REGISTRYINDEX, "pix_callbacks") != LUA_TTABLE) {
		lua_pop(L, 1);
		return 0;
	}
	if (lua_getfield(L, -1, name) != LUA_TFUNCTION) {
		lua_pop(L, 2);
		return 0;
	}
	lua_remove(L, -2);
	return -1;
}


/*----------------------------------------------------------------------------*/
static void destroy_screen() {
	if (surface8 != NULL) {
		SDL_FreeSurface(surface8);
		surface8 = NULL;
	}
	if (surface32 != NULL) {
		SDL_FreeSurface(surface32);
		surface32 = NULL;
	}
	if (texture != NULL) {
		SDL_DestroyTexture(texture);
		texture = NULL;
	}
}


/*----------------------------------------------------------------------------*/
static void init_screen(lua_State *L, int width, int height, const char *title) {
	int i, bpp;
	Uint32 pixel_format, rmask, gmask, bmask, amask;
	SDL_DisplayMode dm;

	destroy_screen();

	if ((pixel_format = SDL_GetWindowPixelFormat(window)) == SDL_PIXELFORMAT_UNKNOWN)
		luaL_error(L, "SDL_GetWindowPixelFormat() failed: %s", SDL_GetError());
	if (SDL_PixelFormatEnumToMasks(pixel_format, &bpp, &rmask, &gmask, &bmask, &amask) == SDL_FALSE)
		luaL_error(L, "SDL_PixelFormatEnumToMasks() failed: %s", SDL_GetError());

	if (SDL_RenderSetLogicalSize(renderer, width, height))
		luaL_error(L, "SDL_RenderSetLogicalSize(%d, %d) failed: %s", SDL_GetError());
	if ((texture = SDL_CreateTexture(renderer, pixel_format, SDL_TEXTUREACCESS_STREAMING, width, height)) == NULL)
		luaL_error(L, "SDL_CreateTexture(%d, %d) failed: %s", SDL_GetError());
	if ((surface32 = SDL_CreateRGBSurface(0, width, height, bpp, rmask, gmask, bmask, amask)) == NULL)
		luaL_error(L, "SDL_CreateRGBSurface(%d, %d, %d) failed: %s", bpp, width, height);
	if ((surface8 = SDL_CreateRGBSurface(0, width, height, 8, 0, 0, 0, 0)) == NULL)
		luaL_error(L, "SDL_CreateRGBSurface(%d, %d, %d) failed: %s", 8, width, height);

	clip_tl.x = 0;
	clip_tl.y = 0;
	clip_br.x = width - 1;
	clip_br.y = height - 1;
	screen_modified = palette_modified = -1;
	for (i = 0; i < 16; ++i) palette_mapping[i] = i;

	if (SDL_GetDesktopDisplayMode(0, &dm) == 0) {
		dm.w -= PIX_WINDOW_WIDTH;
		dm.h -= PIX_WINDOW_HEIGHT;
		if (width > dm.w || height > dm.h) {
			while (width > dm.w || height > dm.h) {
				width /= 2;
				height /= 2;
			}
		} else {
			int fx = dm.w / width;
			int fy = dm.h / height;
			width *= fx < fy ? fx : fy;
			height *= fx < fy ? fx : fy;
		}
	}

	SDL_SetWindowSize(window, width, height);
	SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
	SDL_SetWindowTitle(window, title);
}


/*----------------------------------------------------------------------------*/
static void render_screen(lua_State *L) {
	if (SDL_RenderClear(renderer))
		luaL_error(L, "SDL_RenderClear() failed: %s", SDL_GetError());

	if (texture != NULL && surface32 != NULL && surface8 != NULL) {
		if (screen_modified) {
			screen_modified = 0;
			if (palette_modified) {
				palette_modified = 0;
				if (SDL_SetPaletteColors(surface8->format->palette, palette, 0, 16))
					luaL_error(L, "SDL_SetPaletteColors() failed: %s", SDL_GetError());
			}
			if (SDL_BlitSurface(surface8, NULL, surface32, NULL))
				luaL_error(L, "SDL_BlitSurface() failed: %s", SDL_GetError());
			if (SDL_UpdateTexture(texture, NULL, surface32->pixels, surface32->pitch))
				luaL_error(L, "SDL_UpdateTexture() failed: %s", SDL_GetError());
		}
		if (SDL_RenderCopy(renderer, texture, NULL, NULL))
			luaL_error(L, "SDL_RenderCopy() failed: %s", SDL_GetError());
	}

	SDL_RenderPresent(renderer);
}


/*----------------------------------------------------------------------------*/
static void draw_pixel(int x, int y, Uint8 color) {
	if ((surface8 != NULL) && (x >= clip_tl.x) && (x <= clip_br.x) && (y >= clip_tl.y) && (y <= clip_br.y)) {
		((Uint8*)surface8->pixels)[surface8->pitch * y + x] = palette_mapping[color % 16];
		screen_modified = -1;
	}
}


/*
================================================================================

		LUA API

================================================================================
*/
/*
--------------------------------------------------------------------------------

		Misc Functions

--------------------------------------------------------------------------------
*/
/*----------------------------------------------------------------------------*/
static int f_quit(lua_State *L) {
	(void)L;
	event_loop_running = 0;
	return 0;
}


/*----------------------------------------------------------------------------*/
static int f_emit(lua_State *L) {
	const char *name = luaL_checkstring(L, 1);
	if (push_callback(L, name)) {
		lua_replace(L, 1);
		lua_call(L, lua_gettop(L) - 1, 0);
		lua_pushboolean(L, 1);
		return 1;
	} else {
		return push_error(L, "undefined event handler: '%s'", name);
	}
}


/*----------------------------------------------------------------------------*/
static int f_screen(lua_State *L) {
	if (lua_gettop(L) > 0) {
		int w = (int)luaL_checkinteger(L, 1);
		int h = (int)luaL_checkinteger(L, 2);
		const char *title = luaL_optstring(L, 3, PIX_WINDOW_TITLE);

		luaL_argcheck(L, w > 0 && w <= PIX_MAX_WINDOW_WIDTH, 1, "invalid screen width");
		luaL_argcheck(L, h > 0 && h <= PIX_MAX_WINDOW_HEIGHT, 2, "invalid screen height");

		init_screen(L, w, h, title);
	}

	if (surface8 == NULL)
		return push_error(L, "screen not initialized");

	lua_pushinteger(L, surface8->w);
	lua_pushinteger(L, surface8->h);

	return 2;
}


/*----------------------------------------------------------------------------*/
static int f_color(lua_State *L) {
	SDL_Color *color;
	int idx = (int)luaL_checkinteger(L, 1);

	luaL_argcheck(L, idx >= 0 && idx < 16, 1, "invalid color index");
	color = &palette[idx];

	if (lua_gettop(L) > 1) {
		int r = (int)luaL_checknumber(L, 2);
		int g = (int)luaL_checknumber(L, 3);
		int b = (int)luaL_checknumber(L, 4);
		color->r = (Uint8)clamp(r, 0, 255);
		color->g = (Uint8)clamp(g, 0, 255);
		color->b = (Uint8)clamp(b, 0, 255);
	}

	lua_pushinteger(L, color->r);
	lua_pushinteger(L, color->g);
	lua_pushinteger(L, color->b);

	return 3;
}


/*----------------------------------------------------------------------------*/
static int f_palette(lua_State *L) {
	int i, idx, color;
	switch (lua_gettop(L)) {
		case 0: /* no params reset palette */
			for (i = 0; i < 16; ++i) palette_mapping[i] = i;
			return 0;
		case 1: /* return the mapping for color */
			idx = (int)luaL_checkinteger(L, 1);
			luaL_argcheck(L, idx >= 0 && idx < 16, 1, "invalid color index");
			lua_pushinteger(L, palette_mapping[idx]);
			return 1;
		case 2: /* remap a color */
			idx = (int)luaL_checkinteger(L, 1);
			color = (int)luaL_checkinteger(L, 2);
			luaL_argcheck(L, idx >= 0 && idx < 16, 1, "invalid color index");
			luaL_argcheck(L, idx >= 0 && idx < 16, 2, "invalid color index");
			palette_mapping[idx] = color;
			lua_pushinteger(L, color);
			return 1;
		default: /* wrong number of arguments */
			return luaL_error(L, "wrong number of arguments");
	}
}


/*----------------------------------------------------------------------------*/
static int f_fullscreen(lua_State *L) {
	Uint32 flags;

	if (lua_gettop(L) > 0) {
		int fullscreen = lua_toboolean(L, 1);
		SDL_SetWindowFullscreen(window, fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
	}

	flags = SDL_GetWindowFlags(window);
	lua_pushboolean(L, flags & (SDL_WINDOW_FULLSCREEN | SDL_WINDOW_FULLSCREEN_DESKTOP));
	return 1;
}


/*----------------------------------------------------------------------------*/
static int f_mousecursor(lua_State *L) {
	if (lua_gettop(L) > 0) {
		int show = lua_toboolean(L, 1);
		SDL_ShowCursor(show ? SDL_ENABLE : SDL_DISABLE);
	}

	lua_pushboolean(L, SDL_ShowCursor(SDL_QUERY) == SDL_ENABLE);
	return 1;
}


/*
--------------------------------------------------------------------------------

		Drawing Functions

--------------------------------------------------------------------------------
*/
/*----------------------------------------------------------------------------*/
static int f_clear(lua_State *L) {
	int x, y;
	Uint8 color = (Uint8)luaL_optinteger(L, 1, 0);

	for (y = clip_tl.y; y <= clip_br.y; ++y) {
		for (x = clip_tl.x; x <= clip_br.x; ++x) {
			draw_pixel(x, y, color);
		}
	}

	return 0;
}


/*----------------------------------------------------------------------------*/
static int f_pixel(lua_State *L) {
	Uint8 color = (Uint8)luaL_checkinteger(L, 1);
	int x0 = (int)luaL_checknumber(L, 2);
	int y0 = (int)luaL_checknumber(L, 3);

	draw_pixel(x0, y0, color);

	return 0;
}


/*----------------------------------------------------------------------------*/
static int f_line(lua_State *L) {
	Uint8 color = (Uint8)luaL_checkinteger(L, 1);
	int x0 = (int)luaL_checknumber(L, 2);
	int y0 = (int)luaL_checknumber(L, 3);
	int x1 = (int)luaL_checknumber(L, 4);
	int y1 = (int)luaL_checknumber(L, 5);

	int dx =  abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
	int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
	int e2, err = dx + dy;

	for (;;) {
		draw_pixel(x0, y0, color);
		if (x0 == x1 && y0 == y1) break;
		e2 = err * 2;
		if (e2 > dy) { err += dy; x0 += sx; }
		if (e2 < dx) { err += dx; y0 += sy; }
	}

	return 0;
}


/*----------------------------------------------------------------------------*/
static int f_rect(lua_State *L) {
	int x, y;
	Uint8 color = (Uint8)luaL_checkinteger(L, 1);
	int x0 = (int)luaL_checknumber(L, 2);
	int y0 = (int)luaL_checknumber(L, 3);
	int x1 = (int)luaL_checknumber(L, 4);
	int y1 = (int)luaL_checknumber(L, 5);
	int fill = lua_toboolean(L, 6);

	if (x0 > x1) swap(int, x0, x1);
	if (y0 > y1) swap(int, y0, y1);

	if (fill) {
		for (y = y0; y <= y1; ++y) {
			for (x = x0; x <= x1; ++x) draw_pixel(x, y, color);
		}
	} else {
		for (y = y0; y <= y1; ++y) {
			draw_pixel(x0, y, color);
			draw_pixel(x1, y, color);
		}
		for (x = x0; x <= x1; ++x) {
			draw_pixel(x, y0, color);
			draw_pixel(x, y1, color);
		}
	}

	return 0;
}


/*----------------------------------------------------------------------------*/
static int f_circle(lua_State *L) {
	int x, y, dx, dy, dist;
	Uint8 color = (Uint8)luaL_checkinteger(L, 1);
	int x0 = (int)luaL_checknumber(L, 2);
	int y0 = (int)luaL_checknumber(L, 3);
	int radius = (int)luaL_checknumber(L, 4);
	int r0sq = lua_toboolean(L, 5) ? 0 : ((radius - 1) * (radius - 1));
	int r1sq = radius * radius;

	for (y = -radius; y <= radius; ++y) {
		dy = y * y;
		for (x = -radius; x <= radius; ++x) {
			dx = x * x;
			dist = dx + dy;
			if (dist >= r0sq && dist <= r1sq) draw_pixel(x0 + x, y0 + y, color);
		}
	}

	return 0;
}


/*----------------------------------------------------------------------------*/
static int f_print(lua_State *L) {
	int x, y, bits;
	size_t length;
	Uint8 color = (Uint8)luaL_checkinteger(L, 1);
	int x0 = (int)luaL_checknumber(L, 2);
	int y0 = (int)luaL_checknumber(L, 3);
	const Uint8 *str = (const Uint8*)luaL_checklstring(L, 4, &length);

	for (; length; --length, x0 += 8, ++str) {
		for (y = 0; y < 8; ++y) {
			bits = font8x8[*str][y];
			for (x = 0; x < 8; ++x) {
				if (bits & (1 << x)) draw_pixel(x0 + x, y0 + y, color);
			}
		}
	}
	return 0;
}


/*----------------------------------------------------------------------------*/
static int f_draw(lua_State *L) {
	size_t length;
	int x, y;
	Uint8 color;
	int x0 = (int)luaL_checknumber(L, 1);
	int y0 = (int)luaL_checknumber(L, 2);
	int w = (int)luaL_checkinteger(L, 3);
	int h = (int)luaL_checkinteger(L, 4);
	const Uint8 *pixels = (const Uint8*)luaL_checklstring(L, 5, &length);
	Uint8 alpha = (Uint8)luaL_optinteger(L, 6, 256);

	luaL_argcheck(L, (int)length == w * h, 5, "invalid length of pixel string");
	for (y = 0; y < h; ++y) {
		for (x = 0; x < w; ++x) {
			color = hexdecoder_table[*pixels++];
			if (color != alpha) draw_pixel(x0 + x, y0 + y, color);
		}
	}

	return 0;
}


/*
--------------------------------------------------------------------------------

		Compression Functions

--------------------------------------------------------------------------------
*/
/*----------------------------------------------------------------------------*/
static int f_xxhash(lua_State *L) {
	size_t length;
	const char *data = luaL_checklstring(L, 1, &length);
	unsigned int seed = (unsigned int)luaL_optinteger(L, 2, 0);

	lua_pushinteger(L, XXH32(data, length, seed));

	return 1;
}


/*----------------------------------------------------------------------------*/
static int f_compress(lua_State *L) {
	LZ4F_preferences_t prefs;
	size_t src_size, dst_size;
	luaL_Buffer buffer;
	char *dst_data;
	const char *src_data = luaL_checklstring(L, 1, &src_size);
	int compression_level = (int)luaL_optinteger(L, 2, 9);

	SDL_zero(prefs);
	prefs.compressionLevel = compression_level;

	dst_size = LZ4F_compressFrameBound(src_size, &prefs);
	if (LZ4F_isError(dst_size))
		return push_error(L, "LZ4F_compressFrameBound() failed: %s", LZ4F_getErrorName(dst_size));

	dst_data = luaL_buffinitsize(L, &buffer, dst_size);
	dst_size = LZ4F_compressFrame(dst_data, dst_size, src_data, src_size, &prefs);

	if (LZ4F_isError(dst_size))
		return push_error(L, "LZ4F_compressFrame() failed: %s", LZ4F_getErrorName(dst_size));

	luaL_pushresultsize(&buffer, dst_size);
	return 1;
}


/*----------------------------------------------------------------------------*/
static int f_decompress(lua_State *L) {
	size_t src_size, dst_size, lz_avail, lz_hint;
	char data[1024 * 64];
	luaL_Buffer buffer;
	LZ4F_dctx *dctx;
	LZ4F_errorCode_t lz_err;
	LZ4F_decompressOptions_t options;
	const char *src_data = luaL_checklstring(L, 1, &src_size);

	lz_err = LZ4F_createDecompressionContext(&dctx, LZ4F_VERSION);
	if (LZ4F_isError(lz_err))
		return push_error(L, "LZ4F_createDecompressionContext() failed: %s", LZ4F_getErrorName(lz_err));

	SDL_zero(options);
	luaL_buffinit(L, &buffer);

	for (;;) {
		lz_avail = src_size;
		dst_size = sizeof(data);
		lz_hint = LZ4F_decompress(dctx, data, &dst_size, src_data, &lz_avail, &options);

		if (LZ4F_isError(lz_hint)) {
			LZ4F_freeDecompressionContext(dctx);
			return push_error(L, "LZ4F_decompress() failed: %s", LZ4F_getErrorName(lz_hint));
		}
		if (dst_size == 0) {
			LZ4F_freeDecompressionContext(dctx);
			return push_error(L, "LZ4F_decompress() returned no output");
		}

		luaL_addlstring(&buffer, data, dst_size);
		src_data += lz_avail;
		src_size -= lz_avail;

		if (lz_hint == 0) break;
	}

	LZ4F_freeDecompressionContext(dctx);
	luaL_pushresult(&buffer);
	return 1;
}


/*----------------------------------------------------------------------------*/
static const luaL_Reg funcs[] = {
	/* Misc Functions */
	{ "quit", f_quit },
	{ "emit", f_emit },
	{ "screen", f_screen },
	{ "palette", f_palette },
	{ "color", f_color },
	{ "fullscreen", f_fullscreen },
	{ "mousecursor", f_mousecursor },

	/* Drawing Functions */
	{ "clear", f_clear },
	{ "pixel", f_pixel },
	{ "line", f_line },
	{ "rect", f_rect },
	{ "circle", f_circle },
	{ "print", f_print },
	{ "draw", f_draw },

	/* Compression Functions */
	{ "xxhash", f_xxhash },
	{ "compress", f_compress },
	{ "decompress", f_decompress },

	/* Sentinel */
	{ NULL, NULL }
};


/*----------------------------------------------------------------------------*/
static int open_pix_module(lua_State *L) {
	luaL_newlib(L, funcs);

	lua_pushstring(L, PIX_AUTHOR);
	lua_setfield(L, -2, "__AUTHOR");

	lua_pushstring(L, PIX_VERSION);
	lua_setfield(L, -2, "__VERSION");

	return 1;
}


/*
================================================================================

		EVENT LOOP

================================================================================
*/
/*----------------------------------------------------------------------------*/
static void handle_SDL_events(lua_State *L) {
	SDL_Event ev;

	while (SDL_PollEvent(&ev)) {
		switch (ev.type) {
			case SDL_QUIT:
				event_loop_running = 0;
				break;

			case SDL_KEYDOWN:
				if (push_callback(L, "on_keydown")) {
					lua_pushstring(L, SDL_GetKeyName(ev.key.keysym.sym));
					lua_call(L, 1, 0);
				}
				break;

			case SDL_KEYUP:
				if (push_callback(L, "on_keyup")) {
					lua_pushstring(L, SDL_GetKeyName(ev.key.keysym.sym));
					lua_call(L, 1, 0);
				}
				break;

			case SDL_TEXTINPUT:
				if (push_callback(L, "on_textinput")) {
					lua_pushstring(L, ev.text.text);
					lua_call(L, 1, 0);
				}
				break;

			case SDL_MOUSEBUTTONDOWN:
				if (push_callback(L, "on_mousedown")) {
					lua_pushinteger(L, ev.button.button);
					lua_call(L, 1, 0);
				}
				break;

			case SDL_MOUSEBUTTONUP:
				if (push_callback(L, "on_mouseup")) {
					lua_pushinteger(L, ev.button.button);
					lua_call(L, 1, 0);
				}
				break;

			case SDL_MOUSEMOTION:
				if (push_callback(L, "on_mousemoved")) {
					lua_pushinteger(L, ev.motion.x);
					lua_pushinteger(L, ev.motion.y);
					lua_call(L, 2, 0);
				}
				break;

			case SDL_CONTROLLERDEVICEADDED:
				if (SDL_GameControllerOpen(ev.cdevice.which) && push_callback(L, "on_controlleradded")) {
					lua_pushinteger(L, ev.cdevice.which);
					lua_call(L, 1, 0);
				}
				break;

			case SDL_CONTROLLERDEVICEREMOVED:
				if (push_callback(L, "on_controllerremoved")) {
					lua_pushinteger(L, ev.cdevice.which);
					lua_call(L, 1, 0);
				}
				break;

			case SDL_CONTROLLERBUTTONDOWN:
				if (push_callback(L, "on_controllerdown")) {
					lua_pushinteger(L, ev.cbutton.which);
					lua_pushstring(L, SDL_GameControllerGetStringForButton(ev.cbutton.button));
					lua_call(L, 2, 0);
				}
				break;

			case SDL_CONTROLLERBUTTONUP:
				if (push_callback(L, "on_controllerup")) {
					lua_pushinteger(L, ev.cbutton.which);
					lua_pushstring(L, SDL_GameControllerGetStringForButton(ev.cbutton.button));
					lua_call(L, 2, 0);
				}
				break;

			case SDL_CONTROLLERAXISMOTION:
				if (push_callback(L, "on_controllermoved")) {
					lua_pushinteger(L, ev.caxis.which);
					lua_pushstring(L, SDL_GameControllerGetStringForAxis(ev.caxis.axis));
					lua_pushnumber(L, ev.caxis.value >= 0 ? (lua_Number)ev.caxis.value / 32767.0 : (lua_Number)ev.caxis.value / 32768.0);
					lua_call(L, 3, 0);
				}
				break;
		}
	}
}


/*----------------------------------------------------------------------------*/
static void run_event_loop(lua_State *L) {
	Uint32 last_tick, current_tick, delta_ticks, frame_no;

	if (push_callback(L, "on_init"))
		lua_call(L, 0, 0);

	delta_ticks = 0;
	frame_no = 0;
	last_tick = SDL_GetTicks();

	while (event_loop_running) {
		handle_SDL_events(L);

		current_tick = SDL_GetTicks();
		delta_ticks += current_tick - last_tick;
		last_tick = current_tick;

		for (; delta_ticks >= PIX_FPS_TICKS; delta_ticks -= PIX_FPS_TICKS) {
			++frame_no;
			if (push_callback(L, "on_update")) {
				lua_pushinteger(L, frame_no);
				lua_call(L, 1, 0);
			}
		}

		render_screen(L);
	}

	if (push_callback(L, "on_quit"))
		lua_call(L, 0, 0);
}


/*
================================================================================

		INIT / SHUTDOWN

================================================================================
*/
/*----------------------------------------------------------------------------*/
static int init_pix(lua_State *L) {
	if (SDL_Init(SDL_INIT_EVERYTHING))
		luaL_error(L, "SDL_Init() failed: %s", SDL_GetError());

	if ((window = SDL_CreateWindow(PIX_WINDOW_TITLE, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, PIX_WINDOW_WIDTH, PIX_WINDOW_HEIGHT, SDL_WINDOW_RESIZABLE)) == NULL)
		luaL_error(L, "SDL_CreateWindow() failed: %s", SDL_GetError());
	if ((renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_PRESENTVSYNC | SDL_RENDERER_ACCELERATED)) == NULL)
		luaL_error(L,"SDL_CreateRenderer() failed: %s", SDL_GetError());

	if (luaL_loadfile(L, "demo.lua") != LUA_OK)
		lua_error(L);
	lua_call(L, 0, 0);

	run_event_loop(L);

	return 0;
}


/*----------------------------------------------------------------------------*/
static void shutdown_pix() {
	destroy_screen();
	SDL_Quit();
}


/*----------------------------------------------------------------------------*/
int main() {
	lua_State *L = luaL_newstate();

	luaL_openlibs(L);
	luaL_requiref(L, "pix", open_pix_module, 1);
	lua_setfield(L, LUA_REGISTRYINDEX, "pix_callbacks");

	lua_getglobal(L, "debug");
	lua_getfield(L, -1, "traceback");
	lua_remove(L, -2);

	lua_pushcfunction(L, init_pix);
	if (lua_pcall(L, 0, 0, -2) != LUA_OK) {
		const char *msg = luaL_gsub(L, lua_tostring(L, -1), "\t", "    ");
		SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "PiX Panic", msg, window);
	}

	lua_close(L);
	shutdown_pix();
	return 0;
}
