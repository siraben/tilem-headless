/*
 * TilEm II - SDL2 UI
 *
 * Copyright (c) 2024
 *
 * This program is free software: you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, either version 3 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <SDL.h>
#include <SDL_ttf.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <glib.h>
#include <glib/gstdio.h>
#include <ticalcs.h>
#include <tilem.h>
#include <scancodes.h>

#include "sdlui.h"
#include "files.h"
#include "skinops.h"
#include "sdlpixbuf.h"
#include "sdlscreenshot.h"
#include "ti81prg.h"

void tilem_config_get(const char *group, const char *option, ...);
void tilem_config_set(const char *group, const char *option, ...);
void tilem_link_send_file(TilemCalcEmulator *emu, const char *filename,
                          int slot, gboolean first, gboolean last);
void tilem_link_receive_all(TilemCalcEmulator *emu,
                            const char *destination);
int name_to_model(const char *name);

#define SDL_LCD_GAMMA 2.2
#define SDL_FRAME_DELAY_MS 16
#define SDL_MENU_FONT_SCALE 2
#define SDL_MENU_PADDING 6
#define SDL_MENU_SPACING 2
#define SDL_MENU_FONT_SIZE 16

typedef struct {
	TilemCalcEmulator *emu;
	SDL_Window *window;
	SDL_Renderer *renderer;
	SDL_Texture *skin_texture;
	SDL_Texture *lcd_texture;
	SKIN_INFOS *skin;
	char *skin_file_name;
	gboolean skin_disabled;
	gboolean skin_locked;
	const char *skin_override;
	gboolean lcd_smooth_scale;
	int mouse_key;
	int lcd_width;
	int lcd_height;
	int window_width;
	int window_height;
	SDL_Rect skin_rect;
	SDL_Rect lcd_rect;
	byte *lcd_pixels;
	int lcd_pixels_width;
	int lcd_pixels_height;
	int lcd_pixels_stride;
	dword *lcd_palette;
	gboolean menu_visible;
	int menu_x;
	int menu_y;
	int menu_width;
	int menu_height;
	int menu_selected;
	gboolean submenu_visible;
	int submenu_x;
	int submenu_y;
	int submenu_width;
	int submenu_height;
	int submenu_selected;
	TTF_Font *menu_font;
	gboolean ttf_ready;
	int keypress_keycodes[64];
	int sequence_keycode;
} TilemSdlUi;

typedef enum {
	SDL_MENU_NONE = 0,
	SDL_MENU_SEND_FILE,
	SDL_MENU_RECEIVE_FILE,
	SDL_MENU_OPEN_CALC,
	SDL_MENU_SAVE_CALC,
	SDL_MENU_REVERT_CALC,
	SDL_MENU_RESET,
	SDL_MENU_MACRO_MENU,
	SDL_MENU_MACRO_BEGIN,
	SDL_MENU_MACRO_END,
	SDL_MENU_MACRO_PLAY,
	SDL_MENU_MACRO_OPEN,
	SDL_MENU_MACRO_SAVE,
	SDL_MENU_SCREENSHOT,
	SDL_MENU_QUICK_SCREENSHOT,
	SDL_MENU_PREFERENCES,
	SDL_MENU_ABOUT,
	SDL_MENU_QUIT
} TilemSdlMenuAction;

typedef struct {
	const char *label;
	TilemSdlMenuAction action;
	gboolean separator;
} TilemSdlMenuItem;

static const TilemSdlMenuItem sdl_menu_items[] = {
	{ "Send File...", SDL_MENU_SEND_FILE, FALSE },
	{ "Receive File...", SDL_MENU_RECEIVE_FILE, FALSE },
	{ NULL, SDL_MENU_NONE, TRUE },
	{ "Open Calculator...", SDL_MENU_OPEN_CALC, FALSE },
	{ "Save Calculator", SDL_MENU_SAVE_CALC, FALSE },
	{ "Revert Calculator State", SDL_MENU_REVERT_CALC, FALSE },
	{ "Reset Calculator", SDL_MENU_RESET, FALSE },
	{ NULL, SDL_MENU_NONE, TRUE },
	{ "Macro >", SDL_MENU_MACRO_MENU, FALSE },
	{ "Screenshot...", SDL_MENU_SCREENSHOT, FALSE },
	{ "Quick Screenshot", SDL_MENU_QUICK_SCREENSHOT, FALSE },
	{ NULL, SDL_MENU_NONE, TRUE },
	{ "Preferences", SDL_MENU_PREFERENCES, FALSE },
	{ NULL, SDL_MENU_NONE, TRUE },
	{ "About", SDL_MENU_ABOUT, FALSE },
	{ "Quit", SDL_MENU_QUIT, FALSE }
};

static const TilemSdlMenuItem sdl_menu_macro_items[] = {
	{ "Begin Recording", SDL_MENU_MACRO_BEGIN, FALSE },
	{ "End Recording", SDL_MENU_MACRO_END, FALSE },
	{ "Play Macro", SDL_MENU_MACRO_PLAY, FALSE },
	{ NULL, SDL_MENU_NONE, TRUE },
	{ "Open Macro...", SDL_MENU_MACRO_OPEN, FALSE },
	{ "Save Macro...", SDL_MENU_MACRO_SAVE, FALSE }
};

#define SDL_SCREENSHOT_DEFAULT_WIDTH_96 192
#define SDL_SCREENSHOT_DEFAULT_HEIGHT_96 128
#define SDL_SCREENSHOT_DEFAULT_WIDTH_128 256
#define SDL_SCREENSHOT_DEFAULT_HEIGHT_128 128

static const unsigned char sdl_font8x8_basic[128][8] = {
	{ 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 }, /* 0x00 */
	{ 0x7e,0x81,0xa5,0x81,0xbd,0x99,0x81,0x7e }, /* 0x01 */
	{ 0x7e,0xff,0xdb,0xff,0xc3,0xe7,0xff,0x7e }, /* 0x02 */
	{ 0x6c,0xfe,0xfe,0xfe,0x7c,0x38,0x10,0x00 }, /* 0x03 */
	{ 0x10,0x38,0x7c,0xfe,0x7c,0x38,0x10,0x00 }, /* 0x04 */
	{ 0x38,0x7c,0x38,0xfe,0xfe,0x92,0x10,0x7c }, /* 0x05 */
	{ 0x00,0x00,0x18,0x3c,0x3c,0x18,0x00,0x00 }, /* 0x06 */
	{ 0xff,0xff,0xe7,0xc3,0xc3,0xe7,0xff,0xff }, /* 0x07 */
	{ 0x00,0x3c,0x66,0x42,0x42,0x66,0x3c,0x00 }, /* 0x08 */
	{ 0xff,0xc3,0x99,0xbd,0xbd,0x99,0xc3,0xff }, /* 0x09 */
	{ 0x0f,0x07,0x0f,0x7d,0xcc,0xcc,0xcc,0x78 }, /* 0x0a */
	{ 0x3c,0x66,0x66,0x66,0x3c,0x18,0x7e,0x18 }, /* 0x0b */
	{ 0x3f,0x33,0x3f,0x30,0x30,0x70,0xf0,0xe0 }, /* 0x0c */
	{ 0x7f,0x63,0x7f,0x63,0x63,0x67,0xe6,0xc0 }, /* 0x0d */
	{ 0x99,0x5a,0x3c,0xe7,0xe7,0x3c,0x5a,0x99 }, /* 0x0e */
	{ 0x80,0xe0,0xf8,0xfe,0xf8,0xe0,0x80,0x00 }, /* 0x0f */
	{ 0x02,0x0e,0x3e,0xfe,0x3e,0x0e,0x02,0x00 }, /* 0x10 */
	{ 0x18,0x3c,0x7e,0x18,0x18,0x7e,0x3c,0x18 }, /* 0x11 */
	{ 0x66,0x66,0x66,0x66,0x66,0x00,0x66,0x00 }, /* 0x12 */
	{ 0x7f,0xdb,0xdb,0x7b,0x1b,0x1b,0x1b,0x00 }, /* 0x13 */
	{ 0x3e,0x61,0x3c,0x66,0x66,0x3c,0x86,0x7c }, /* 0x14 */
	{ 0x00,0x00,0x00,0x00,0x7e,0x7e,0x7e,0x00 }, /* 0x15 */
	{ 0x18,0x3c,0x7e,0x18,0x7e,0x3c,0x18,0xff }, /* 0x16 */
	{ 0x18,0x3c,0x7e,0x18,0x18,0x18,0x18,0x00 }, /* 0x17 */
	{ 0x18,0x18,0x18,0x18,0x7e,0x3c,0x18,0x00 }, /* 0x18 */
	{ 0x00,0x18,0x0c,0xfe,0x0c,0x18,0x00,0x00 }, /* 0x19 */
	{ 0x00,0x30,0x60,0xfe,0x60,0x30,0x00,0x00 }, /* 0x1a */
	{ 0x00,0x00,0xc0,0xc0,0xc0,0xfe,0x00,0x00 }, /* 0x1b */
	{ 0x00,0x24,0x66,0xff,0x66,0x24,0x00,0x00 }, /* 0x1c */
	{ 0x00,0x18,0x3c,0x7e,0xff,0xff,0x00,0x00 }, /* 0x1d */
	{ 0x00,0xff,0xff,0x7e,0x3c,0x18,0x00,0x00 }, /* 0x1e */
	{ 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 }, /* 0x1f */
	{ 0x18,0x3c,0x3c,0x18,0x18,0x00,0x18,0x00 }, /* 0x20 */
	{ 0x6c,0x6c,0x24,0x00,0x00,0x00,0x00,0x00 }, /* 0x21 */
	{ 0x6c,0x6c,0xfe,0x6c,0xfe,0x6c,0x6c,0x00 }, /* 0x22 */
	{ 0x18,0x3e,0x60,0x3c,0x06,0x7c,0x18,0x00 }, /* 0x23 */
	{ 0x00,0xc6,0xcc,0x18,0x30,0x66,0xc6,0x00 }, /* 0x24 */
	{ 0x38,0x6c,0x38,0x76,0xdc,0xcc,0x76,0x00 }, /* 0x25 */
	{ 0x30,0x30,0x60,0x00,0x00,0x00,0x00,0x00 }, /* 0x26 */
	{ 0x0c,0x18,0x30,0x30,0x30,0x18,0x0c,0x00 }, /* 0x27 */
	{ 0x30,0x18,0x0c,0x0c,0x0c,0x18,0x30,0x00 }, /* 0x28 */
	{ 0x00,0x66,0x3c,0xff,0x3c,0x66,0x00,0x00 }, /* 0x29 */
	{ 0x00,0x18,0x18,0x7e,0x18,0x18,0x00,0x00 }, /* 0x2a */
	{ 0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x30 }, /* 0x2b */
	{ 0x00,0x00,0x00,0x7e,0x00,0x00,0x00,0x00 }, /* 0x2c */
	{ 0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x00 }, /* 0x2d */
	{ 0x06,0x0c,0x18,0x30,0x60,0xc0,0x80,0x00 }, /* 0x2e */
	{ 0x7c,0xc6,0xce,0xde,0xf6,0xe6,0x7c,0x00 }, /* 0x2f */
	{ 0x18,0x38,0x18,0x18,0x18,0x18,0x7e,0x00 }, /* 0x30 */
	{ 0x7c,0xc6,0x0e,0x1c,0x70,0xc0,0xfe,0x00 }, /* 0x31 */
	{ 0x7c,0xc6,0x06,0x3c,0x06,0xc6,0x7c,0x00 }, /* 0x32 */
	{ 0x1c,0x3c,0x6c,0xcc,0xfe,0x0c,0x1e,0x00 }, /* 0x33 */
	{ 0xfe,0xc0,0xfc,0x06,0x06,0xc6,0x7c,0x00 }, /* 0x34 */
	{ 0x3c,0x60,0xc0,0xfc,0xc6,0xc6,0x7c,0x00 }, /* 0x35 */
	{ 0xfe,0xc6,0x0c,0x18,0x30,0x30,0x30,0x00 }, /* 0x36 */
	{ 0x7c,0xc6,0xc6,0x7c,0xc6,0xc6,0x7c,0x00 }, /* 0x37 */
	{ 0x7c,0xc6,0xc6,0x7e,0x06,0x0c,0x78,0x00 }, /* 0x38 */
	{ 0x00,0x18,0x18,0x00,0x00,0x18,0x18,0x00 }, /* 0x39 */
	{ 0x00,0x18,0x18,0x00,0x00,0x18,0x18,0x30 }, /* 0x3a */
	{ 0x0e,0x1c,0x38,0x70,0x38,0x1c,0x0e,0x00 }, /* 0x3b */
	{ 0x00,0x00,0x7e,0x00,0x00,0x7e,0x00,0x00 }, /* 0x3c */
	{ 0x70,0x38,0x1c,0x0e,0x1c,0x38,0x70,0x00 }, /* 0x3d */
	{ 0x7c,0xc6,0x0c,0x18,0x18,0x00,0x18,0x00 }, /* 0x3e */
	{ 0x7c,0xc6,0xde,0xde,0xde,0xc0,0x7c,0x00 }, /* 0x3f */
	{ 0x38,0x6c,0xc6,0xc6,0xfe,0xc6,0xc6,0x00 }, /* 0x40 */
	{ 0xfc,0x66,0x66,0x7c,0x66,0x66,0xfc,0x00 }, /* 0x41 */
	{ 0x3c,0x66,0xc0,0xc0,0xc0,0x66,0x3c,0x00 }, /* 0x42 */
	{ 0xf8,0x6c,0x66,0x66,0x66,0x6c,0xf8,0x00 }, /* 0x43 */
	{ 0xfe,0x62,0x68,0x78,0x68,0x62,0xfe,0x00 }, /* 0x44 */
	{ 0xfe,0x62,0x68,0x78,0x68,0x60,0xf0,0x00 }, /* 0x45 */
	{ 0x3c,0x66,0xc0,0xc0,0xce,0x66,0x3e,0x00 }, /* 0x46 */
	{ 0xc6,0xc6,0xc6,0xfe,0xc6,0xc6,0xc6,0x00 }, /* 0x47 */
	{ 0x3c,0x18,0x18,0x18,0x18,0x18,0x3c,0x00 }, /* 0x48 */
	{ 0x1e,0x0c,0x0c,0x0c,0xcc,0xcc,0x78,0x00 }, /* 0x49 */
	{ 0xe6,0x66,0x6c,0x78,0x6c,0x66,0xe6,0x00 }, /* 0x4a */
	{ 0xf0,0x60,0x60,0x60,0x62,0x66,0xfe,0x00 }, /* 0x4b */
	{ 0xc6,0xee,0xfe,0xfe,0xd6,0xc6,0xc6,0x00 }, /* 0x4c */
	{ 0xc6,0xe6,0xf6,0xde,0xce,0xc6,0xc6,0x00 }, /* 0x4d */
	{ 0x7c,0xc6,0xc6,0xc6,0xc6,0xc6,0x7c,0x00 }, /* 0x4e */
	{ 0xfc,0x66,0x66,0x7c,0x60,0x60,0xf0,0x00 }, /* 0x4f */
	{ 0x7c,0xc6,0xc6,0xc6,0xd6,0xcc,0x7a,0x00 }, /* 0x50 */
	{ 0xfc,0x66,0x66,0x7c,0x6c,0x66,0xe6,0x00 }, /* 0x51 */
	{ 0x7c,0xc6,0x60,0x38,0x0c,0xc6,0x7c,0x00 }, /* 0x52 */
	{ 0x7e,0x7e,0x5a,0x18,0x18,0x18,0x3c,0x00 }, /* 0x53 */
	{ 0xc6,0xc6,0xc6,0xc6,0xc6,0xc6,0x7c,0x00 }, /* 0x54 */
	{ 0xc6,0xc6,0xc6,0xc6,0xc6,0x6c,0x38,0x00 }, /* 0x55 */
	{ 0xc6,0xc6,0xc6,0xd6,0xfe,0xee,0xc6,0x00 }, /* 0x56 */
	{ 0xc6,0xc6,0x6c,0x38,0x6c,0xc6,0xc6,0x00 }, /* 0x57 */
	{ 0x66,0x66,0x66,0x3c,0x18,0x18,0x3c,0x00 }, /* 0x58 */
	{ 0xfe,0xc6,0x8c,0x18,0x32,0x66,0xfe,0x00 }, /* 0x59 */
	{ 0x3c,0x30,0x30,0x30,0x30,0x30,0x3c,0x00 }, /* 0x5a */
	{ 0xc0,0x60,0x30,0x18,0x0c,0x06,0x02,0x00 }, /* 0x5b */
	{ 0x3c,0x0c,0x0c,0x0c,0x0c,0x0c,0x3c,0x00 }, /* 0x5c */
	{ 0x10,0x38,0x6c,0xc6,0x00,0x00,0x00,0x00 }, /* 0x5d */
	{ 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xff }, /* 0x5e */
	{ 0x30,0x18,0x0c,0x00,0x00,0x00,0x00,0x00 }, /* 0x5f */
	{ 0x00,0x00,0x7c,0x06,0x7e,0xc6,0x7e,0x00 }, /* 0x60 */
	{ 0xe0,0x60,0x7c,0x66,0x66,0x66,0xdc,0x00 }, /* 0x61 */
	{ 0x00,0x00,0x7c,0xc6,0xc0,0xc6,0x7c,0x00 }, /* 0x62 */
	{ 0x1c,0x0c,0x7c,0xcc,0xcc,0xcc,0x76,0x00 }, /* 0x63 */
	{ 0x00,0x00,0x7c,0xc6,0xfe,0xc0,0x7c,0x00 }, /* 0x64 */
	{ 0x3c,0x66,0x60,0xf8,0x60,0x60,0xf0,0x00 }, /* 0x65 */
	{ 0x00,0x00,0x76,0xcc,0xcc,0x7c,0x0c,0xf8 }, /* 0x66 */
	{ 0xe0,0x60,0x6c,0x76,0x66,0x66,0xe6,0x00 }, /* 0x67 */
	{ 0x18,0x00,0x38,0x18,0x18,0x18,0x3c,0x00 }, /* 0x68 */
	{ 0x06,0x00,0x06,0x06,0x06,0x66,0x66,0x3c }, /* 0x69 */
	{ 0xe0,0x60,0x66,0x6c,0x78,0x6c,0xe6,0x00 }, /* 0x6a */
	{ 0x38,0x18,0x18,0x18,0x18,0x18,0x3c,0x00 }, /* 0x6b */
	{ 0x00,0x00,0xec,0xfe,0xd6,0xd6,0xc6,0x00 }, /* 0x6c */
	{ 0x00,0x00,0xdc,0x66,0x66,0x66,0x66,0x00 }, /* 0x6d */
	{ 0x00,0x00,0x7c,0xc6,0xc6,0xc6,0x7c,0x00 }, /* 0x6e */
	{ 0x00,0x00,0xdc,0x66,0x66,0x7c,0x60,0xf0 }, /* 0x6f */
	{ 0x00,0x00,0x76,0xcc,0xcc,0x7c,0x0c,0x1e }, /* 0x70 */
	{ 0x00,0x00,0xdc,0x76,0x66,0x60,0xf0,0x00 }, /* 0x71 */
	{ 0x00,0x00,0x7e,0xc0,0x7c,0x06,0xfc,0x00 }, /* 0x72 */
	{ 0x30,0x30,0xfc,0x30,0x30,0x36,0x1c,0x00 }, /* 0x73 */
	{ 0x00,0x00,0xcc,0xcc,0xcc,0xcc,0x76,0x00 }, /* 0x74 */
	{ 0x00,0x00,0xc6,0xc6,0xc6,0x6c,0x38,0x00 }, /* 0x75 */
	{ 0x00,0x00,0xc6,0xd6,0xd6,0xfe,0x6c,0x00 }, /* 0x76 */
	{ 0x00,0x00,0xc6,0x6c,0x38,0x6c,0xc6,0x00 }, /* 0x77 */
	{ 0x00,0x00,0xc6,0xc6,0xc6,0x7e,0x06,0xfc }, /* 0x78 */
	{ 0x00,0x00,0xfe,0x8c,0x18,0x32,0xfe,0x00 }, /* 0x79 */
	{ 0x0e,0x18,0x18,0x70,0x18,0x18,0x0e,0x00 }, /* 0x7a */
	{ 0x18,0x18,0x18,0x00,0x18,0x18,0x18,0x00 }, /* 0x7b */
	{ 0x70,0x18,0x18,0x0e,0x18,0x18,0x70,0x00 }, /* 0x7c */
	{ 0x76,0xdc,0x00,0x00,0x00,0x00,0x00,0x00 }, /* 0x7d */
	{ 0x00,0x10,0x38,0x6c,0xc6,0xc6,0xfe,0x00 }, /* 0x7e */
	{ 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 }  /* 0x7f */
};

static int sdl_calc_key_from_name(const TilemCalc *calc, const char *name)
{
	int i;

	for (i = 0; i < 64; i++) {
		if (calc->hw.keynames[i]
		    && !strcmp(calc->hw.keynames[i], name))
			return i + 1;
	}

	for (i = 0; i < 64; i++) {
		if (!calc->hw.keynames[i])
			continue;

		if (!strcmp(name, "Matrix")
		    && !strcmp(calc->hw.keynames[i], "Apps"))
			return i + 1;
		if (!strcmp(name, "Apps")
		    && !strcmp(calc->hw.keynames[i], "AppsMenu"))
			return i + 1;
		if (!strcmp(name, "List")
		    && !strcmp(calc->hw.keynames[i], "StatEd"))
			return i + 1;
		if (!strcmp(name, "Power")
		    && !strcmp(calc->hw.keynames[i], "Expon"))
			return i + 1;
		if (!strcmp(name, "Stat")
		    && !strcmp(calc->hw.keynames[i], "Table"))
			return i + 1;
	}

	return 0;
}

static SDL_Keycode sdl_keycode_from_name(const char *name,
                                         gboolean *force_shift)
{
	const char *kp;
	size_t len;

	if (force_shift)
		*force_shift = FALSE;

	if (!name || !*name)
		return SDLK_UNKNOWN;

	len = strlen(name);
	if (len == 1) {
		char c = name[0];
		if (g_ascii_isalpha(c)) {
			if (g_ascii_isupper(c) && force_shift)
				*force_shift = TRUE;
			c = g_ascii_tolower(c);
		}
		return (SDL_Keycode) c;
	}

	if (!g_ascii_strcasecmp(name, "BackSpace"))
		return SDLK_BACKSPACE;
	if (!g_ascii_strcasecmp(name, "Delete"))
		return SDLK_DELETE;
	if (!g_ascii_strcasecmp(name, "Escape"))
		return SDLK_ESCAPE;
	if (!g_ascii_strcasecmp(name, "Return"))
		return SDLK_RETURN;
	if (!g_ascii_strcasecmp(name, "ISO_Enter"))
		return SDLK_RETURN;
	if (!g_ascii_strcasecmp(name, "Tab"))
		return SDLK_TAB;
	if (!g_ascii_strcasecmp(name, "ISO_Left_Tab")) {
		if (force_shift)
			*force_shift = TRUE;
		return SDLK_TAB;
	}
	if (!g_ascii_strcasecmp(name, "Insert"))
		return SDLK_INSERT;
	if (!g_ascii_strcasecmp(name, "Home"))
		return SDLK_HOME;
	if (!g_ascii_strcasecmp(name, "End"))
		return SDLK_END;
	if (!g_ascii_strcasecmp(name, "Page_Up"))
		return SDLK_PAGEUP;
	if (!g_ascii_strcasecmp(name, "Page_Down"))
		return SDLK_PAGEDOWN;
	if (!g_ascii_strcasecmp(name, "Left"))
		return SDLK_LEFT;
	if (!g_ascii_strcasecmp(name, "Right"))
		return SDLK_RIGHT;
	if (!g_ascii_strcasecmp(name, "Up"))
		return SDLK_UP;
	if (!g_ascii_strcasecmp(name, "Down"))
		return SDLK_DOWN;
	if (!g_ascii_strcasecmp(name, "Menu"))
		return SDLK_MENU;
	if (!g_ascii_strcasecmp(name, "space"))
		return SDLK_SPACE;

	if (!g_ascii_strcasecmp(name, "comma"))
		return ',';
	if (!g_ascii_strcasecmp(name, "period"))
		return '.';
	if (!g_ascii_strcasecmp(name, "slash"))
		return '/';
	if (!g_ascii_strcasecmp(name, "backslash"))
		return '\\';
	if (!g_ascii_strcasecmp(name, "apostrophe"))
		return '\'';
	if (!g_ascii_strcasecmp(name, "quotedbl"))
		return '"';
	if (!g_ascii_strcasecmp(name, "colon"))
		return ':';
	if (!g_ascii_strcasecmp(name, "question"))
		return '?';
	if (!g_ascii_strcasecmp(name, "minus"))
		return '-';
	if (!g_ascii_strcasecmp(name, "plus"))
		return '+';
	if (!g_ascii_strcasecmp(name, "asterisk"))
		return '*';
	if (!g_ascii_strcasecmp(name, "equal"))
		return '=';
	if (!g_ascii_strcasecmp(name, "greater"))
		return '>';
	if (!g_ascii_strcasecmp(name, "less"))
		return '<';
	if (!g_ascii_strcasecmp(name, "parenleft"))
		return '(';
	if (!g_ascii_strcasecmp(name, "parenright"))
		return ')';
	if (!g_ascii_strcasecmp(name, "braceleft"))
		return '{';
	if (!g_ascii_strcasecmp(name, "braceright"))
		return '}';
	if (!g_ascii_strcasecmp(name, "bracketleft"))
		return '[';
	if (!g_ascii_strcasecmp(name, "bracketright"))
		return ']';
	if (!g_ascii_strcasecmp(name, "underscore"))
		return '_';
	if (!g_ascii_strcasecmp(name, "bar"))
		return '|';
	if (!g_ascii_strcasecmp(name, "brokenbar"))
		return 0x00a6;
	if (!g_ascii_strcasecmp(name, "asciitilde"))
		return '~';
	if (!g_ascii_strcasecmp(name, "dead_tilde"))
		return '~';
	if (!g_ascii_strcasecmp(name, "asciicircum"))
		return '^';
	if (!g_ascii_strcasecmp(name, "dead_circumflex"))
		return '^';
	if (!g_ascii_strcasecmp(name, "numbersign"))
		return '#';
	if (!g_ascii_strcasecmp(name, "dollar"))
		return '$';
	if (!g_ascii_strcasecmp(name, "percent"))
		return '%';
	if (!g_ascii_strcasecmp(name, "ampersand"))
		return '&';
	if (!g_ascii_strcasecmp(name, "at"))
		return '@';
	if (!g_ascii_strcasecmp(name, "plusminus"))
		return 0x00b1;
	if (!g_ascii_strcasecmp(name, "onesuperior"))
		return 0x00b9;
	if (!g_ascii_strcasecmp(name, "twosuperior"))
		return 0x00b2;
	if (!g_ascii_strcasecmp(name, "onehalf"))
		return 0x00bd;
	if (!g_ascii_strcasecmp(name, "EuroSign"))
		return 0x20ac;

	if (len >= 2 && (name[0] == 'F' || name[0] == 'f')) {
		int fn = atoi(name + 1);
		if (fn >= 1 && fn <= 24)
			return SDLK_F1 + (fn - 1);
	}

	kp = g_str_has_prefix(name, "KP_") ? name + 3 : NULL;
	if (kp && g_ascii_isdigit(kp[0]) && kp[1] == '\0')
		return SDLK_KP_0 + (kp[0] - '0');
	if (kp && !g_ascii_strcasecmp(kp, "Add"))
		return SDLK_KP_PLUS;
	if (kp && !g_ascii_strcasecmp(kp, "Subtract"))
		return SDLK_KP_MINUS;
	if (kp && !g_ascii_strcasecmp(kp, "Multiply"))
		return SDLK_KP_MULTIPLY;
	if (kp && !g_ascii_strcasecmp(kp, "Divide"))
		return SDLK_KP_DIVIDE;
	if (kp && !g_ascii_strcasecmp(kp, "Decimal"))
		return SDLK_KP_PERIOD;
	if (kp && !g_ascii_strcasecmp(kp, "Enter"))
		return SDLK_KP_ENTER;
	if (kp && !g_ascii_strcasecmp(kp, "Tab"))
		return SDLK_TAB;
	if (kp && !g_ascii_strcasecmp(kp, "Up"))
		return SDLK_KP_8;
	if (kp && !g_ascii_strcasecmp(kp, "Down"))
		return SDLK_KP_2;
	if (kp && !g_ascii_strcasecmp(kp, "Left"))
		return SDLK_KP_4;
	if (kp && !g_ascii_strcasecmp(kp, "Right"))
		return SDLK_KP_6;
	if (kp && !g_ascii_strcasecmp(kp, "Home"))
		return SDLK_KP_7;
	if (kp && !g_ascii_strcasecmp(kp, "End"))
		return SDLK_KP_1;
	if (kp && !g_ascii_strcasecmp(kp, "Page_Up"))
		return SDLK_KP_9;
	if (kp && !g_ascii_strcasecmp(kp, "Page_Down"))
		return SDLK_KP_3;
	if (kp && !g_ascii_strcasecmp(kp, "Insert"))
		return SDLK_KP_0;
	if (kp && !g_ascii_strcasecmp(kp, "Delete"))
		return SDLK_KP_PERIOD;

	return SDLK_UNKNOWN;
}

static gboolean sdl_parse_binding(TilemKeyBinding *kb,
                                  const char *pckeys, const char *tikeys,
                                  const TilemCalc *calc)
{
	const char *p;
	char *s;
	int n, k;
	gboolean force_shift = FALSE;

	kb->modifiers = 0;
	kb->keysym = 0;
	kb->nscancodes = 0;
	kb->scancodes = NULL;

	while ((p = strchr(pckeys, '+'))) {
		s = g_strndup(pckeys, p - pckeys);
		g_strstrip(s);
		if (!g_ascii_strcasecmp(s, "ctrl")
		    || !g_ascii_strcasecmp(s, "control"))
			kb->modifiers |= KMOD_CTRL;
		else if (!g_ascii_strcasecmp(s, "shift"))
			kb->modifiers |= KMOD_SHIFT;
		else if (!g_ascii_strcasecmp(s, "alt")
		         || !g_ascii_strcasecmp(s, "mod1"))
			kb->modifiers |= KMOD_ALT;
		else if (!g_ascii_strcasecmp(s, "capslock")
		         || !g_ascii_strcasecmp(s, "lock"))
			kb->modifiers |= KMOD_CAPS;
		else {
			g_free(s);
			return FALSE;
		}
		g_free(s);
		pckeys = p + 1;
	}

	s = g_strstrip(g_strdup(pckeys));
	kb->keysym = sdl_keycode_from_name(s, &force_shift);
	g_free(s);
	if (!kb->keysym)
		return FALSE;
	if (force_shift)
		kb->modifiers |= KMOD_SHIFT;

	n = 0;
	do {
		if ((p = strchr(tikeys, ',')))
			s = g_strndup(tikeys, p - tikeys);
		else
			s = g_strdup(tikeys);
		g_strstrip(s);

		k = sdl_calc_key_from_name(calc, s);
		g_free(s);

		if (!k) {
			g_free(kb->scancodes);
			kb->scancodes = NULL;
			return FALSE;
		}

		kb->nscancodes++;
		if (kb->nscancodes >= n) {
			n = kb->nscancodes * 2;
			kb->scancodes = g_renew(byte, kb->scancodes, n);
		}
		kb->scancodes[kb->nscancodes - 1] = k;

		tikeys = (p ? p + 1 : NULL);
	} while (tikeys);

	return TRUE;
}

static void sdl_parse_binding_group(TilemCalcEmulator *emu, GKeyFile *gkf,
                                    const char *group, int maxdepth)
{
	gchar **keys, **groups;
	char *k, *v;
	int i, n;

	keys = g_key_file_get_keys(gkf, group, NULL, NULL);
	if (!keys) {
		g_printerr("no bindings for %s\n", group);
		return;
	}

	for (i = 0; keys[i]; i++)
		;

	n = emu->nkeybindings;
	emu->keybindings = g_renew(TilemKeyBinding, emu->keybindings, n + i);

	for (i = 0; keys[i]; i++) {
		k = keys[i];
		if (!strcmp(k, "INHERIT"))
			continue;

		v = g_key_file_get_value(gkf, group, k, NULL);
		if (!v)
			continue;

		if (sdl_parse_binding(&emu->keybindings[n], k, v, emu->calc))
			n++;
		else
			g_printerr("syntax error in key bindings: '%s=%s'\n",
			           k, v);
		g_free(v);
	}

	emu->nkeybindings = n;

	g_strfreev(keys);

	if (maxdepth == 0)
		return;

	groups = g_key_file_get_string_list(gkf, group, "INHERIT",
	                                    NULL, NULL);
	for (i = 0; groups && groups[i]; i++)
		sdl_parse_binding_group(emu, gkf, groups[i], maxdepth - 1);
	g_strfreev(groups);
}

static void sdl_keybindings_init(TilemCalcEmulator *emu, const char *model)
{
	char *kfname = get_shared_file_path("keybindings.ini", NULL);
	GKeyFile *gkf;
	GError *err = NULL;
	int i;

	g_return_if_fail(emu != NULL);
	g_return_if_fail(emu->calc != NULL);

	if (!kfname) {
		g_printerr("Unable to load key bindings: keybindings.ini not found.\n");
		return;
	}

	gkf = g_key_file_new();
	if (!g_key_file_load_from_file(gkf, kfname, 0, &err)) {
		g_printerr("Unable to load key bindings: %s\n",
		           err ? err->message : "unknown error");
		g_clear_error(&err);
		g_key_file_free(gkf);
		g_free(kfname);
		return;
	}

	for (i = 0; i < emu->nkeybindings; i++)
		g_free(emu->keybindings[i].scancodes);
	g_free(emu->keybindings);
	emu->keybindings = NULL;
	emu->nkeybindings = 0;

	sdl_parse_binding_group(emu, gkf, model, 5);

	g_key_file_free(gkf);
	g_free(kfname);
}

static TilemKeyBinding *sdl_find_key_binding_for_key(TilemCalcEmulator *emu,
                                                     SDL_Keycode key,
                                                     Uint16 mods)
{
	int i;

	for (i = 0; i < emu->nkeybindings; i++) {
		if (key == (SDL_Keycode) emu->keybindings[i].keysym
		    && mods == emu->keybindings[i].modifiers)
			return &emu->keybindings[i];
	}

	return NULL;
}

static TilemKeyBinding *sdl_find_key_binding(TilemCalcEmulator *emu,
                                             const SDL_KeyboardEvent *event)
{
	SDL_Keycode sym;
	SDL_Keycode base;
	Uint16 mods;
	Uint16 caps;
	TilemKeyBinding *kb;

	sym = event->keysym.sym;
	base = SDL_GetKeyFromScancode(event->keysym.scancode);

	if (sym >= 'A' && sym <= 'Z')
		sym = sym - 'A' + 'a';

	mods = event->keysym.mod & (KMOD_SHIFT | KMOD_CTRL | KMOD_ALT);
	caps = event->keysym.mod & KMOD_CAPS;

	if ((mods & KMOD_SHIFT) && sym != base)
		mods &= ~KMOD_SHIFT;

	if (caps) {
		kb = sdl_find_key_binding_for_key(emu, sym, mods | KMOD_CAPS);
		if (kb)
			return kb;
	}

	return sdl_find_key_binding_for_key(emu, sym, mods);
}

/* Table for translating skin-file key number into a scancode. */
static const int keycode_map[] =
	{ TILEM_KEY_YEQU,
	  TILEM_KEY_WINDOW,
	  TILEM_KEY_ZOOM,
	  TILEM_KEY_TRACE,
	  TILEM_KEY_GRAPH,

	  TILEM_KEY_2ND,
	  TILEM_KEY_MODE,
	  TILEM_KEY_DEL,
	  TILEM_KEY_LEFT,
	  TILEM_KEY_RIGHT,
	  TILEM_KEY_UP,
	  TILEM_KEY_DOWN,
	  TILEM_KEY_ALPHA,
	  TILEM_KEY_GRAPHVAR,
	  TILEM_KEY_STAT,

	  TILEM_KEY_MATH,
	  TILEM_KEY_MATRIX,
	  TILEM_KEY_PRGM,
	  TILEM_KEY_VARS,
	  TILEM_KEY_CLEAR,

	  TILEM_KEY_RECIP,
	  TILEM_KEY_SIN,
	  TILEM_KEY_COS,
	  TILEM_KEY_TAN,
	  TILEM_KEY_POWER,

	  TILEM_KEY_SQUARE,
	  TILEM_KEY_COMMA,
	  TILEM_KEY_LPAREN,
	  TILEM_KEY_RPAREN,
	  TILEM_KEY_DIV,

	  TILEM_KEY_LOG,
	  TILEM_KEY_7,
	  TILEM_KEY_8,
	  TILEM_KEY_9,
	  TILEM_KEY_MUL,

	  TILEM_KEY_LN,
	  TILEM_KEY_4,
	  TILEM_KEY_5,
	  TILEM_KEY_6,
	  TILEM_KEY_SUB,

	  TILEM_KEY_STORE,
	  TILEM_KEY_1,
	  TILEM_KEY_2,
	  TILEM_KEY_3,
	  TILEM_KEY_ADD,

	  TILEM_KEY_ON,
	  TILEM_KEY_0,
	  TILEM_KEY_DECPNT,
	  TILEM_KEY_CHS,
	  TILEM_KEY_ENTER };

static int sdl_guess_model_from_state(const char *statefile)
{
	FILE *savfile;
	int model = 0;

	savfile = g_fopen(statefile, "rb");
	if (!savfile)
		return 0;

	model = tilem_get_sav_type(savfile);
	fclose(savfile);
	return model;
}

static int sdl_guess_model_from_rom(const char *romfile)
{
	FILE *rom = g_fopen(romfile, "rb");
	int model = 0;

	if (!rom)
		return 0;

	model = tilem_guess_rom_type(rom);
	fclose(rom);
	return model;
}

static gboolean sdl_load_state(TilemSdlUi *ui, const char *romfile,
                               const char *statefile, int model,
                               GError **err)
{
	int load_model = model;

	if (!load_model && statefile)
		load_model = sdl_guess_model_from_state(statefile);
	if (!load_model && romfile)
		load_model = sdl_guess_model_from_rom(romfile);

	return tilem_calc_emulator_load_state(ui->emu, romfile, statefile,
	                                      load_model, err);
}

static void sdl_report_error(const char *title, GError *err)
{
	if (!err)
		return;

	g_printerr("%s: %s\n", title, err->message);
	g_error_free(err);
}

static void sdl_show_message(TilemSdlUi *ui, const char *title,
                             const char *message)
{
	if (!message)
		return;

	SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, title, message,
	                         ui ? ui->window : NULL);
}

static void sdl_init_ttf(TilemSdlUi *ui);
static void sdl_shutdown_ttf(TilemSdlUi *ui);

static char *sdl_find_font_path(void)
{
	const char *env = g_getenv("TILEM_SDL_FONT");
	const char *candidates[] = {
		"/System/Library/Fonts/Supplemental/Arial.ttf",
		"/System/Library/Fonts/Supplemental/Helvetica.ttf",
		"/System/Library/Fonts/Supplemental/Verdana.ttf",
		"/Library/Fonts/Arial.ttf",
		"/Library/Fonts/Helvetica.ttf",
		"/Library/Fonts/Verdana.ttf",
		"/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
		"/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
		"/usr/share/fonts/truetype/freefont/FreeSans.ttf",
		NULL
	};
	int i;

	if (env && *env)
		return g_strdup(env);

	for (i = 0; candidates[i]; i++) {
		if (g_file_test(candidates[i], G_FILE_TEST_IS_REGULAR))
			return g_strdup(candidates[i]);
	}

	return NULL;
}

static char *sdl_spawn_capture(char **argv)
{
	char *out = NULL;
	char *err = NULL;
	int status = 0;

	if (!g_spawn_sync(NULL, argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL,
	                  &out, &err, &status, NULL)) {
		g_free(out);
		g_free(err);
		return NULL;
	}

	if (!g_spawn_check_exit_status(status, NULL)) {
		g_free(out);
		g_free(err);
		return NULL;
	}

	if (!out || !*out) {
		g_free(out);
		g_free(err);
		return NULL;
	}

	g_strchomp(out);
	g_free(err);
	return out;
}

static char *sdl_applescript_escape(const char *str)
{
	GString *out;
	const char *p;

	if (!str)
		return g_strdup("");

	out = g_string_new(NULL);
	for (p = str; *p; p++) {
		if (*p == '\\' || *p == '"')
			g_string_append_c(out, '\\');
		g_string_append_c(out, *p);
	}

	return g_string_free(out, FALSE);
}

static char *sdl_native_file_dialog(const char *title,
                                    const char *suggest_dir,
                                    const char *suggest_name,
                                    gboolean save,
                                    gboolean *used_native)
{
	if (used_native)
		*used_native = FALSE;

#ifdef __APPLE__
	{
		char *escaped_title = sdl_applescript_escape(title);
		char *escaped_dir = sdl_applescript_escape(suggest_dir);
		char *escaped_name = sdl_applescript_escape(suggest_name);
		char *script;
		char *argv[4];
		char *result;
		GString *cmd = g_string_new(NULL);

		if (save)
			g_string_append_printf(cmd,
			                       "choose file name with prompt \"%s\"",
			                       escaped_title);
		else
			g_string_append_printf(cmd,
			                       "choose file with prompt \"%s\"",
			                       escaped_title);

		if (escaped_dir && *escaped_dir) {
			g_string_append_printf(cmd,
			                       " default location POSIX file \"%s\"",
			                       escaped_dir);
		}

		if (save && escaped_name && *escaped_name) {
			g_string_append_printf(cmd,
			                       " default name \"%s\"",
			                       escaped_name);
		}

		script = g_strdup_printf("POSIX path of (%s)", cmd->str);

		argv[0] = (char *)"osascript";
		argv[1] = (char *)"-e";
		argv[2] = script;
		argv[3] = NULL;

		if (used_native)
			*used_native = TRUE;
		result = sdl_spawn_capture(argv);

		g_string_free(cmd, TRUE);
		g_free(script);
		g_free(escaped_title);
		g_free(escaped_dir);
		g_free(escaped_name);
		return result;
	}
#elif defined(__linux__)
	{
		char *prog = g_find_program_in_path("zenity");
		char *result = NULL;
		char *filename = NULL;

		if (prog) {
			GPtrArray *argv = g_ptr_array_new();

			g_ptr_array_add(argv, prog);
			g_ptr_array_add(argv, (char *)"--file-selection");
			if (save)
				g_ptr_array_add(argv, (char *)"--save");
			g_ptr_array_add(argv, (char *)"--title");
			g_ptr_array_add(argv, (char *)title);
			if (suggest_dir && *suggest_dir) {
				if (save && suggest_name)
					filename = g_build_filename(suggest_dir,
					                            suggest_name, NULL);
				else
					filename = g_strconcat(suggest_dir,
					                       G_DIR_SEPARATOR_S, NULL);
				g_ptr_array_add(argv, (char *)"--filename");
				g_ptr_array_add(argv, filename);
			}
			g_ptr_array_add(argv, NULL);

			if (used_native)
				*used_native = TRUE;
			result = sdl_spawn_capture((char **) argv->pdata);

			g_ptr_array_free(argv, TRUE);
			g_free(prog);
			g_free(filename);
			return result;
		}

		prog = g_find_program_in_path("kdialog");
		if (prog) {
			char *argv[6];
			char *path = NULL;
			int i = 0;

			argv[i++] = prog;
			if (save)
				argv[i++] = (char *)"--getsavefilename";
			else
				argv[i++] = (char *)"--getopenfilename";

			if (suggest_dir && *suggest_dir) {
				if (save && suggest_name)
					path = g_build_filename(suggest_dir,
					                        suggest_name, NULL);
				else
					path = g_strdup(suggest_dir);
				argv[i++] = path;
			}

			argv[i++] = (char *)"--title";
			argv[i++] = (char *)title;
			argv[i++] = NULL;

			if (used_native)
				*used_native = TRUE;
			result = sdl_spawn_capture(argv);

			g_free(path);
			g_free(prog);
			return result;
		}
	}
#endif

	return NULL;
}

static int sdl_text_width(const char *text, int scale)
{
	return (int) strlen(text) * 8 * scale;
}

static void sdl_draw_char(SDL_Renderer *renderer, int x, int y,
                          int scale, unsigned char c, SDL_Color color)
{
	int use_scale = scale > 0 ? scale : 1;
	int row, col;
	const unsigned char *glyph;
	SDL_Rect pixel;

	if (c == 0x20) {
		static const unsigned char blank[8] = { 0 };
		glyph = blank;
	}
	else if (c >= 0x21 && c <= 0x7e) {
		/* Font table is shifted for printable ASCII. */
		glyph = sdl_font8x8_basic[c - 1];
	}
	else {
		glyph = sdl_font8x8_basic[c];
	}
	SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);

	pixel.w = use_scale;
	pixel.h = use_scale;

	for (row = 0; row < 8; row++) {
		for (col = 0; col < 8; col++) {
			if (glyph[row] & (0x80 >> col)) {
				pixel.x = x + col * use_scale;
				pixel.y = y + row * use_scale;
				SDL_RenderFillRect(renderer, &pixel);
			}
		}
	}
}

static void sdl_draw_text(SDL_Renderer *renderer, int x, int y,
                          int scale, const char *text, SDL_Color color)
{
	size_t i;

	for (i = 0; text[i]; i++) {
		unsigned char c = (unsigned char) text[i];
		if (c < 128)
			sdl_draw_char(renderer, x + (int) i * 8 * scale,
			              y, scale, c, color);
	}
}

static char *sdl_default_skin_path(TilemCalcEmulator *emu)
{
	const char *model;
	char *name = NULL;
	char *path = NULL;

	g_return_val_if_fail(emu != NULL, NULL);
	g_return_val_if_fail(emu->calc != NULL, NULL);

	model = emu->calc->hw.name;
	tilem_config_get(model, "skin/f", &name, NULL);

	if (!name)
		name = g_strdup_printf("%s.skn", model);

	if (!g_path_is_absolute(name))
		path = get_shared_file_path("skins", name, NULL);
	else
		path = g_strdup(name);

	g_free(name);
	return path;
}

static void sdl_free_skin(TilemSdlUi *ui)
{
	if (ui->skin_texture)
		SDL_DestroyTexture(ui->skin_texture);
	ui->skin_texture = NULL;

	if (ui->skin) {
		skin_unload(ui->skin);
		g_free(ui->skin);
	}
	ui->skin = NULL;

	g_free(ui->skin_file_name);
	ui->skin_file_name = NULL;
}

static gboolean sdl_load_skin(TilemSdlUi *ui, const char *filename,
                              GError **err)
{
	sdl_free_skin(ui);

	ui->skin = g_new0(SKIN_INFOS, 1);
	if (skin_load(ui->skin, filename, err)) {
		skin_unload(ui->skin);
		g_free(ui->skin);
		ui->skin = NULL;
		return FALSE;
	}

	if (ui->renderer) {
		ui->skin_texture = tilem_sdl_texture_from_pixbuf(ui->renderer,
		                                                 ui->skin->raw);
		if (!ui->skin_texture) {
			g_set_error(err, G_FILE_ERROR, G_FILE_ERROR_FAILED,
			            "Unable to create SDL texture for %s", filename);
			sdl_free_skin(ui);
			return FALSE;
		}
	}

	ui->skin_file_name = g_strdup(filename);
	return TRUE;
}

static void sdl_set_palette(TilemSdlUi *ui)
{
	int r_dark, g_dark, b_dark;
	int r_light, g_light, b_light;

	if (ui->lcd_palette) {
		tilem_free(ui->lcd_palette);
		ui->lcd_palette = NULL;
	}

	if (ui->skin) {
		r_dark = (ui->skin->lcd_black >> 16) & 0xff;
		g_dark = (ui->skin->lcd_black >> 8) & 0xff;
		b_dark = ui->skin->lcd_black & 0xff;

		r_light = (ui->skin->lcd_white >> 16) & 0xff;
		g_light = (ui->skin->lcd_white >> 8) & 0xff;
		b_light = ui->skin->lcd_white & 0xff;
	}
	else {
		r_dark = (LCD_LOW_BLACK >> 16) & 0xff;
		g_dark = (LCD_LOW_BLACK >> 8) & 0xff;
		b_dark = LCD_LOW_BLACK & 0xff;

		r_light = (LCD_LOW_WHITE >> 16) & 0xff;
		g_light = (LCD_LOW_WHITE >> 8) & 0xff;
		b_light = LCD_LOW_WHITE & 0xff;
	}

	ui->lcd_palette = tilem_color_palette_new(r_light, g_light, b_light,
	                                          r_dark, g_dark, b_dark,
	                                          SDL_LCD_GAMMA);
}

static void sdl_resize_lcd_texture(TilemSdlUi *ui, int width, int height)
{
	if (width <= 0 || height <= 0)
		return;

	if (ui->lcd_pixels_width == width
	    && ui->lcd_pixels_height == height)
		return;

	g_free(ui->lcd_pixels);
	ui->lcd_pixels = NULL;

	if (ui->lcd_texture)
		SDL_DestroyTexture(ui->lcd_texture);
	ui->lcd_texture = NULL;

	ui->lcd_pixels_width = width;
	ui->lcd_pixels_height = height;
	ui->lcd_pixels_stride = width * 3;

	ui->lcd_pixels = g_new(byte, ui->lcd_pixels_stride * height);

	ui->lcd_texture = SDL_CreateTexture(ui->renderer, SDL_PIXELFORMAT_RGB24,
	                                    SDL_TEXTUREACCESS_STREAMING,
	                                    width, height);
	SDL_SetTextureBlendMode(ui->lcd_texture, SDL_BLENDMODE_NONE);
}

static void sdl_update_layout(TilemSdlUi *ui, int win_w, int win_h)
{
	int base_w, base_h;
	double rx, ry, r;
	int scaled_w, scaled_h;

	ui->window_width = win_w;
	ui->window_height = win_h;

	if (ui->skin) {
		base_w = ui->skin->width;
		base_h = ui->skin->height;
	}
	else {
		base_w = ui->lcd_width;
		base_h = ui->lcd_height;
	}

	rx = (double) win_w / base_w;
	ry = (double) win_h / base_h;
	r = MIN(rx, ry);
	if (r <= 0.0)
		r = 1.0;

	scaled_w = base_w * r + 0.5;
	scaled_h = base_h * r + 0.5;

	ui->skin_rect.x = (win_w - scaled_w) / 2;
	ui->skin_rect.y = (win_h - scaled_h) / 2;
	ui->skin_rect.w = scaled_w;
	ui->skin_rect.h = scaled_h;

	if (ui->skin) {
		int lcdleft, lcdright, lcdtop, lcdbottom;

		ui->skin->sx = ui->skin->sy = 1.0 / r;

		lcdleft = ui->skin->lcd_pos.left * r + 0.5;
		lcdright = ui->skin->lcd_pos.right * r + 0.5;
		lcdtop = ui->skin->lcd_pos.top * r + 0.5;
		lcdbottom = ui->skin->lcd_pos.bottom * r + 0.5;

		ui->lcd_rect.x = ui->skin_rect.x + lcdleft;
		ui->lcd_rect.y = ui->skin_rect.y + lcdtop;
		ui->lcd_rect.w = MAX(lcdright - lcdleft, 1);
		ui->lcd_rect.h = MAX(lcdbottom - lcdtop, 1);
	}
	else {
		ui->lcd_rect.x = ui->skin_rect.x;
		ui->lcd_rect.y = ui->skin_rect.y;
		ui->lcd_rect.w = ui->skin_rect.w;
		ui->lcd_rect.h = ui->skin_rect.h;
	}

	sdl_resize_lcd_texture(ui, ui->lcd_rect.w, ui->lcd_rect.h);
}

static int sdl_menu_item_height(TilemSdlUi *ui)
{
	if (ui->menu_font)
		return TTF_FontHeight(ui->menu_font) + SDL_MENU_PADDING * 2;
	return 8 * SDL_MENU_FONT_SCALE + SDL_MENU_PADDING * 2;
}

static int sdl_menu_text_width(TilemSdlUi *ui, const char *text)
{
	int width = 0;
	int height = 0;

	if (!text)
		return 0;

	if (ui->menu_font) {
		if (TTF_SizeUTF8(ui->menu_font, text, &width, &height) == 0)
			return width;
	}

	return sdl_text_width(text, SDL_MENU_FONT_SCALE);
}

static void sdl_draw_text_menu(TilemSdlUi *ui, int x, int y,
                               const char *text, SDL_Color color)
{
	if (ui->menu_font) {
		SDL_Surface *surface = TTF_RenderUTF8_Blended(ui->menu_font,
		                                              text, color);
		SDL_Texture *texture;
		SDL_Rect dst;

		if (!surface)
			return;

		texture = SDL_CreateTextureFromSurface(ui->renderer, surface);
		if (!texture) {
			SDL_FreeSurface(surface);
			return;
		}

		dst.x = x;
		dst.y = y;
		dst.w = surface->w;
		dst.h = surface->h;
		SDL_RenderCopy(ui->renderer, texture, NULL, &dst);
		SDL_DestroyTexture(texture);
		SDL_FreeSurface(surface);
		return;
	}

	sdl_draw_text(ui->renderer, x, y, SDL_MENU_FONT_SCALE, text, color);
}

static void sdl_menu_calc_size(TilemSdlUi *ui,
                               const TilemSdlMenuItem *items,
                               size_t n_items,
                               int *out_w,
                               int *out_h)
{
	size_t i;
	int maxw = 0;
	int item_h = sdl_menu_item_height(ui);
	int text_w;

	for (i = 0; i < n_items; i++) {
		if (items[i].separator)
			continue;
		text_w = sdl_menu_text_width(ui, items[i].label);
		if (text_w > maxw)
			maxw = text_w;
	}

	if (out_w)
		*out_w = maxw + SDL_MENU_PADDING * 2;
	if (out_h)
		*out_h = (int) n_items * item_h + SDL_MENU_SPACING * 2;
}

static gboolean sdl_menu_contains(int x, int y,
                                  int menu_x, int menu_y,
                                  int menu_w, int menu_h)
{
	if (x < menu_x || y < menu_y)
		return FALSE;
	if (x >= menu_x + menu_w || y >= menu_y + menu_h)
		return FALSE;
	return TRUE;
}

static int sdl_menu_hit_test_items(TilemSdlUi *ui,
                                   const TilemSdlMenuItem *items,
                                   size_t n_items,
                                   int menu_x, int menu_y,
                                   int menu_w, int menu_h,
                                   int x, int y)
{
	int rel_y;
	int item_h;
	int index;

	if (!sdl_menu_contains(x, y, menu_x, menu_y, menu_w, menu_h))
		return -1;

	item_h = sdl_menu_item_height(ui);
	rel_y = y - menu_y - SDL_MENU_SPACING;
	if (rel_y < 0)
		return -1;

	index = rel_y / item_h;
	if (index < 0 || index >= (int) n_items)
		return -1;

	if (items[index].separator)
		return -1;

	return index;
}

static void sdl_menu_show(TilemSdlUi *ui, int x, int y)
{
	int menu_w;
	int menu_h;

	sdl_menu_calc_size(ui, sdl_menu_items, G_N_ELEMENTS(sdl_menu_items),
	                   &menu_w, &menu_h);

	if (x + menu_w > ui->window_width)
		x = MAX(ui->window_width - menu_w, 0);
	if (y + menu_h > ui->window_height)
		y = MAX(ui->window_height - menu_h, 0);

	ui->menu_visible = TRUE;
	ui->menu_x = x;
	ui->menu_y = y;
	ui->menu_width = menu_w;
	ui->menu_height = menu_h;
	ui->menu_selected = -1;
	ui->submenu_visible = FALSE;
	ui->submenu_selected = -1;
}

static void sdl_menu_show_macro(TilemSdlUi *ui, int index)
{
	int item_h = sdl_menu_item_height(ui);
	int menu_w;
	int menu_h;
	int x = ui->menu_x + ui->menu_width;
	int y = ui->menu_y + item_h * index;

	sdl_menu_calc_size(ui, sdl_menu_macro_items,
	                   G_N_ELEMENTS(sdl_menu_macro_items),
	                   &menu_w, &menu_h);

	if (x + menu_w > ui->window_width)
		x = MAX(ui->menu_x - menu_w, 0);
	if (y + menu_h > ui->window_height)
		y = MAX(ui->window_height - menu_h, 0);

	ui->submenu_visible = TRUE;
	ui->submenu_x = x;
	ui->submenu_y = y;
	ui->submenu_width = menu_w;
	ui->submenu_height = menu_h;
	ui->submenu_selected = -1;
}

static void sdl_menu_hide(TilemSdlUi *ui)
{
	ui->menu_visible = FALSE;
	ui->menu_selected = -1;
	ui->submenu_visible = FALSE;
	ui->submenu_selected = -1;
}

static int sdl_menu_hit_test(TilemSdlUi *ui, int x, int y)
{
	if (!ui->menu_visible)
		return -1;
	return sdl_menu_hit_test_items(ui, sdl_menu_items,
	                               G_N_ELEMENTS(sdl_menu_items),
	                               ui->menu_x, ui->menu_y,
	                               ui->menu_width, ui->menu_height,
	                               x, y);
}

static void sdl_render_menu_panel(TilemSdlUi *ui,
                                  const TilemSdlMenuItem *items,
                                  size_t n_items,
                                  int menu_x, int menu_y,
                                  int menu_w, int menu_h,
                                  int selected)
{
	SDL_Rect menu_rect;
	SDL_Rect item_rect;
	size_t i;
	int item_h;
	SDL_Color text_color = { 230, 230, 230, 255 };
	SDL_Color highlight_color = { 60, 110, 170, 255 };
	SDL_Color bg_color = { 40, 40, 40, 240 };
	SDL_Color border_color = { 90, 90, 90, 255 };

	item_h = sdl_menu_item_height(ui);
	menu_rect.x = menu_x;
	menu_rect.y = menu_y;
	menu_rect.w = menu_w;
	menu_rect.h = menu_h;

	SDL_SetRenderDrawColor(ui->renderer, bg_color.r, bg_color.g,
	                       bg_color.b, bg_color.a);
	SDL_RenderFillRect(ui->renderer, &menu_rect);

	SDL_SetRenderDrawColor(ui->renderer, border_color.r, border_color.g,
	                       border_color.b, border_color.a);
	SDL_RenderDrawRect(ui->renderer, &menu_rect);

	for (i = 0; i < n_items; i++) {
		item_rect.x = menu_x;
		item_rect.y = menu_y + SDL_MENU_SPACING
			+ (int) i * item_h;
		item_rect.w = menu_w;
		item_rect.h = item_h;

		if (items[i].separator) {
			SDL_SetRenderDrawColor(ui->renderer, border_color.r,
			                       border_color.g,
			                       border_color.b,
			                       border_color.a);
			SDL_RenderDrawLine(ui->renderer,
			                   item_rect.x + SDL_MENU_PADDING,
			                   item_rect.y + item_rect.h / 2,
			                   item_rect.x + item_rect.w - SDL_MENU_PADDING,
			                   item_rect.y + item_rect.h / 2);
			continue;
		}

		if ((int) i == selected) {
			SDL_SetRenderDrawColor(ui->renderer, highlight_color.r,
			                       highlight_color.g,
			                       highlight_color.b,
			                       highlight_color.a);
			SDL_RenderFillRect(ui->renderer, &item_rect);
		}

		sdl_draw_text_menu(ui,
		                   item_rect.x + SDL_MENU_PADDING,
		                   item_rect.y + SDL_MENU_PADDING,
		                   items[i].label,
		                   text_color);
	}
}

static void sdl_render_menu(TilemSdlUi *ui)
{
	if (!ui->menu_visible)
		return;

	sdl_render_menu_panel(ui, sdl_menu_items,
	                      G_N_ELEMENTS(sdl_menu_items),
	                      ui->menu_x, ui->menu_y,
	                      ui->menu_width, ui->menu_height,
	                      ui->menu_selected);

	if (ui->submenu_visible) {
		sdl_render_menu_panel(ui, sdl_menu_macro_items,
		                      G_N_ELEMENTS(sdl_menu_macro_items),
		                      ui->submenu_x, ui->submenu_y,
		                      ui->submenu_width, ui->submenu_height,
		                      ui->submenu_selected);
	}
}

static void sdl_render_lcd(TilemSdlUi *ui)
{
	SDL_Color text_color = { 200, 200, 200, 255 };
	SDL_Color sub_color = { 160, 160, 160, 255 };
	const char *line1 = "No ROM loaded";
	const char *line2 = "Right-click to open";
	int text_w1;
	int text_w2;
	int x1;
	int x2;
	int y1;
	int y2;

	if (!ui->emu->calc || !ui->emu->lcd_buffer) {
		if (ui->menu_visible)
			return;

		text_w1 = sdl_menu_text_width(ui, line1);
		text_w2 = sdl_menu_text_width(ui, line2);
		x1 = ui->lcd_rect.x + (ui->lcd_rect.w - text_w1) / 2;
		x2 = ui->lcd_rect.x + (ui->lcd_rect.w - text_w2) / 2;
		y1 = ui->lcd_rect.y + (ui->lcd_rect.h / 2) - 16;
		y2 = y1 + 20;

		sdl_draw_text_menu(ui, x1, y1, line1, text_color);
		sdl_draw_text_menu(ui, x2, y2, line2, sub_color);
		return;
	}

	if (!ui->lcd_texture || !ui->lcd_pixels || !ui->lcd_palette)
		return;

	g_mutex_lock(ui->emu->lcd_mutex);
	tilem_draw_lcd_image_rgb(ui->emu->lcd_buffer, ui->lcd_pixels,
	                         ui->lcd_pixels_width,
	                         ui->lcd_pixels_height,
	                         ui->lcd_pixels_stride, 3, ui->lcd_palette,
	                         ui->lcd_smooth_scale
	                         ? TILEM_SCALE_SMOOTH
	                         : TILEM_SCALE_FAST);
	g_mutex_unlock(ui->emu->lcd_mutex);

	SDL_UpdateTexture(ui->lcd_texture, NULL, ui->lcd_pixels,
	                  ui->lcd_pixels_stride);
	SDL_RenderCopy(ui->renderer, ui->lcd_texture, NULL, &ui->lcd_rect);
}

static void sdl_render(TilemSdlUi *ui)
{
	SDL_SetRenderDrawColor(ui->renderer, 20, 20, 20, 255);
	SDL_RenderClear(ui->renderer);

	if (ui->skin_texture)
		SDL_RenderCopy(ui->renderer, ui->skin_texture, NULL,
		               &ui->skin_rect);

	sdl_render_lcd(ui);
	sdl_render_menu(ui);
	SDL_RenderPresent(ui->renderer);
}

/* Find keycode for the key (if any) at the given position. */
static int scan_click(const SKIN_INFOS* skin, double x, double y)
{
	guint ix, iy, nearest = 0, i;
	int dx, dy, d, best_d = G_MAXINT;

	if (!skin)
		return 0;

	ix = (x * skin->sx + 0.5);
	iy = (y * skin->sy + 0.5);

	for (i = 0; i < G_N_ELEMENTS(keycode_map); i++) {
		if (ix >= skin->keys_pos[i].left
		    && ix < skin->keys_pos[i].right
		    && iy >= skin->keys_pos[i].top
		    && iy < skin->keys_pos[i].bottom) {
			dx = (skin->keys_pos[i].left + skin->keys_pos[i].right
			      - 2 * ix);
			dy = (skin->keys_pos[i].top + skin->keys_pos[i].bottom
			      - 2 * iy);
			d = ABS(dx) + ABS(dy);

			if (d < best_d) {
				best_d = d;
				nearest = keycode_map[i];
			}
		}
	}

	return nearest;
}

static int sdl_key_from_mouse(TilemSdlUi *ui, int x, int y)
{
	int local_x, local_y;

	if (!ui->skin)
		return 0;

	if (x < ui->skin_rect.x || y < ui->skin_rect.y)
		return 0;
	if (x >= (ui->skin_rect.x + ui->skin_rect.w)
	    || y >= (ui->skin_rect.y + ui->skin_rect.h))
		return 0;

	local_x = x - ui->skin_rect.x;
	local_y = y - ui->skin_rect.y;

	return scan_click(ui->skin, local_x, local_y);
}

static void press_mouse_key(TilemSdlUi *ui, int key)
{
	if (ui->mouse_key == key)
		return;

	tilem_calc_emulator_release_key(ui->emu, ui->mouse_key);
	tilem_calc_emulator_press_key(ui->emu, key);
	ui->mouse_key = key;
}

static gboolean sdl_keycode_active(TilemSdlUi *ui, SDL_Scancode scancode)
{
	int i;

	for (i = 0; i < 64; i++) {
		if (ui->keypress_keycodes[i] == (int) scancode)
			return TRUE;
	}

	return ui->sequence_keycode == (int) scancode;
}

static void sdl_handle_keydown(TilemSdlUi *ui, const SDL_KeyboardEvent *event)
{
	TilemKeyBinding *kb;
	SDL_Scancode scancode;
	int key;

	if (!ui->emu->calc)
		return;

	if (event->repeat)
		return;

	scancode = event->keysym.scancode;
	if (sdl_keycode_active(ui, scancode))
		return;

	kb = sdl_find_key_binding(ui->emu, event);
	if (!kb)
		return;

	if (kb->nscancodes == 1) {
		key = kb->scancodes[0];
		if (tilem_calc_emulator_press_or_queue(ui->emu, key))
			ui->sequence_keycode = (int) scancode;
		else
			ui->keypress_keycodes[key] = (int) scancode;
	}
	else {
		tilem_calc_emulator_queue_keys(ui->emu, kb->scancodes,
		                               kb->nscancodes);
		ui->sequence_keycode = (int) scancode;
	}
}

static void sdl_handle_keyup(TilemSdlUi *ui, const SDL_KeyboardEvent *event)
{
	SDL_Scancode scancode;
	int i;

	if (!ui->emu->calc)
		return;

	scancode = event->keysym.scancode;

	for (i = 0; i < 64; i++) {
		if (ui->keypress_keycodes[i] == (int) scancode) {
			tilem_calc_emulator_release_key(ui->emu, i);
			ui->keypress_keycodes[i] = 0;
		}
	}

	if (ui->sequence_keycode == (int) scancode) {
		tilem_calc_emulator_release_queued_key(ui->emu);
		ui->sequence_keycode = 0;
	}
}

static void sdl_update_skin_for_calc(TilemSdlUi *ui)
{
	char *skin_path = NULL;
	GError *err = NULL;

	if (ui->skin_disabled) {
		sdl_free_skin(ui);
		sdl_set_palette(ui);
		return;
	}

	if (ui->skin_override)
		skin_path = g_strdup(ui->skin_override);
	else
		skin_path = sdl_default_skin_path(ui->emu);

	if (!skin_path) {
		sdl_free_skin(ui);
		sdl_set_palette(ui);
		return;
	}

	if (!sdl_load_skin(ui, skin_path, &err)) {
		sdl_report_error("Unable to load skin", err);
	}

	sdl_set_palette(ui);
	g_free(skin_path);
}

static char *sdl_pick_rom_file(TilemSdlUi *ui)
{
	char *dir;
	char *filename;
	gboolean used_native = FALSE;

	if (ui->emu->rom_file_name)
		dir = g_path_get_dirname(ui->emu->rom_file_name);
	else
		dir = g_get_current_dir();

	filename = sdl_native_file_dialog("Open Calculator",
	                                  dir, NULL, FALSE, &used_native);
	g_free(dir);

	if (!filename && !used_native) {
		sdl_show_message(ui, "Load ROM",
		                 "No native file picker available.");
	}

	return filename;
}

static char *sdl_pick_state_file(TilemSdlUi *ui, const char *title)
{
	char *dir;
	char *filename;
	gboolean used_native = FALSE;

	if (ui->emu->state_file_name)
		dir = g_path_get_dirname(ui->emu->state_file_name);
	else if (ui->emu->rom_file_name)
		dir = g_path_get_dirname(ui->emu->rom_file_name);
	else
		dir = g_get_current_dir();

	filename = sdl_native_file_dialog(title, dir, NULL, FALSE, &used_native);
	g_free(dir);
	if (!filename && !used_native) {
		sdl_show_message(ui, "Load State",
		                 "No native file picker available.");
	}
	return filename;
}

static char *sdl_pick_state_save_file(TilemSdlUi *ui)
{
	char *dir;
	char *suggest_name = NULL;
	char *filename;
	gboolean used_native = FALSE;

	if (ui->emu->state_file_name) {
		dir = g_path_get_dirname(ui->emu->state_file_name);
		suggest_name = g_filename_display_basename(
			ui->emu->state_file_name);
	}
	else if (ui->emu->rom_file_name) {
		dir = g_path_get_dirname(ui->emu->rom_file_name);
		suggest_name = g_strdup("calc.sav");
	}
	else {
		dir = g_get_current_dir();
		suggest_name = g_strdup("calc.sav");
	}

	filename = sdl_native_file_dialog("Save State As", dir, suggest_name,
	                                  TRUE, &used_native);

	g_free(dir);
	g_free(suggest_name);
	if (!filename && !used_native) {
		sdl_show_message(ui, "Save State",
		                 "No native file picker available.");
	}
	return filename;
}

static char *sdl_pick_send_file(TilemSdlUi *ui)
{
	char *dir = NULL;
	char *filename;
	gboolean used_native = FALSE;

	if (!ui->emu->calc)
		return NULL;

	tilem_config_get("upload", "sendfile_recentdir/f", &dir, NULL);
	if (!dir)
		dir = g_get_current_dir();

	filename = sdl_native_file_dialog("Send File", dir, NULL, FALSE,
	                                  &used_native);
	g_free(dir);

	if (!filename && !used_native) {
		sdl_show_message(ui, "Send File",
		                 "No native file picker available.");
	}

	if (filename) {
		dir = g_path_get_dirname(filename);
		tilem_config_set("upload", "sendfile_recentdir/f", dir, NULL);
		g_free(dir);
	}

	return filename;
}

static char *sdl_pick_receive_file(TilemSdlUi *ui)
{
	char *dir = NULL;
	char *filename;
	gboolean used_native = FALSE;

	if (!ui->emu->calc)
		return NULL;

	tilem_config_get("download", "receivefile_recentdir/f", &dir, NULL);
	if (!dir)
		dir = g_get_current_dir();

	filename = sdl_native_file_dialog("Receive File", dir,
	                                  "calculator.tig", TRUE,
	                                  &used_native);
	g_free(dir);

	if (!filename && !used_native) {
		sdl_show_message(ui, "Receive File",
		                 "No native file picker available.");
	}

	if (filename) {
		dir = g_path_get_dirname(filename);
		tilem_config_set("download", "receivefile_recentdir/f", dir,
		                 NULL);
		g_free(dir);
	}

	return filename;
}

static char *sdl_pick_screenshot_file(TilemSdlUi *ui)
{
	char *dir = NULL;
	char *filename;
	gboolean used_native = FALSE;

	if (!ui->emu->calc)
		return NULL;

	tilem_config_get("screenshot", "directory/f", &dir, NULL);
	if (!dir)
		dir = g_get_current_dir();

	filename = sdl_native_file_dialog("Save Screenshot", dir,
	                                  "screenshot.png", TRUE,
	                                  &used_native);
	g_free(dir);

	if (!filename && !used_native) {
		sdl_show_message(ui, "Screenshot",
		                 "No native file picker available.");
	}

	if (filename) {
		dir = g_path_get_dirname(filename);
		tilem_config_set("screenshot", "directory/f", dir, NULL);
		g_free(dir);
	}

	return filename;
}

static char *sdl_pick_macro_file(TilemSdlUi *ui)
{
	char *dir = NULL;
	char *filename;
	gboolean used_native = FALSE;

	tilem_config_get("macro", "directory/f", &dir, NULL);
	if (!dir)
		dir = g_get_current_dir();

	filename = sdl_native_file_dialog("Open Macro", dir, NULL,
	                                  FALSE, &used_native);
	g_free(dir);

	if (!filename && !used_native) {
		sdl_show_message(ui, "Macro",
		                 "No native file picker available.");
	}

	return filename;
}

static char *sdl_pick_macro_save_file(TilemSdlUi *ui)
{
	char *dir = NULL;
	char *filename;
	gboolean used_native = FALSE;

	tilem_config_get("macro", "directory/f", &dir, NULL);
	if (!dir)
		dir = g_get_current_dir();

	filename = sdl_native_file_dialog("Save Macro", dir,
	                                  "macro.txt", TRUE,
	                                  &used_native);
	g_free(dir);

	if (!filename && !used_native) {
		sdl_show_message(ui, "Macro",
		                 "No native file picker available.");
	}

	return filename;
}

static gboolean sdl_save_state_to(TilemSdlUi *ui, const char *savname,
                                  GError **err)
{
	FILE *romfile = NULL;
	FILE *savfile = NULL;
	char *dname;
	int errnum = 0;
	gboolean status = TRUE;

	g_return_val_if_fail(ui != NULL, FALSE);
	g_return_val_if_fail(ui->emu != NULL, FALSE);
	g_return_val_if_fail(ui->emu->calc != NULL, FALSE);
	g_return_val_if_fail(ui->emu->rom_file_name != NULL, FALSE);
	g_return_val_if_fail(savname != NULL, FALSE);

	if (ui->emu->calc->hw.flags & TILEM_CALC_HAS_FLASH) {
		romfile = g_fopen(ui->emu->rom_file_name, "r+b");
		if (!romfile) {
			errnum = errno;
			dname = g_filename_display_basename(
				ui->emu->rom_file_name);
			g_set_error(err, G_FILE_ERROR,
			            g_file_error_from_errno(errnum),
			            "Unable to open %s for writing: %s",
			            dname, g_strerror(errnum));
			g_free(dname);
			return FALSE;
		}
	}

	savfile = g_fopen(savname, "wb");
	if (!savfile) {
		errnum = errno;
		dname = g_filename_display_basename(savname);
		g_set_error(err, G_FILE_ERROR,
		            g_file_error_from_errno(errnum),
		            "Unable to open %s for writing: %s",
		            dname, g_strerror(errnum));
		g_free(dname);
		if (romfile)
			fclose(romfile);
		return FALSE;
	}

	tilem_calc_emulator_lock(ui->emu);

	if (romfile && tilem_calc_save_state(ui->emu->calc, romfile, NULL))
		errnum = errno;
	if (romfile && fclose(romfile))
		errnum = errno;

	if (errnum) {
		dname = g_filename_display_basename(ui->emu->rom_file_name);
		g_set_error(err, G_FILE_ERROR,
		            g_file_error_from_errno(errnum),
		            "Error writing %s: %s",
		            dname, g_strerror(errnum));
		g_free(dname);
		fclose(savfile);
		tilem_calc_emulator_unlock(ui->emu);
		return FALSE;
	}

	if (tilem_calc_save_state(ui->emu->calc, NULL, savfile))
		errnum = errno;
	if (fclose(savfile))
		errnum = errno;

	tilem_calc_emulator_unlock(ui->emu);

	if (errnum) {
		dname = g_filename_display_basename(savname);
		g_set_error(err, G_FILE_ERROR,
		            g_file_error_from_errno(errnum),
		            "Error writing %s: %s",
		            dname, g_strerror(errnum));
		g_free(dname);
		status = FALSE;
	}

	return status;
}

static char *sdl_find_free_filename(const char *folder,
                                    const char *basename,
                                    const char *extension)
{
	int i;
	char *filename;
	char *prefix;
	const char *ext = extension;

	if (ext && ext[0] == '.')
		ext++;
	if (!ext || !*ext)
		ext = "png";

	if (folder)
		prefix = g_build_filename(folder, basename, NULL);
	else
		prefix = g_strdup(basename);

	for (i = 0; i < 999; i++) {
		filename = g_strdup_printf("%s%03d.%s", prefix, i, ext);
		if (!g_file_test(filename, G_FILE_TEST_IS_REGULAR)) {
			g_free(prefix);
			return filename;
		}
		g_free(filename);
	}

	g_free(prefix);
	return NULL;
}

static gboolean sdl_is_wide_screen(TilemSdlUi *ui)
{
	if (!ui->emu->calc)
		return FALSE;
	return (ui->emu->calc->hw.lcdwidth == 128);
}

static void sdl_get_screenshot_size(TilemSdlUi *ui, int *out_w, int *out_h)
{
	int w96 = 0;
	int h96 = 0;
	int w128 = 0;
	int h128 = 0;

	tilem_config_get("screenshot",
	                 "width_96x64/i", &w96,
	                 "height_96x64/i", &h96,
	                 "width_128x64/i", &w128,
	                 "height_128x64/i", &h128,
	                 NULL);

	if (sdl_is_wide_screen(ui)) {
		*out_w = (w128 > 0 ? w128 : SDL_SCREENSHOT_DEFAULT_WIDTH_128);
		*out_h = (h128 > 0 ? h128 : SDL_SCREENSHOT_DEFAULT_HEIGHT_128);
	}
	else {
		*out_w = (w96 > 0 ? w96 : SDL_SCREENSHOT_DEFAULT_WIDTH_96);
		*out_h = (h96 > 0 ? h96 : SDL_SCREENSHOT_DEFAULT_HEIGHT_96);
	}
}

static char *sdl_guess_image_format(const char *filename,
                                    const char *default_format)
{
	const char *dot;
	const char *sep;
	const char *fmt;
	char *lower;

	if (!filename) {
		fmt = default_format ? default_format : "png";
		if (fmt[0] == '.')
			fmt++;
		return g_ascii_strdown(fmt, -1);
	}

	fmt = default_format ? default_format : "png";
	if (fmt[0] == '.')
		fmt++;

	dot = strrchr(filename, '.');
	sep = strrchr(filename, G_DIR_SEPARATOR);
	if (!dot || (sep && dot < sep) || !dot[1]) {
		return g_ascii_strdown(fmt, -1);
	}

	lower = g_ascii_strdown(dot + 1, -1);
	return lower;
}

static char *sdl_ensure_extension(const char *filename, const char *format)
{
	const char *dot;
	const char *sep;
	const char *fmt = format;

	if (!filename)
		return NULL;

	if (fmt && fmt[0] == '.')
		fmt++;
	if (!fmt || !*fmt)
		fmt = "png";

	dot = strrchr(filename, '.');
	sep = strrchr(filename, G_DIR_SEPARATOR);
	if (!dot || (sep && dot < sep))
		return g_strdup_printf("%s.%s", filename, fmt);

	return g_strdup(filename);
}

static gboolean sdl_load_initial(TilemSdlUi *ui,
                                 const TilemSdlOptions *opts)
{
	GError *err = NULL;
	char *modelname = NULL;
	int model = opts->model;

	if (opts->romfile || opts->statefile) {
		if (sdl_load_state(ui, opts->romfile, opts->statefile,
		                   model, &err))
			return TRUE;
		sdl_report_error("Unable to load calculator state", err);
		return FALSE;
	}

	if (!model) {
		tilem_config_get("recent", "last_model/s", &modelname, NULL);
		if (modelname)
			model = name_to_model(modelname);
		g_free(modelname);
	}

	if (model) {
		if (sdl_load_state(ui, NULL, NULL, model, &err))
			return TRUE;
		if (err && !g_error_matches(err, TILEM_EMULATOR_ERROR,
		                            TILEM_EMULATOR_ERROR_NO_ROM))
			sdl_report_error("Unable to load calculator state", err);
		else if (err)
			g_error_free(err);
		err = NULL;
	}

	for (;;) {
		char *rom = sdl_pick_rom_file(ui);
		char *state = NULL;

		if (!rom)
			return TRUE;

		state = sdl_pick_state_file(ui, "Open State (optional)");
		if (sdl_load_state(ui, rom, state, model, &err)) {
			g_free(rom);
			g_free(state);
			return TRUE;
		}

		sdl_report_error("Unable to load calculator state", err);
		g_free(rom);
		g_free(state);
		err = NULL;
	}
}

static void sdl_start_emulation(TilemSdlUi *ui)
{
	if (!ui->emu->calc)
		return;

	if (!ui->emu->z80_thread)
		tilem_calc_emulator_run(ui->emu);
}

static void sdl_update_after_load(TilemSdlUi *ui)
{
	const char *modelname;

	ui->lcd_width = ui->emu->calc->hw.lcdwidth;
	ui->lcd_height = ui->emu->calc->hw.lcdheight;

	modelname = ui->emu->calc->hw.name;
	tilem_config_set(modelname,
	                 "rom_file/f", ui->emu->rom_file_name,
	                 "state_file/f", ui->emu->state_file_name,
	                 NULL);
	tilem_config_set("recent", "last_model/s", modelname, NULL);

	sdl_keybindings_init(ui->emu, modelname);
	memset(ui->keypress_keycodes, 0, sizeof(ui->keypress_keycodes));
	ui->sequence_keycode = 0;

	sdl_update_skin_for_calc(ui);
	sdl_update_layout(ui, ui->window_width, ui->window_height);
}

static void sdl_handle_load_rom(TilemSdlUi *ui)
{
	GError *err = NULL;
	char *rom = sdl_pick_rom_file(ui);
	char *state = NULL;

	if (!rom)
		return;

	state = sdl_pick_state_file(ui, "Open State (optional)");

	if (!sdl_load_state(ui, rom, state, 0, &err)) {
		sdl_report_error("Unable to load calculator state", err);
		g_free(rom);
		g_free(state);
		return;
	}

	sdl_update_after_load(ui);
	sdl_start_emulation(ui);
	g_free(rom);
	g_free(state);
}

static void sdl_handle_send_file(TilemSdlUi *ui)
{
	char *filename;

	if (!ui->emu->calc) {
		sdl_show_message(ui, "Send File",
		                 "No calculator loaded yet.");
		return;
	}

	filename = sdl_pick_send_file(ui);
	if (!filename)
		return;

	tilem_link_send_file(ui->emu, filename, TI81_SLOT_AUTO, TRUE, TRUE);
	g_free(filename);
}

static void sdl_handle_receive_file(TilemSdlUi *ui)
{
	char *filename;

	if (!ui->emu->calc) {
		sdl_show_message(ui, "Receive File",
		                 "No calculator loaded yet.");
		return;
	}

	filename = sdl_pick_receive_file(ui);
	if (!filename)
		return;

	tilem_link_receive_all(ui->emu, filename);
	g_free(filename);
}

static void sdl_handle_save_state(TilemSdlUi *ui)
{
	GError *err = NULL;

	if (!ui->emu->calc)
		return;

	if (!tilem_calc_emulator_save_state(ui->emu, &err))
		sdl_report_error("Unable to save calculator state", err);
}

static void sdl_handle_save_state_as(TilemSdlUi *ui)
{
	GError *err = NULL;
	char *filename;
	const char *modelname;

	if (!ui->emu->calc)
		return;

	filename = sdl_pick_state_save_file(ui);
	if (!filename)
		return;

	if (!sdl_save_state_to(ui, filename, &err)) {
		sdl_report_error("Unable to save calculator state", err);
		g_free(filename);
		return;
	}

	g_free(ui->emu->state_file_name);
	ui->emu->state_file_name = g_strdup(filename);
	if (ui->emu->calc) {
		modelname = ui->emu->calc->hw.name;
		tilem_config_set(modelname,
		                 "rom_file/f", ui->emu->rom_file_name,
		                 "state_file/f", ui->emu->state_file_name,
		                 NULL);
	}
	g_free(filename);
}

static void sdl_handle_open_calc(TilemSdlUi *ui)
{
	sdl_handle_load_rom(ui);
}

static void sdl_handle_save_calc(TilemSdlUi *ui)
{
	if (!ui->emu->calc)
		return;

	if (!ui->emu->state_file_name) {
		sdl_handle_save_state_as(ui);
		return;
	}

	sdl_handle_save_state(ui);
}

static void sdl_handle_revert_calc(TilemSdlUi *ui)
{
	GError *err = NULL;

	if (!ui->emu->calc || !ui->emu->rom_file_name
	    || !ui->emu->state_file_name) {
		sdl_show_message(ui, "Revert Calculator State",
		                 "No saved state available.");
		return;
	}

	if (!tilem_calc_emulator_revert_state(ui->emu, &err))
		sdl_report_error("Unable to load calculator state", err);
}

static void sdl_handle_macro_begin(TilemSdlUi *ui)
{
	tilem_macro_start(ui->emu);
}

static void sdl_handle_macro_end(TilemSdlUi *ui)
{
	tilem_macro_stop(ui->emu);
}

static void sdl_handle_macro_play(TilemSdlUi *ui)
{
	tilem_macro_play(ui->emu);
}

static void sdl_handle_macro_open(TilemSdlUi *ui)
{
	char *filename = sdl_pick_macro_file(ui);

	if (!filename)
		return;

	tilem_macro_load(ui->emu, filename);
	g_free(filename);
}

static void sdl_handle_macro_save(TilemSdlUi *ui)
{
	char *filename = sdl_pick_macro_save_file(ui);

	if (!filename)
		return;

	tilem_macro_write_file_to(ui->emu, filename);
	g_free(filename);
}

static void sdl_handle_screenshot(TilemSdlUi *ui)
{
	GError *err = NULL;
	char *filename;
	char *default_format = NULL;
	char *format;
	char *final_name;
	int width;
	int height;

	if (!ui->emu->calc) {
		sdl_show_message(ui, "Screenshot",
		                 "No calculator loaded yet.");
		return;
	}

	filename = sdl_pick_screenshot_file(ui);
	if (!filename)
		return;

	tilem_config_get("screenshot", "format/s", &default_format, NULL);
	format = sdl_guess_image_format(filename, default_format);
	final_name = sdl_ensure_extension(filename, format);
	sdl_get_screenshot_size(ui, &width, &height);
	if (!ui->lcd_palette)
		sdl_set_palette(ui);

	if (!tilem_sdl_save_screenshot(ui->emu, ui->lcd_smooth_scale,
	                               ui->lcd_palette, width, height,
	                               final_name, format, &err)) {
		sdl_report_error("Unable to save screenshot", err);
	}

	g_free(default_format);
	g_free(format);
	g_free(final_name);
	g_free(filename);
}

static void sdl_handle_quick_screenshot(TilemSdlUi *ui)
{
	GError *err = NULL;
	char *folder = NULL;
	char *format = NULL;
	char *filename;
	int width;
	int height;

	if (!ui->emu->calc) {
		sdl_show_message(ui, "Quick Screenshot",
		                 "No calculator loaded yet.");
		return;
	}

	tilem_config_get("screenshot",
	                 "directory/f", &folder,
	                 "format/s", &format,
	                 NULL);

	if (!folder)
		folder = get_config_file_path("screenshots", NULL);
	if (!format)
		format = g_strdup("png");
	else {
		char *lower = g_ascii_strdown(format, -1);
		g_free(format);
		format = lower;
		if (format[0] == '.') {
			char *stripped = g_strdup(format + 1);
			g_free(format);
			format = stripped;
		}
	}

	g_mkdir_with_parents(folder, 0755);
	filename = sdl_find_free_filename(folder, "screenshot", format);
	sdl_get_screenshot_size(ui, &width, &height);
	if (!ui->lcd_palette)
		sdl_set_palette(ui);

	if (!filename) {
		g_free(folder);
		g_free(format);
		return;
	}

	if (!tilem_sdl_save_screenshot(ui->emu, ui->lcd_smooth_scale,
	                               ui->lcd_palette, width, height,
	                               filename, format, &err)) {
		sdl_report_error("Unable to save screenshot", err);
	}

	g_free(filename);
	g_free(folder);
	g_free(format);
}

static void sdl_handle_preferences(TilemSdlUi *ui)
{
	char *config_path;
	char *message;

	config_path = get_config_file_path("config.ini", NULL);
	message = g_strdup_printf("Preferences are stored in:\n%s",
	                          config_path);
	SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION,
	                         "Preferences", message,
	                         ui ? ui->window : NULL);
	g_free(config_path);
	g_free(message);
}

static void sdl_handle_about(TilemSdlUi *ui)
{
	const char *title = "TilEm II";
	char *body = g_strdup_printf(
		"TilEm II %s\n"
		"\n"
		"TilEm is a TI Linux Emulator.\n"
		"It emulates all current Z80 models:\n"
		"TI73, TI76, TI81, TI82, TI83(+)(SE),\n"
		"TI84+(SE), TI85 and TI86.\n"
		"\n"
		"http://lpg.ticalc.org/prj_tilem/",
		PACKAGE_VERSION);

	SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION,
	                         title, body,
	                         ui ? ui->window : NULL);
	g_free(body);
}

static void sdl_handle_menu_action(TilemSdlUi *ui, TilemSdlMenuAction action,
                                   gboolean *running)
{
	switch (action) {
	case SDL_MENU_SEND_FILE:
		sdl_handle_send_file(ui);
		break;
	case SDL_MENU_RECEIVE_FILE:
		sdl_handle_receive_file(ui);
		break;
	case SDL_MENU_OPEN_CALC:
		sdl_handle_open_calc(ui);
		break;
	case SDL_MENU_SAVE_CALC:
		sdl_handle_save_calc(ui);
		break;
	case SDL_MENU_REVERT_CALC:
		sdl_handle_revert_calc(ui);
		break;
	case SDL_MENU_RESET:
		tilem_calc_emulator_reset(ui->emu);
		break;
	case SDL_MENU_MACRO_BEGIN:
		sdl_handle_macro_begin(ui);
		break;
	case SDL_MENU_MACRO_END:
		sdl_handle_macro_end(ui);
		break;
	case SDL_MENU_MACRO_PLAY:
		sdl_handle_macro_play(ui);
		break;
	case SDL_MENU_MACRO_OPEN:
		sdl_handle_macro_open(ui);
		break;
	case SDL_MENU_MACRO_SAVE:
		sdl_handle_macro_save(ui);
		break;
	case SDL_MENU_SCREENSHOT:
		sdl_handle_screenshot(ui);
		break;
	case SDL_MENU_QUICK_SCREENSHOT:
		sdl_handle_quick_screenshot(ui);
		break;
	case SDL_MENU_PREFERENCES:
		sdl_handle_preferences(ui);
		break;
	case SDL_MENU_ABOUT:
		sdl_handle_about(ui);
		break;
	case SDL_MENU_QUIT:
		*running = FALSE;
		break;
	default:
		break;
	}
}

static void sdl_cleanup(TilemSdlUi *ui)
{
	sdl_free_skin(ui);

	if (ui->lcd_texture)
		SDL_DestroyTexture(ui->lcd_texture);
	if (ui->renderer)
		SDL_DestroyRenderer(ui->renderer);
	if (ui->window)
		SDL_DestroyWindow(ui->window);

	g_free(ui->lcd_pixels);
	ui->lcd_pixels = NULL;

	if (ui->lcd_palette)
		tilem_free(ui->lcd_palette);
	ui->lcd_palette = NULL;

	sdl_shutdown_ttf(ui);
}

static void sdl_init_ttf(TilemSdlUi *ui)
{
	char *font_path;

	if (TTF_WasInit())
		ui->ttf_ready = TRUE;

	if (!ui->ttf_ready) {
		if (TTF_Init() != 0) {
			g_printerr("SDL_ttf init failed: %s\n", TTF_GetError());
			return;
		}
		ui->ttf_ready = TRUE;
	}

	font_path = sdl_find_font_path();
	if (!font_path) {
		g_printerr("SDL_ttf font not found; set TILEM_SDL_FONT\n");
		return;
	}

	ui->menu_font = TTF_OpenFont(font_path, SDL_MENU_FONT_SIZE);
	if (!ui->menu_font)
		g_printerr("SDL_ttf open failed: %s\n", TTF_GetError());

	g_free(font_path);
}

static void sdl_shutdown_ttf(TilemSdlUi *ui)
{
	if (ui->menu_font) {
		TTF_CloseFont(ui->menu_font);
		ui->menu_font = NULL;
	}

	if (ui->ttf_ready && TTF_WasInit())
		TTF_Quit();
	ui->ttf_ready = FALSE;
}

int tilem_sdl_run(TilemCalcEmulator *emu, const TilemSdlOptions *opts)
{
	TilemSdlUi ui;
	double zoom_factor = 2.0;
	int base_w, base_h;
	int window_w, window_h;
	SDL_Event event;
	gboolean running = TRUE;
	int mods;

	memset(&ui, 0, sizeof(ui));
	ui.emu = emu;

	tilem_config_get("settings",
	                 "skin_disabled/b", &ui.skin_disabled,
	                 "smooth_scaling/b=1", &ui.lcd_smooth_scale,
	                 "zoom/r=2.0", &zoom_factor,
	                 NULL);

	if (opts->skinless)
		ui.skin_disabled = TRUE;
	if (opts->skinfile)
		ui.skin_disabled = FALSE;

	ui.skin_locked = opts->skinless || (opts->skinfile != NULL);
	ui.skin_override = opts->skinfile;

	if (!sdl_load_initial(&ui, opts))
		return 1;

	if (ui.emu->calc) {
		ui.lcd_width = ui.emu->calc->hw.lcdwidth;
		ui.lcd_height = ui.emu->calc->hw.lcdheight;
		sdl_keybindings_init(ui.emu, ui.emu->calc->hw.name);
		memset(ui.keypress_keycodes, 0, sizeof(ui.keypress_keycodes));
		ui.sequence_keycode = 0;
		sdl_update_skin_for_calc(&ui);
	}
	else {
		ui.lcd_width = 96;
		ui.lcd_height = 64;
		sdl_free_skin(&ui);
		sdl_set_palette(&ui);
	}

	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
		g_printerr("SDL_Init failed: %s\n", SDL_GetError());
		sdl_cleanup(&ui);
		return 1;
	}

	if (zoom_factor < 1.0)
		zoom_factor = 1.0;

	if (ui.skin) {
		base_w = ui.skin->width;
		base_h = ui.skin->height;
	}
	else {
		base_w = ui.lcd_width;
		base_h = ui.lcd_height;
	}

	window_w = base_w * zoom_factor + 0.5;
	window_h = base_h * zoom_factor + 0.5;

	SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");
	ui.window = SDL_CreateWindow("TilEm (SDL2)",
	                             SDL_WINDOWPOS_CENTERED,
	                             SDL_WINDOWPOS_CENTERED,
	                             window_w, window_h,
	                             SDL_WINDOW_RESIZABLE);
	if (!ui.window) {
		g_printerr("SDL_CreateWindow failed: %s\n", SDL_GetError());
		sdl_cleanup(&ui);
		SDL_Quit();
		return 1;
	}

	ui.renderer = SDL_CreateRenderer(ui.window, -1,
	                                 SDL_RENDERER_ACCELERATED
	                                 | SDL_RENDERER_PRESENTVSYNC);
	if (!ui.renderer) {
		ui.renderer = SDL_CreateRenderer(ui.window, -1, 0);
		if (!ui.renderer) {
			g_printerr("SDL_CreateRenderer failed: %s\n", SDL_GetError());
			sdl_cleanup(&ui);
			SDL_Quit();
			return 1;
		}
	}

	if (ui.skin && !ui.skin_texture) {
		ui.skin_texture = tilem_sdl_texture_from_pixbuf(ui.renderer,
		                                                ui.skin->raw);
		if (!ui.skin_texture)
			g_printerr("Unable to create SDL texture for skin\n");
	}

	sdl_init_ttf(&ui);

	SDL_GetWindowSize(ui.window, &ui.window_width, &ui.window_height);
	sdl_update_layout(&ui, ui.window_width, ui.window_height);

	ticables_library_init();
	tifiles_library_init();
	ticalcs_library_init();

	if (opts->reset)
		tilem_calc_emulator_reset(ui.emu);

	if (opts->full_speed)
		tilem_calc_emulator_set_limit_speed(ui.emu, FALSE);
	else if (opts->normal_speed)
		tilem_calc_emulator_set_limit_speed(ui.emu, TRUE);

	if (ui.emu->calc)
		tilem_calc_emulator_run(ui.emu);

	while (running) {
		while (SDL_PollEvent(&event)) {
			switch (event.type) {
			case SDL_QUIT:
				running = FALSE;
				break;
			case SDL_WINDOWEVENT:
				if (event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED)
					sdl_update_layout(&ui, event.window.data1,
					                  event.window.data2);
				break;
			case SDL_MOUSEBUTTONDOWN:
				if (event.button.button == SDL_BUTTON_RIGHT) {
					if (ui.menu_visible)
						sdl_menu_hide(&ui);
					else
						sdl_menu_show(&ui, event.button.x,
						              event.button.y);
					break;
				}

				if (event.button.button == SDL_BUTTON_LEFT) {
					if (ui.menu_visible) {
						int sub_idx = -1;
						int idx = -1;
						gboolean hide_menu = TRUE;

						if (ui.submenu_visible) {
							sub_idx = sdl_menu_hit_test_items(
								&ui,
								sdl_menu_macro_items,
								G_N_ELEMENTS(sdl_menu_macro_items),
								ui.submenu_x,
								ui.submenu_y,
								ui.submenu_width,
								ui.submenu_height,
								event.button.x,
								event.button.y);
							if (sub_idx >= 0) {
								sdl_handle_menu_action(
									&ui,
									sdl_menu_macro_items[sub_idx].action,
									&running);
							}
						}

						if (sub_idx >= 0) {
							sdl_menu_hide(&ui);
							break;
						}

						idx = sdl_menu_hit_test(&ui,
						                        event.button.x,
						                        event.button.y);
						if (idx >= 0) {
							if (sdl_menu_items[idx].action
							    == SDL_MENU_MACRO_MENU) {
								sdl_menu_show_macro(&ui, idx);
								ui.menu_selected = idx;
								hide_menu = FALSE;
							} else {
								sdl_handle_menu_action(
									&ui,
									sdl_menu_items[idx].action,
									&running);
							}
						}

						if (hide_menu)
							sdl_menu_hide(&ui);
						break;
					}

					int key = sdl_key_from_mouse(&ui,
					                             event.button.x,
					                             event.button.y);
					press_mouse_key(&ui, key);
				}
				break;
			case SDL_MOUSEBUTTONUP:
				if (event.button.button == SDL_BUTTON_LEFT
				    && !ui.menu_visible)
					press_mouse_key(&ui, 0);
				break;
			case SDL_MOUSEMOTION:
				if (ui.menu_visible) {
					int main_idx;
					int sub_idx = -1;
					gboolean over_submenu = FALSE;

					main_idx = sdl_menu_hit_test(&ui,
					                             event.motion.x,
					                             event.motion.y);
					if (ui.submenu_visible) {
						over_submenu = sdl_menu_contains(
							event.motion.x,
							event.motion.y,
							ui.submenu_x,
							ui.submenu_y,
							ui.submenu_width,
							ui.submenu_height);
						sub_idx = sdl_menu_hit_test_items(
							&ui,
							sdl_menu_macro_items,
							G_N_ELEMENTS(sdl_menu_macro_items),
							ui.submenu_x,
							ui.submenu_y,
							ui.submenu_width,
							ui.submenu_height,
							event.motion.x,
							event.motion.y);
					}

					if (main_idx >= 0) {
						ui.menu_selected = main_idx;
					} else if (!over_submenu) {
						ui.menu_selected = -1;
					}

					if (over_submenu) {
						if (sub_idx >= 0)
							ui.submenu_selected = sub_idx;
						else
							ui.submenu_selected = -1;
					} else {
						ui.submenu_selected = -1;
					}

					if (main_idx >= 0
					    && sdl_menu_items[main_idx].action
					    == SDL_MENU_MACRO_MENU) {
						sdl_menu_show_macro(&ui, main_idx);
					} else if (!over_submenu) {
						ui.submenu_visible = FALSE;
						ui.submenu_selected = -1;
					}
					break;
				}

				if (event.motion.state & SDL_BUTTON_LMASK) {
					int key = sdl_key_from_mouse(&ui,
					                             event.motion.x,
					                             event.motion.y);
					press_mouse_key(&ui, key);
				}
				break;
			case SDL_KEYDOWN:
				if (event.key.repeat)
					break;
				mods = event.key.keysym.mod;
				{
					SDL_Keycode sym = event.key.keysym.sym;
					if (sym >= 'A' && sym <= 'Z')
						sym = sym - 'A' + 'a';

					if (ui.menu_visible
					    && sym == SDLK_ESCAPE) {
						sdl_menu_hide(&ui);
						break;
					}
					if ((mods & (KMOD_CTRL | KMOD_GUI))
					    && (sym == SDLK_o || sym == SDLK_s)) {
						if (mods & KMOD_SHIFT) {
							if (sym == SDLK_o)
								sdl_handle_open_calc(&ui);
							else
								sdl_handle_save_calc(&ui);
						}
						else {
							if (sym == SDLK_o)
								sdl_handle_send_file(&ui);
							else
								sdl_handle_receive_file(&ui);
						}
						break;
					}
				}
				sdl_handle_keydown(&ui, &event.key);
				break;
			case SDL_KEYUP:
				sdl_handle_keyup(&ui, &event.key);
				break;
			default:
				break;
			}
		}

		sdl_render(&ui);
		SDL_Delay(SDL_FRAME_DELAY_MS);
	}

	tilem_calc_emulator_pause(ui.emu);

	ticables_library_exit();
	tifiles_library_exit();
	ticalcs_library_exit();

	sdl_cleanup(&ui);
	SDL_Quit();

	return 0;
}
