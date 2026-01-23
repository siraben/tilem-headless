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
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <gtk/gtk.h>

#include <glib.h>
#include <glib/gstdio.h>
#include <ticalcs.h>
#include <tilem.h>
#include <scancodes.h>

#include "sdlui.h"
#include "filedlg.h"
#include "files.h"
#include "gui.h"
#include "skinops.h"

#define SDL_LCD_GAMMA 2.2
#define SDL_FRAME_DELAY_MS 16
#define SDL_MENU_FONT_SCALE 2
#define SDL_MENU_PADDING 6
#define SDL_MENU_SPACING 2

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
	gboolean gtk_ready;
} TilemSdlUi;

typedef enum {
	SDL_MENU_LOAD_ROM = 0,
	SDL_MENU_LOAD_STATE,
	SDL_MENU_SAVE_STATE,
	SDL_MENU_SAVE_STATE_AS,
	SDL_MENU_RESET,
	SDL_MENU_QUIT
} TilemSdlMenuAction;

typedef struct {
	const char *label;
	TilemSdlMenuAction action;
} TilemSdlMenuItem;

static const TilemSdlMenuItem sdl_menu_items[] = {
	{ "Load ROM...", SDL_MENU_LOAD_ROM },
	{ "Load State...", SDL_MENU_LOAD_STATE },
	{ "Save State", SDL_MENU_SAVE_STATE },
	{ "Save State As...", SDL_MENU_SAVE_STATE_AS },
	{ "Reset", SDL_MENU_RESET },
	{ "Quit", SDL_MENU_QUIT }
};

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

static void sdl_ensure_gtk(TilemSdlUi *ui)
{
	int argc = 0;
	char **argv = NULL;

	if (ui->gtk_ready)
		return;

	gtk_init(&argc, &argv);
	ui->gtk_ready = TRUE;
}

static int sdl_text_width(const char *text, int scale)
{
	return (int) strlen(text) * 8 * scale;
}

static void sdl_draw_char(SDL_Renderer *renderer, int x, int y,
                          int scale, unsigned char c, SDL_Color color)
{
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

	pixel.w = scale;
	pixel.h = scale;

	for (row = 0; row < 8; row++) {
		for (col = 0; col < 8; col++) {
			if (glyph[row] & (0x80 >> col)) {
				pixel.x = x + col * scale;
				pixel.y = y + row * scale;
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

static SDL_Texture *sdl_texture_from_pixbuf(SDL_Renderer *renderer,
                                            GdkPixbuf *pixbuf)
{
	SDL_Texture *texture;
	guchar *pixels;
	byte *rgb;
	int width, height, rowstride, n_channels, y;

	width = gdk_pixbuf_get_width(pixbuf);
	height = gdk_pixbuf_get_height(pixbuf);
	rowstride = gdk_pixbuf_get_rowstride(pixbuf);
	n_channels = gdk_pixbuf_get_n_channels(pixbuf);
	pixels = gdk_pixbuf_get_pixels(pixbuf);

	texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB24,
	                            SDL_TEXTUREACCESS_STATIC, width, height);
	if (!texture)
		return NULL;

	if (n_channels == 3) {
		SDL_UpdateTexture(texture, NULL, pixels, rowstride);
		return texture;
	}

	/* Convert RGBA to RGB for SDL's RGB24 texture. */
	rgb = g_new(byte, width * height * 3);
	for (y = 0; y < height; y++) {
		const guchar *src = pixels + y * rowstride;
		byte *dst = rgb + y * width * 3;
		int x;
		for (x = 0; x < width; x++) {
			dst[0] = src[0];
			dst[1] = src[1];
			dst[2] = src[2];
			src += 4;
			dst += 3;
		}
	}

	SDL_UpdateTexture(texture, NULL, rgb, width * 3);
	g_free(rgb);
	return texture;
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
	GdkPixbuf *pixbuf;

	sdl_free_skin(ui);

	ui->skin = g_new0(SKIN_INFOS, 1);
	if (skin_load(ui->skin, filename, err)) {
		skin_unload(ui->skin);
		g_free(ui->skin);
		ui->skin = NULL;
		return FALSE;
	}

	pixbuf = ui->skin->raw;
	if (ui->renderer) {
		ui->skin_texture = sdl_texture_from_pixbuf(ui->renderer, pixbuf);
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
		base_w = gdk_pixbuf_get_width(ui->skin->raw);
		base_h = gdk_pixbuf_get_height(ui->skin->raw);
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

static int sdl_menu_item_height(void)
{
	return 8 * SDL_MENU_FONT_SCALE + SDL_MENU_PADDING * 2;
}

static void sdl_menu_show(TilemSdlUi *ui, int x, int y)
{
	size_t i;
	int maxw = 0;
	int item_h = sdl_menu_item_height();
	int text_w;
	int menu_w;
	int menu_h;

	for (i = 0; i < G_N_ELEMENTS(sdl_menu_items); i++) {
		text_w = sdl_text_width(sdl_menu_items[i].label,
		                        SDL_MENU_FONT_SCALE);
		if (text_w > maxw)
			maxw = text_w;
	}

	menu_w = maxw + SDL_MENU_PADDING * 2;
	menu_h = (int) G_N_ELEMENTS(sdl_menu_items) * item_h
		+ SDL_MENU_SPACING * 2;

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
}

static void sdl_menu_hide(TilemSdlUi *ui)
{
	ui->menu_visible = FALSE;
	ui->menu_selected = -1;
}

static int sdl_menu_hit_test(TilemSdlUi *ui, int x, int y)
{
	int rel_y;
	int item_h;
	int index;

	if (!ui->menu_visible)
		return -1;

	if (x < ui->menu_x || y < ui->menu_y)
		return -1;
	if (x >= ui->menu_x + ui->menu_width
	    || y >= ui->menu_y + ui->menu_height)
		return -1;

	item_h = sdl_menu_item_height();
	rel_y = y - ui->menu_y - SDL_MENU_SPACING;
	if (rel_y < 0)
		return -1;

	index = rel_y / item_h;
	if (index < 0 || index >= (int) G_N_ELEMENTS(sdl_menu_items))
		return -1;

	return index;
}

static void sdl_render_menu(TilemSdlUi *ui)
{
	SDL_Rect menu_rect;
	SDL_Rect item_rect;
	size_t i;
	int item_h;
	SDL_Color text_color = { 230, 230, 230, 255 };
	SDL_Color highlight_color = { 60, 110, 170, 255 };
	SDL_Color bg_color = { 40, 40, 40, 240 };
	SDL_Color border_color = { 90, 90, 90, 255 };

	if (!ui->menu_visible)
		return;

	item_h = sdl_menu_item_height();
	menu_rect.x = ui->menu_x;
	menu_rect.y = ui->menu_y;
	menu_rect.w = ui->menu_width;
	menu_rect.h = ui->menu_height;

	SDL_SetRenderDrawColor(ui->renderer, bg_color.r, bg_color.g,
	                       bg_color.b, bg_color.a);
	SDL_RenderFillRect(ui->renderer, &menu_rect);

	SDL_SetRenderDrawColor(ui->renderer, border_color.r, border_color.g,
	                       border_color.b, border_color.a);
	SDL_RenderDrawRect(ui->renderer, &menu_rect);

	for (i = 0; i < G_N_ELEMENTS(sdl_menu_items); i++) {
		item_rect.x = ui->menu_x;
		item_rect.y = ui->menu_y + SDL_MENU_SPACING
			+ (int) i * item_h;
		item_rect.w = ui->menu_width;
		item_rect.h = item_h;

		if ((int) i == ui->menu_selected) {
			SDL_SetRenderDrawColor(ui->renderer, highlight_color.r,
			                       highlight_color.g,
			                       highlight_color.b,
			                       highlight_color.a);
			SDL_RenderFillRect(ui->renderer, &item_rect);
		}

		sdl_draw_text(ui->renderer,
		              item_rect.x + SDL_MENU_PADDING,
		              item_rect.y + SDL_MENU_PADDING,
		              SDL_MENU_FONT_SCALE,
		              sdl_menu_items[i].label, text_color);
	}
}

static void sdl_render_lcd(TilemSdlUi *ui)
{
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

	sdl_ensure_gtk(ui);

	if (ui->emu->rom_file_name)
		dir = g_path_get_dirname(ui->emu->rom_file_name);
	else
		dir = g_get_current_dir();

	filename = prompt_open_file("Open Calculator",
	                            NULL,
	                            dir,
	                            "ROM files", "*.rom;*.clc;*.bin",
	                            "All files", "*",
	                            NULL);
	g_free(dir);
	return filename;
}

static char *sdl_pick_state_file(TilemSdlUi *ui, const char *title)
{
	char *dir;
	char *filename;

	sdl_ensure_gtk(ui);

	if (ui->emu->state_file_name)
		dir = g_path_get_dirname(ui->emu->state_file_name);
	else if (ui->emu->rom_file_name)
		dir = g_path_get_dirname(ui->emu->rom_file_name);
	else
		dir = g_get_current_dir();

	filename = prompt_open_file(title, NULL, dir,
	                            "State files", "*.sav",
	                            "All files", "*",
	                            NULL);
	g_free(dir);
	return filename;
}

static char *sdl_pick_state_save_file(TilemSdlUi *ui)
{
	char *dir;
	char *suggest_name = NULL;
	char *filename;

	sdl_ensure_gtk(ui);

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

	filename = prompt_save_file("Save State As",
	                            NULL,
	                            suggest_name,
	                            dir,
	                            "State files", "*.sav",
	                            "All files", "*",
	                            NULL);

	g_free(dir);
	g_free(suggest_name);
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
			return FALSE;

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
	g_free(rom);
	g_free(state);
}

static void sdl_handle_load_state(TilemSdlUi *ui)
{
	GError *err = NULL;
	char *state;

	if (!ui->emu->rom_file_name) {
		sdl_show_message(ui, "Load State",
		                 "No ROM loaded yet.");
		return;
	}

	state = sdl_pick_state_file(ui, "Open State");
	if (!state)
		return;

	if (!tilem_calc_emulator_load_state(ui->emu, ui->emu->rom_file_name,
	                                    state, 0, &err)) {
		sdl_report_error("Unable to load calculator state", err);
		g_free(state);
		return;
	}

	sdl_update_after_load(ui);
	g_free(state);
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

static void sdl_handle_menu_action(TilemSdlUi *ui, TilemSdlMenuAction action,
                                   gboolean *running)
{
	switch (action) {
	case SDL_MENU_LOAD_ROM:
		sdl_handle_load_rom(ui);
		break;
	case SDL_MENU_LOAD_STATE:
		sdl_handle_load_state(ui);
		break;
	case SDL_MENU_SAVE_STATE:
		sdl_handle_save_state(ui);
		break;
	case SDL_MENU_SAVE_STATE_AS:
		sdl_handle_save_state_as(ui);
		break;
	case SDL_MENU_RESET:
		tilem_calc_emulator_reset(ui->emu);
		break;
	case SDL_MENU_QUIT:
		*running = FALSE;
		break;
	default:
		break;
	}
}

static void sdl_handle_load(TilemSdlUi *ui)
{
	sdl_handle_load_rom(ui);
}

static void sdl_handle_save(TilemSdlUi *ui)
{
	sdl_handle_save_state(ui);
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

	ui.lcd_width = ui.emu->calc->hw.lcdwidth;
	ui.lcd_height = ui.emu->calc->hw.lcdheight;
	sdl_update_skin_for_calc(&ui);

	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
		g_printerr("SDL_Init failed: %s\n", SDL_GetError());
		sdl_cleanup(&ui);
		return 1;
	}

	if (zoom_factor < 1.0)
		zoom_factor = 1.0;

	if (ui.skin) {
		base_w = gdk_pixbuf_get_width(ui.skin->raw);
		base_h = gdk_pixbuf_get_height(ui.skin->raw);
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
		ui.skin_texture = sdl_texture_from_pixbuf(ui.renderer,
		                                          ui.skin->raw);
		if (!ui.skin_texture)
			g_printerr("Unable to create SDL texture for skin\n");
	}

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
						int idx = sdl_menu_hit_test(&ui,
						                            event.button.x,
						                            event.button.y);
						if (idx >= 0) {
							sdl_handle_menu_action(
								&ui,
								sdl_menu_items[idx].action,
								&running);
						}
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
					ui.menu_selected = sdl_menu_hit_test(&ui,
					                                     event.motion.x,
					                                     event.motion.y);
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
				mods = SDL_GetModState();
				if (event.key.keysym.sym == SDLK_ESCAPE) {
					running = FALSE;
				}
				else if ((mods & (KMOD_CTRL | KMOD_GUI))
				         && event.key.keysym.sym == SDLK_o) {
					sdl_handle_load(&ui);
				}
				else if ((mods & (KMOD_CTRL | KMOD_GUI))
				         && event.key.keysym.sym == SDLK_s) {
					sdl_handle_save(&ui);
				}
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
