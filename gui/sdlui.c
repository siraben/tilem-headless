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
#include <SDL_image.h>
#include <SDL_ttf.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <glib.h>
#include <glib-object.h>
#include <glib/gstdio.h>
#include <ticalcs.h>
#include <tilem.h>
#include <scancodes.h>

#include "sdlui.h"
#include "files.h"
#include "sdlskin.h"
#include "sdlicons.h"
#include "sdlscreenshot.h"
#include "sdldebugger.h"
#include "ti81prg.h"
#include "varentry.h"
#include "emucore.h"

void tilem_config_get(const char *group, const char *option, ...);
void tilem_config_set(const char *group, const char *option, ...);
void tilem_link_send_file(TilemCalcEmulator *emu, const char *filename,
                          int slot, gboolean first, gboolean last);
void tilem_link_receive_file(TilemCalcEmulator *emu,
                             const TilemVarEntry *varentry,
                             const char* destination);
void tilem_link_receive_group(TilemCalcEmulator *emu,
                              GSList *entries,
                              const char *destination);
void tilem_link_receive_all(TilemCalcEmulator *emu,
                            const char *destination);
void tilem_link_get_dirlist_with_callback(TilemCalcEmulator *emu,
                                          gboolean no_gui,
                                          void (*callback)(TilemCalcEmulator *emu,
                                                           GSList *list,
                                                           const char *error_message,
                                                           gpointer data),
                                          gpointer data);
typedef struct _TilemAnimFrame TilemAnimFrame;
typedef struct {
	guint32 pixel;
	guint16 red;
	guint16 green;
	guint16 blue;
} GdkColor;
void tilem_animation_set_size(TilemAnimation *anim, int width, int height);
void tilem_animation_set_colors(TilemAnimation *anim,
                                const GdkColor *foreground,
                                const GdkColor *background);
void tilem_animation_set_speed(TilemAnimation *anim, gdouble factor);
gdouble tilem_animation_get_speed(TilemAnimation *anim);
TilemAnimFrame *tilem_animation_next_frame(TilemAnimation *anim,
                                           TilemAnimFrame *frm);
int tilem_anim_frame_get_duration(TilemAnimFrame *frm);
void tilem_animation_get_indexed_image(TilemAnimation *anim,
                                       TilemAnimFrame *frm,
                                       byte **buffer,
                                       int *width, int *height);
gboolean tilem_animation_save(TilemAnimation *anim,
                              const char *fname, const char *type,
                              char **option_keys, char **option_values,
                              GError **err);
int name_to_model(const char *name);
int get_calc_model(TilemCalc *calc);
char *utf8_to_filename(const char *utf8str);
char *get_default_filename(const TilemVarEntry *tve);

#define SDL_LCD_GAMMA 2.2
#define SDL_FRAME_DELAY_MS 16
#define SDL_MENU_FONT_SCALE 2
#define SDL_MENU_PADDING 6
#define SDL_MENU_SPACING 2
#define SDL_MENU_FONT_SIZE 16
#define SDL_MENU_ICON_GAP 6

typedef struct {
	gboolean visible;
	char **filenames;
	int nfiles;
	int *slots;
	TI81ProgInfo info[TI81_SLOT_MAX + 1];
	int selected;
	int top;
} TilemSdlSlotDialog;

typedef struct {
	gboolean visible;
	gboolean refresh_pending;
	gboolean is_81;
	gboolean use_group;
	int selected;
	int top;
	int hover;
	GPtrArray *entries;
	GArray *selected_flags;
} TilemSdlReceiveDialog;

typedef enum {
	SDL_SS_INPUT_NONE,
	SDL_SS_INPUT_WIDTH,
	SDL_SS_INPUT_HEIGHT,
	SDL_SS_INPUT_SPEED,
	SDL_SS_INPUT_FG_R,
	SDL_SS_INPUT_FG_G,
	SDL_SS_INPUT_FG_B,
	SDL_SS_INPUT_BG_R,
	SDL_SS_INPUT_BG_G,
	SDL_SS_INPUT_BG_B
} TilemSdlScreenshotInput;

typedef enum {
	SDL_SS_ITEM_NONE = -1,
	SDL_SS_ITEM_GRAB,
	SDL_SS_ITEM_RECORD,
	SDL_SS_ITEM_STOP,
	SDL_SS_ITEM_GRAYSCALE,
	SDL_SS_ITEM_SIZE,
	SDL_SS_ITEM_WIDTH,
	SDL_SS_ITEM_HEIGHT,
	SDL_SS_ITEM_SPEED,
	SDL_SS_ITEM_FG_R,
	SDL_SS_ITEM_FG_G,
	SDL_SS_ITEM_FG_B,
	SDL_SS_ITEM_BG_R,
	SDL_SS_ITEM_BG_G,
	SDL_SS_ITEM_BG_B,
	SDL_SS_ITEM_SAVE,
	SDL_SS_ITEM_CANCEL
} TilemSdlScreenshotItem;

typedef struct {
	gboolean visible;
	gboolean size_menu_open;
	int size_menu_hover;
	int hover;
	TilemSdlScreenshotInput input_mode;
	char input_buf[32];
	int input_len;
	int selected_size;
	int width;
	int height;
	double speed;
	gboolean grayscale;
	int fg_r;
	int fg_g;
	int fg_b;
	int bg_r;
	int bg_g;
	int bg_b;
	TilemAnimation *current_anim;
	gboolean current_anim_grayscale;
	TilemAnimFrame *preview_frame;
	Uint32 preview_frame_start;
	SDL_Texture *preview_texture;
	int preview_width;
	int preview_height;
	dword *preview_palette;
	gboolean preview_dirty;
} TilemSdlScreenshotDialog;

typedef struct {
	SDL_Rect panel;
	SDL_Rect preview_rect;
	SDL_Rect grab_rect;
	SDL_Rect record_rect;
	SDL_Rect stop_rect;
	SDL_Rect grayscale_rect;
	SDL_Rect size_rect;
	SDL_Rect width_rect;
	SDL_Rect height_rect;
	SDL_Rect speed_rect;
	SDL_Rect fg_label_rect;
	SDL_Rect bg_label_rect;
	SDL_Rect fg_r_rect;
	SDL_Rect fg_g_rect;
	SDL_Rect fg_b_rect;
	SDL_Rect fg_color_rect;
	SDL_Rect bg_r_rect;
	SDL_Rect bg_g_rect;
	SDL_Rect bg_b_rect;
	SDL_Rect bg_color_rect;
	SDL_Rect save_rect;
	SDL_Rect cancel_rect;
	int text_h;
	int row_h;
	int padding;
} TilemSdlScreenshotLayout;

typedef struct {
	TilemCalcEmulator *emu;
	SDL_Window *window;
	SDL_Renderer *renderer;
	SDL_Texture *skin_texture;
	SDL_Texture *lcd_texture;
	TilemSdlSkin *skin;
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
	gboolean prefs_visible;
	int prefs_hover;
	TilemSdlSlotDialog slot_dialog;
	TilemSdlReceiveDialog receive_dialog;
	TilemSdlScreenshotDialog screenshot_dialog;
	GPtrArray *drop_files;
	gboolean drop_active;
	TTF_Font *menu_font;
	gboolean ttf_ready;
	gboolean image_ready;
	int keypress_keycodes[64];
	int sequence_keycode;
	TilemSdlDebugger *debugger;
	TilemSdlIcons *icons;
} TilemSdlUi;

typedef enum {
	SDL_MENU_NONE = 0,
	SDL_MENU_SEND_FILE,
	SDL_MENU_RECEIVE_FILE,
	SDL_MENU_OPEN_CALC,
	SDL_MENU_SAVE_CALC,
	SDL_MENU_REVERT_CALC,
	SDL_MENU_RESET,
	SDL_MENU_DEBUGGER,
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

typedef enum {
	SDL_PREF_ITEM_NONE = -1,
	SDL_PREF_ITEM_SPEED_LIMIT,
	SDL_PREF_ITEM_SPEED_FAST,
	SDL_PREF_ITEM_GRAYSCALE,
	SDL_PREF_ITEM_SMOOTH,
	SDL_PREF_ITEM_USE_SKIN,
	SDL_PREF_ITEM_SKIN_CHOOSE,
	SDL_PREF_ITEM_CLOSE
} TilemSdlPrefItem;

typedef struct {
	const char *label;
	TilemSdlMenuAction action;
	gboolean separator;
	TilemSdlMenuIconId icon;
} TilemSdlMenuItem;

typedef struct {
	SDL_Rect panel;
	SDL_Rect speed_limit_rect;
	SDL_Rect speed_fast_rect;
	SDL_Rect grayscale_rect;
	SDL_Rect smooth_rect;
	SDL_Rect skin_toggle_rect;
	SDL_Rect skin_label_rect;
	SDL_Rect skin_value_rect;
	SDL_Rect skin_button_rect;
	SDL_Rect close_rect;
	int text_h;
	int row_h;
	int indicator;
	int padding;
	int title_y;
	int speed_header_y;
	int display_header_y;
} TilemSdlPrefsLayout;

typedef struct {
	SDL_Rect panel;
	SDL_Rect list_rect;
	SDL_Rect send_rect;
	SDL_Rect cancel_rect;
	int row_h;
	int text_h;
	int padding;
} TilemSdlSlotLayout;

typedef struct {
	SDL_Rect panel;
	SDL_Rect list_rect;
	SDL_Rect refresh_rect;
	SDL_Rect save_rect;
	SDL_Rect cancel_rect;
	SDL_Rect mode_label_rect;
	SDL_Rect mode_sep_rect;
	SDL_Rect mode_group_rect;
	int row_h;
	int header_h;
	int text_h;
	int padding;
} TilemSdlReceiveLayout;

static const TilemSdlMenuItem sdl_menu_items[] = {
	{ "Send File...", SDL_MENU_SEND_FILE, FALSE,
	  TILEM_SDL_MENU_ICON_OPEN },
	{ "Receive File...", SDL_MENU_RECEIVE_FILE, FALSE,
	  TILEM_SDL_MENU_ICON_SAVE_AS },
	{ NULL, SDL_MENU_NONE, TRUE, TILEM_SDL_MENU_ICON_NONE },
	{ "Open Calculator...", SDL_MENU_OPEN_CALC, FALSE,
	  TILEM_SDL_MENU_ICON_OPEN },
	{ "Save Calculator", SDL_MENU_SAVE_CALC, FALSE,
	  TILEM_SDL_MENU_ICON_SAVE },
	{ "Revert Calculator State", SDL_MENU_REVERT_CALC, FALSE,
	  TILEM_SDL_MENU_ICON_REVERT },
	{ "Reset Calculator", SDL_MENU_RESET, FALSE,
	  TILEM_SDL_MENU_ICON_CLEAR },
	{ NULL, SDL_MENU_NONE, TRUE, TILEM_SDL_MENU_ICON_NONE },
	{ "Debugger", SDL_MENU_DEBUGGER, FALSE,
	  TILEM_SDL_MENU_ICON_NONE },
	{ "Macro >", SDL_MENU_MACRO_MENU, FALSE,
	  TILEM_SDL_MENU_ICON_NONE },
	{ "Screenshot...", SDL_MENU_SCREENSHOT, FALSE,
	  TILEM_SDL_MENU_ICON_NONE },
	{ "Quick Screenshot", SDL_MENU_QUICK_SCREENSHOT, FALSE,
	  TILEM_SDL_MENU_ICON_NONE },
	{ NULL, SDL_MENU_NONE, TRUE, TILEM_SDL_MENU_ICON_NONE },
	{ "Preferences", SDL_MENU_PREFERENCES, FALSE,
	  TILEM_SDL_MENU_ICON_PREFERENCES },
	{ NULL, SDL_MENU_NONE, TRUE, TILEM_SDL_MENU_ICON_NONE },
	{ "About", SDL_MENU_ABOUT, FALSE,
	  TILEM_SDL_MENU_ICON_ABOUT },
	{ "Quit", SDL_MENU_QUIT, FALSE,
	  TILEM_SDL_MENU_ICON_QUIT }
};

static const TilemSdlMenuItem sdl_menu_macro_items[] = {
	{ "Begin Recording", SDL_MENU_MACRO_BEGIN, FALSE,
	  TILEM_SDL_MENU_ICON_RECORD },
	{ "End Recording", SDL_MENU_MACRO_END, FALSE,
	  TILEM_SDL_MENU_ICON_STOP },
	{ "Play Macro", SDL_MENU_MACRO_PLAY, FALSE,
	  TILEM_SDL_MENU_ICON_PLAY },
	{ NULL, SDL_MENU_NONE, TRUE, TILEM_SDL_MENU_ICON_NONE },
	{ "Open Macro...", SDL_MENU_MACRO_OPEN, FALSE,
	  TILEM_SDL_MENU_ICON_OPEN },
	{ "Save Macro...", SDL_MENU_MACRO_SAVE, FALSE,
	  TILEM_SDL_MENU_ICON_SAVE_AS }
};

static void sdl_update_skin_for_calc(TilemSdlUi *ui);
static gboolean sdl_is_wide_screen(TilemSdlUi *ui);
static char *sdl_find_free_filename(const char *folder,
                                    const char *basename,
                                    const char *extension);
static char *sdl_guess_image_format(const char *filename,
                                    const char *default_format);
static char *sdl_ensure_extension(const char *filename,
                                  const char *format);

#define SDL_SCREENSHOT_DEFAULT_WIDTH_96 192
#define SDL_SCREENSHOT_DEFAULT_HEIGHT_96 128
#define SDL_SCREENSHOT_DEFAULT_WIDTH_128 256
#define SDL_SCREENSHOT_DEFAULT_HEIGHT_128 128
#define SDL_SCREENSHOT_DEFAULT_FORMAT "png"
#define SDL_SCREENSHOT_MAX_WIDTH 750
#define SDL_SCREENSHOT_MAX_HEIGHT 500

typedef struct {
	int width;
	int height;
} TilemSdlScreenshotSize;

static const TilemSdlScreenshotSize sdl_screenshot_sizes_normal[] = {
	{ 96, 64 },
	{ 192, 128 },
	{ 288, 192 }
};

static const TilemSdlScreenshotSize sdl_screenshot_sizes_wide[] = {
	{ 128, 64 },
	{ 128, 77 },
	{ 214, 128 },
	{ 256, 128 },
	{ 256, 153 },
	{ 321, 192 },
	{ 384, 192 }
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

static void sdl_init_image(TilemSdlUi *ui);
static void sdl_shutdown_image(TilemSdlUi *ui);
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

static char **sdl_split_output_lines(const char *output)
{
	GPtrArray *paths;
	char **lines;
	int i;

	if (!output || !*output)
		return NULL;

	paths = g_ptr_array_new_with_free_func(g_free);
	lines = g_strsplit(output, "\n", -1);
	for (i = 0; lines && lines[i]; i++) {
		if (!lines[i][0])
			continue;
		g_ptr_array_add(paths, g_strdup(lines[i]));
	}
	g_strfreev(lines);

	if (paths->len == 0) {
		g_ptr_array_free(paths, TRUE);
		return NULL;
	}

	g_ptr_array_add(paths, NULL);
	return (char **) g_ptr_array_free(paths, FALSE);
}

static char **sdl_native_file_dialog_multi(const char *title,
                                           const char *suggest_dir,
                                           gboolean *used_native)
{
	if (used_native)
		*used_native = FALSE;

#ifdef __APPLE__
	{
		char *escaped_title = sdl_applescript_escape(title);
		char *escaped_dir = sdl_applescript_escape(suggest_dir);
		GString *script = g_string_new(NULL);
		char *argv[4];
		char *result;
		char **paths;

		g_string_append_printf(script,
		                       "set out to \"\"\n"
		                       "set files to choose file with prompt \"%s\"",
		                       escaped_title);
		if (escaped_dir && *escaped_dir) {
			g_string_append_printf(script,
			                       " default location POSIX file \"%s\"",
			                       escaped_dir);
		}
		g_string_append(script,
		                " with multiple selections allowed\n"
		                "repeat with f in files\n"
		                "set out to out & POSIX path of f & \"\\n\"\n"
		                "end repeat\n"
		                "return out");

		argv[0] = (char *)"osascript";
		argv[1] = (char *)"-e";
		argv[2] = script->str;
		argv[3] = NULL;

		if (used_native)
			*used_native = TRUE;
		result = sdl_spawn_capture(argv);
		paths = sdl_split_output_lines(result);

		g_free(result);
		g_string_free(script, TRUE);
		g_free(escaped_title);
		g_free(escaped_dir);
		return paths;
	}
#elif defined(__linux__)
	{
		char *prog = g_find_program_in_path("zenity");
		char *result = NULL;
		char *filename = NULL;
		char **paths = NULL;

		if (prog) {
			GPtrArray *argv = g_ptr_array_new();

			g_ptr_array_add(argv, prog);
			g_ptr_array_add(argv, (char *)"--file-selection");
			g_ptr_array_add(argv, (char *)"--multiple");
			g_ptr_array_add(argv, (char *)"--separator");
			g_ptr_array_add(argv, (char *)"\n");
			g_ptr_array_add(argv, (char *)"--title");
			g_ptr_array_add(argv, (char *)title);
			if (suggest_dir && *suggest_dir) {
				filename = g_strconcat(suggest_dir,
				                       G_DIR_SEPARATOR_S,
				                       NULL);
				g_ptr_array_add(argv, (char *)"--filename");
				g_ptr_array_add(argv, filename);
			}
			g_ptr_array_add(argv, NULL);

			if (used_native)
				*used_native = TRUE;
			result = sdl_spawn_capture((char **) argv->pdata);
			paths = sdl_split_output_lines(result);

			g_free(result);
			g_ptr_array_free(argv, TRUE);
			g_free(filename);
			g_free(prog);
			return paths;
		}

		prog = g_find_program_in_path("kdialog");
		if (prog) {
			GPtrArray *argv = g_ptr_array_new();
			char *path = NULL;

			g_ptr_array_add(argv, prog);
			g_ptr_array_add(argv, (char *)"--getopenfilename");
			if (suggest_dir && *suggest_dir) {
				path = g_strdup(suggest_dir);
				g_ptr_array_add(argv, path);
			}
			g_ptr_array_add(argv, (char *)"--multiple");
			g_ptr_array_add(argv, (char *)"--separate-output");
			g_ptr_array_add(argv, (char *)"--title");
			g_ptr_array_add(argv, (char *)title);
			g_ptr_array_add(argv, NULL);

			if (used_native)
				*used_native = TRUE;
			result = sdl_spawn_capture((char **) argv->pdata);
			paths = sdl_split_output_lines(result);

			g_free(result);
			g_ptr_array_free(argv, TRUE);
			g_free(path);
			g_free(prog);
			return paths;
		}
	}
#endif

	{
		char *single = sdl_native_file_dialog(title, suggest_dir,
		                                      NULL, FALSE,
		                                      used_native);
		char **paths;

		if (!single)
			return NULL;

		paths = g_new0(char *, 2);
		paths[0] = single;
		paths[1] = NULL;
		return paths;
	}
}

static char *sdl_native_folder_dialog(const char *title,
                                      const char *suggest_dir,
                                      gboolean *used_native)
{
	if (used_native)
		*used_native = FALSE;

#ifdef __APPLE__
	{
		char *escaped_title = sdl_applescript_escape(title);
		char *escaped_dir = sdl_applescript_escape(suggest_dir);
		GString *cmd = g_string_new(NULL);
		char *script;
		char *argv[4];
		char *result;

		g_string_append_printf(cmd,
		                       "choose folder with prompt \"%s\"",
		                       escaped_title);
		if (escaped_dir && *escaped_dir) {
			g_string_append_printf(cmd,
			                       " default location POSIX file \"%s\"",
			                       escaped_dir);
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
			g_ptr_array_add(argv, (char *)"--directory");
			g_ptr_array_add(argv, (char *)"--title");
			g_ptr_array_add(argv, (char *)title);
			if (suggest_dir && *suggest_dir) {
				filename = g_strconcat(suggest_dir,
				                       G_DIR_SEPARATOR_S,
				                       NULL);
				g_ptr_array_add(argv, (char *)"--filename");
				g_ptr_array_add(argv, filename);
			}
			g_ptr_array_add(argv, NULL);

			if (used_native)
				*used_native = TRUE;
			result = sdl_spawn_capture((char **) argv->pdata);

			g_ptr_array_free(argv, TRUE);
			g_free(filename);
			g_free(prog);
			return result;
		}

		prog = g_find_program_in_path("kdialog");
		if (prog) {
			char *argv[6];
			char *path = NULL;
			int i = 0;

			argv[i++] = prog;
			argv[i++] = (char *)"--getexistingdirectory";
			if (suggest_dir && *suggest_dir) {
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

static char *sdl_canonicalize_filename(const char *name)
{
#ifdef G_OS_WIN32
	static const char delim[] = "/\\";
#else
	static const char delim[] = G_DIR_SEPARATOR_S;
#endif
	char *result, **parts, *p;
	int i;

	if (name == NULL || g_path_is_absolute(name))
		return g_strdup(name);

	result = g_get_current_dir();
	parts = g_strsplit_set(name, delim, -1);
	for (i = 0; parts[i]; i++) {
		if (!strcmp(parts[i], "..")) {
			p = g_path_get_dirname(result);
			g_free(result);
			result = p;
		}
		else if (strcmp(parts[i], ".")
		         && strcmp(parts[i], "")) {
			p = g_build_filename(result, parts[i], NULL);
			g_free(result);
			result = p;
		}
	}
	g_strfreev(parts);
	return result;
}

static gboolean sdl_file_names_equal(const char *a, const char *b)
{
	char *ca, *cb;
	gboolean status;

	if (a == NULL && b == NULL)
		return TRUE;
	else if (a == NULL || b == NULL)
		return FALSE;

	ca = sdl_canonicalize_filename(a);
	cb = sdl_canonicalize_filename(b);
	status = !strcmp(ca, cb);
	g_free(ca);
	g_free(cb);
	return status;
}

static void sdl_save_skin_name(TilemSdlUi *ui)
{
	const char *model;
	char *base, *shared;

	if (!ui || !ui->emu || !ui->emu->calc)
		return;
	if (!ui->skin_file_name || !ui->skin)
		return;

	model = ui->emu->calc->hw.name;
	base = g_path_get_basename(ui->skin_file_name);
	shared = get_shared_file_path("skins", base, NULL);

	if (sdl_file_names_equal(shared, ui->skin_file_name))
		tilem_config_set(model, "skin/f", base, NULL);
	else
		tilem_config_set(model, "skin/f", ui->skin_file_name, NULL);

	g_free(base);
	g_free(shared);
}

static void sdl_free_skin(TilemSdlUi *ui)
{
	if (ui->skin_texture)
		SDL_DestroyTexture(ui->skin_texture);
	ui->skin_texture = NULL;

	if (ui->skin)
		tilem_sdl_skin_free(ui->skin);
	ui->skin = NULL;

	g_free(ui->skin_file_name);
	ui->skin_file_name = NULL;
}

static gboolean sdl_load_skin(TilemSdlUi *ui, const char *filename,
                              GError **err)
{
	sdl_free_skin(ui);

	if (!ui->image_ready) {
		g_set_error(err, G_FILE_ERROR, G_FILE_ERROR_FAILED,
		            "SDL_image is not available");
		return FALSE;
	}

	ui->skin = tilem_sdl_skin_load(filename, err);
	if (!ui->skin)
		return FALSE;

	if (ui->renderer) {
		ui->skin_texture = SDL_CreateTextureFromSurface(
			ui->renderer, ui->skin->surface);
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
	int font_h = ui->menu_font
		? TTF_FontHeight(ui->menu_font)
		: 8 * SDL_MENU_FONT_SCALE;
	int icon_h = 0;

	if (ui->icons) {
		int i;
		for (i = 0; i < TILEM_SDL_MENU_ICON_COUNT; i++) {
			if (ui->icons->menu[i].texture
			    && ui->icons->menu[i].height > icon_h)
				icon_h = ui->icons->menu[i].height;
		}
	}

	if (icon_h > font_h)
		font_h = icon_h;

	return font_h + SDL_MENU_PADDING * 2;
}

static const TilemSdlIcon *sdl_menu_get_icon(TilemSdlUi *ui,
                                             TilemSdlMenuIconId icon_id)
{
	if (!ui || !ui->icons)
		return NULL;
	if (icon_id <= TILEM_SDL_MENU_ICON_NONE
	    || icon_id >= TILEM_SDL_MENU_ICON_COUNT)
		return NULL;
	if (!ui->icons->menu[icon_id].texture)
		return NULL;
	return &ui->icons->menu[icon_id];
}

static int sdl_menu_icon_column_width(TilemSdlUi *ui,
                                      const TilemSdlMenuItem *items,
                                      size_t n_items)
{
	size_t i;
	int maxw = 0;

	if (!ui || !items)
		return 0;

	for (i = 0; i < n_items; i++) {
		const TilemSdlIcon *icon;

		if (items[i].separator)
			continue;
		icon = sdl_menu_get_icon(ui, items[i].icon);
		if (icon && icon->width > maxw)
			maxw = icon->width;
	}

	if (maxw > 0)
		return maxw + SDL_MENU_ICON_GAP;
	return 0;
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
	int icon_w = sdl_menu_icon_column_width(ui, items, n_items);
	int text_w;

	for (i = 0; i < n_items; i++) {
		if (items[i].separator)
			continue;
		text_w = sdl_menu_text_width(ui, items[i].label);
		if (text_w > maxw)
			maxw = text_w;
	}

	if (out_w)
		*out_w = maxw + icon_w + SDL_MENU_PADDING * 2;
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
	int icon_col_w;
	int icon_area_w;
	int text_h;
	SDL_Color text_color = { 230, 230, 230, 255 };
	SDL_Color highlight_color = { 60, 110, 170, 255 };
	SDL_Color bg_color = { 40, 40, 40, 240 };
	SDL_Color border_color = { 90, 90, 90, 255 };

	item_h = sdl_menu_item_height(ui);
	icon_col_w = sdl_menu_icon_column_width(ui, items, n_items);
	icon_area_w = (icon_col_w > 0) ? (icon_col_w - SDL_MENU_ICON_GAP) : 0;
	text_h = ui->menu_font ? TTF_FontHeight(ui->menu_font)
	                       : 8 * SDL_MENU_FONT_SCALE;
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

		if (icon_area_w > 0) {
			const TilemSdlIcon *icon =
				sdl_menu_get_icon(ui, items[i].icon);
			if (icon) {
				SDL_Rect dst;

				dst.x = item_rect.x + SDL_MENU_PADDING
				        + (icon_area_w - icon->width) / 2;
				dst.y = item_rect.y
				        + (item_rect.h - icon->height) / 2;
				dst.w = icon->width;
				dst.h = icon->height;
				SDL_RenderCopy(ui->renderer, icon->texture,
				               NULL, &dst);
			}
		}

		sdl_draw_text_menu(ui,
		                   item_rect.x + SDL_MENU_PADDING
		                       + icon_col_w,
		                   item_rect.y + (item_rect.h - text_h) / 2,
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

static int sdl_pref_text_height(TilemSdlUi *ui)
{
	return ui->menu_font ? TTF_FontHeight(ui->menu_font)
	                     : 8 * SDL_MENU_FONT_SCALE;
}

static gboolean sdl_point_in_rect(int x, int y, const SDL_Rect *rect)
{
	if (!rect)
		return FALSE;
	if (x < rect->x || y < rect->y)
		return FALSE;
	if (x >= rect->x + rect->w || y >= rect->y + rect->h)
		return FALSE;
	return TRUE;
}

static char *sdl_pref_trim_text(TilemSdlUi *ui, const char *text,
                                int max_width)
{
	const char *start;
	int ellipsis_w;

	if (!text)
		return g_strdup("");

	if (sdl_menu_text_width(ui, text) <= max_width)
		return g_strdup(text);

	ellipsis_w = sdl_menu_text_width(ui, "...");
	if (ellipsis_w >= max_width)
		return g_strdup("...");

	start = text;
	while (*start
	       && sdl_menu_text_width(ui, start) + ellipsis_w > max_width)
		start++;

	return g_strdup_printf("...%s", start);
}

static void sdl_draw_filled_circle(SDL_Renderer *renderer, int cx, int cy,
                                   int radius, SDL_Color color)
{
	int x, y;

	SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
	for (y = -radius; y <= radius; y++) {
		for (x = -radius; x <= radius; x++) {
			if (x * x + y * y <= radius * radius)
				SDL_RenderDrawPoint(renderer, cx + x, cy + y);
		}
	}
}

static void sdl_draw_checkbox(SDL_Renderer *renderer, SDL_Rect rect,
                              gboolean checked,
                              SDL_Color border, SDL_Color fill,
                              SDL_Color mark)
{
	SDL_Rect inner;

	SDL_SetRenderDrawColor(renderer, fill.r, fill.g, fill.b, fill.a);
	SDL_RenderFillRect(renderer, &rect);
	SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b,
	                       border.a);
	SDL_RenderDrawRect(renderer, &rect);

	if (!checked)
		return;

	inner.x = rect.x + 3;
	inner.y = rect.y + 3;
	inner.w = MAX(rect.w - 6, 1);
	inner.h = MAX(rect.h - 6, 1);
	SDL_SetRenderDrawColor(renderer, mark.r, mark.g, mark.b, mark.a);
	SDL_RenderFillRect(renderer, &inner);
}

static void sdl_draw_radio(SDL_Renderer *renderer, SDL_Rect rect,
                           gboolean checked,
                           SDL_Color border, SDL_Color fill,
                           SDL_Color mark)
{
	int radius = MIN(rect.w, rect.h) / 2;
	int cx = rect.x + rect.w / 2;
	int cy = rect.y + rect.h / 2;

	sdl_draw_filled_circle(renderer, cx, cy, radius, border);
	if (radius > 1)
		sdl_draw_filled_circle(renderer, cx, cy, radius - 1, fill);
	if (checked)
		sdl_draw_filled_circle(renderer, cx, cy, radius / 2, mark);
}

static void sdl_preferences_layout(TilemSdlUi *ui,
                                   TilemSdlPrefsLayout *layout)
{
	int text_h = sdl_pref_text_height(ui);
	int row_h = MAX(text_h + 8, 22);
	int padding = 18;
	int panel_w = ui->window_width - 40;
	int content_x;
	int content_w;
	int button_w;
	int close_w;
	int panel_h;
	int panel_x;
	int panel_y;
	int y;
	int label_w;

	if (panel_w > 520)
		panel_w = 520;
	if (panel_w < 240)
		panel_w = MAX(ui->window_width - 10, 200);

	button_w = sdl_menu_text_width(ui, "Choose...") + 20;
	close_w = sdl_menu_text_width(ui, "Close") + 24;

	panel_h = padding + text_h + 8;
	panel_h += 10;
	panel_h += text_h + 6;
	panel_h += row_h * 2;
	panel_h += 10;
	panel_h += text_h + 6;
	panel_h += row_h * 4;
	panel_h += 10;
	panel_h += row_h;
	panel_h += padding;

	panel_x = (ui->window_width - panel_w) / 2;
	panel_y = (ui->window_height - panel_h) / 2;
	if (panel_x < 0)
		panel_x = 0;
	if (panel_y < 0)
		panel_y = 0;

	layout->panel.x = panel_x;
	layout->panel.y = panel_y;
	layout->panel.w = panel_w;
	layout->panel.h = panel_h;
	layout->text_h = text_h;
	layout->row_h = row_h;
	layout->indicator = MIN(row_h - 6, 18);
	layout->padding = padding;

	content_x = panel_x + padding;
	content_w = panel_w - padding * 2;
	y = panel_y + padding;

	layout->title_y = y;
	y += text_h + 8;
	layout->speed_header_y = y;
	y += text_h + 6;

	layout->speed_limit_rect = (SDL_Rect) {
		content_x, y, content_w, row_h
	};
	y += row_h;
	layout->speed_fast_rect = (SDL_Rect) {
		content_x, y, content_w, row_h
	};
	y += row_h + 10;

	layout->display_header_y = y;
	y += text_h + 6;

	layout->grayscale_rect = (SDL_Rect) {
		content_x, y, content_w, row_h
	};
	y += row_h;
	layout->smooth_rect = (SDL_Rect) {
		content_x, y, content_w, row_h
	};
	y += row_h;
	layout->skin_toggle_rect = (SDL_Rect) {
		content_x, y, content_w, row_h
	};
	y += row_h;

	label_w = sdl_menu_text_width(ui, "Skin file:");
	layout->skin_label_rect = (SDL_Rect) {
		content_x, y, label_w, row_h
	};
	layout->skin_button_rect = (SDL_Rect) {
		content_x + content_w - button_w,
		y + (row_h - (row_h - 4)) / 2,
		button_w,
		row_h - 4
	};
	layout->skin_value_rect = (SDL_Rect) {
		content_x + label_w + 8,
		y,
		MAX(content_w - label_w - 8 - button_w - 8, 0),
		row_h
	};
	y += row_h + 10;

	layout->close_rect = (SDL_Rect) {
		content_x + content_w - close_w,
		y,
		close_w,
		row_h
	};
}

static TilemSdlPrefItem sdl_preferences_hit_test(
	TilemSdlUi *ui,
	const TilemSdlPrefsLayout *layout,
	int x, int y)
{
	(void) ui;

	if (!sdl_point_in_rect(x, y, &layout->panel))
		return SDL_PREF_ITEM_NONE;
	if (sdl_point_in_rect(x, y, &layout->skin_button_rect))
		return SDL_PREF_ITEM_SKIN_CHOOSE;
	if (sdl_point_in_rect(x, y, &layout->speed_limit_rect))
		return SDL_PREF_ITEM_SPEED_LIMIT;
	if (sdl_point_in_rect(x, y, &layout->speed_fast_rect))
		return SDL_PREF_ITEM_SPEED_FAST;
	if (sdl_point_in_rect(x, y, &layout->grayscale_rect))
		return SDL_PREF_ITEM_GRAYSCALE;
	if (sdl_point_in_rect(x, y, &layout->smooth_rect))
		return SDL_PREF_ITEM_SMOOTH;
	if (sdl_point_in_rect(x, y, &layout->skin_toggle_rect))
		return SDL_PREF_ITEM_USE_SKIN;
	if (sdl_point_in_rect(x, y, &layout->close_rect))
		return SDL_PREF_ITEM_CLOSE;
	return SDL_PREF_ITEM_NONE;
}

static void sdl_preferences_open(TilemSdlUi *ui)
{
	if (!ui)
		return;
	ui->prefs_visible = TRUE;
	ui->prefs_hover = SDL_PREF_ITEM_NONE;
	sdl_menu_hide(ui);
}

static void sdl_preferences_close(TilemSdlUi *ui)
{
	if (!ui)
		return;
	ui->prefs_visible = FALSE;
	ui->prefs_hover = SDL_PREF_ITEM_NONE;
}

static void sdl_preferences_set_limit_speed(TilemSdlUi *ui, gboolean limit)
{
	if (!ui || !ui->emu)
		return;
	tilem_calc_emulator_set_limit_speed(ui->emu, limit);
	tilem_config_set("emulation", "limit_speed/b", limit, NULL);
}

static void sdl_preferences_set_grayscale(TilemSdlUi *ui, gboolean enable)
{
	if (!ui || !ui->emu)
		return;
	tilem_calc_emulator_set_grayscale(ui->emu, enable);
	tilem_config_set("emulation", "grayscale/b", enable, NULL);
}

static void sdl_preferences_set_smooth(TilemSdlUi *ui, gboolean enable)
{
	if (!ui)
		return;
	ui->lcd_smooth_scale = enable;
	tilem_config_set("settings", "smooth_scaling/b", enable, NULL);
}

static void sdl_preferences_set_skin_enabled(TilemSdlUi *ui, gboolean enable)
{
	if (!ui)
		return;
	if (ui->skin_locked)
		return;

	ui->skin_disabled = !enable;
	tilem_config_set("settings", "skin_disabled/b", ui->skin_disabled, NULL);
	if (ui->emu && ui->emu->calc) {
		sdl_update_skin_for_calc(ui);
		sdl_update_layout(ui, ui->window_width, ui->window_height);
	}
}

static void sdl_preferences_choose_skin(TilemSdlUi *ui)
{
	char *dir = NULL;
	char *filename = NULL;
	GError *err = NULL;
	gboolean prev_disabled;

	if (!ui || !ui->emu || !ui->emu->calc) {
		sdl_show_message(ui, "Preferences",
		                 "Load a calculator first.");
		return;
	}
	if (ui->skin_locked)
		return;

	if (ui->skin_file_name)
		dir = g_path_get_dirname(ui->skin_file_name);
	else
		dir = get_shared_dir_path("skins", NULL);
	if (!dir)
		dir = g_get_current_dir();

	filename = sdl_native_file_dialog("Select Skin",
	                                  dir, NULL, FALSE, NULL);
	g_free(dir);

	if (!filename)
		return;

	prev_disabled = ui->skin_disabled;
	if (!sdl_load_skin(ui, filename, &err)) {
		sdl_report_error("Unable to load skin", err);
		ui->skin_disabled = prev_disabled;
		sdl_update_skin_for_calc(ui);
		g_free(filename);
		return;
	}

	ui->skin_disabled = FALSE;
	tilem_config_set("settings", "skin_disabled/b", FALSE, NULL);
	sdl_set_palette(ui);
	sdl_update_layout(ui, ui->window_width, ui->window_height);
	sdl_save_skin_name(ui);
	g_free(filename);
}

static gboolean sdl_preferences_handle_event(TilemSdlUi *ui,
                                             const SDL_Event *event)
{
	TilemSdlPrefsLayout layout;
	TilemSdlPrefItem hit;
	gboolean skin_controls_enabled;
	gboolean skin_file_enabled;

	if (!ui || !ui->prefs_visible || !event)
		return FALSE;

	sdl_preferences_layout(ui, &layout);
	skin_controls_enabled = !ui->skin_locked
		&& ui->emu && ui->emu->calc;
	skin_file_enabled = skin_controls_enabled && !ui->skin_disabled;

	switch (event->type) {
	case SDL_MOUSEMOTION:
		ui->prefs_hover = sdl_preferences_hit_test(
			ui, &layout, event->motion.x, event->motion.y);
		return TRUE;
	case SDL_MOUSEBUTTONDOWN:
		if (event->button.button != SDL_BUTTON_LEFT) {
			return TRUE;
		}
		if (!sdl_point_in_rect(event->button.x,
		                       event->button.y,
		                       &layout.panel)) {
			sdl_preferences_close(ui);
			return TRUE;
		}
		hit = sdl_preferences_hit_test(ui, &layout,
		                               event->button.x,
		                               event->button.y);
		switch (hit) {
		case SDL_PREF_ITEM_SPEED_LIMIT:
			sdl_preferences_set_limit_speed(ui, TRUE);
			break;
		case SDL_PREF_ITEM_SPEED_FAST:
			sdl_preferences_set_limit_speed(ui, FALSE);
			break;
		case SDL_PREF_ITEM_GRAYSCALE:
			if (ui->emu) {
				sdl_preferences_set_grayscale(
					ui, !ui->emu->grayscale);
			}
			break;
		case SDL_PREF_ITEM_SMOOTH:
			sdl_preferences_set_smooth(ui, !ui->lcd_smooth_scale);
			break;
		case SDL_PREF_ITEM_USE_SKIN:
			if (skin_controls_enabled) {
				sdl_preferences_set_skin_enabled(
					ui, ui->skin_disabled);
			}
			break;
		case SDL_PREF_ITEM_SKIN_CHOOSE:
			if (skin_file_enabled)
				sdl_preferences_choose_skin(ui);
			break;
		case SDL_PREF_ITEM_CLOSE:
			sdl_preferences_close(ui);
			break;
		default:
			break;
		}
		return TRUE;
	case SDL_KEYDOWN:
		if (event->key.keysym.sym == SDLK_ESCAPE)
			sdl_preferences_close(ui);
		return TRUE;
	case SDL_MOUSEBUTTONUP:
		return TRUE;
	default:
		break;
	}

	return FALSE;
}

static void sdl_render_preferences(TilemSdlUi *ui)
{
	TilemSdlPrefsLayout layout;
	SDL_Color overlay = { 0, 0, 0, 150 };
	SDL_Color panel = { 45, 45, 45, 240 };
	SDL_Color border = { 90, 90, 90, 255 };
	SDL_Color text = { 230, 230, 230, 255 };
	SDL_Color dim = { 160, 160, 160, 255 };
	SDL_Color disabled = { 110, 110, 110, 255 };
	SDL_Color highlight = { 70, 120, 180, 120 };
	SDL_Color indicator_fill = { 30, 30, 30, 255 };
	SDL_Color indicator_mark = { 210, 210, 210, 255 };
	SDL_Color button_fill = { 70, 70, 70, 255 };
	SDL_Color button_hover = { 90, 90, 90, 255 };
	SDL_Rect row;
	int indicator_size;
	int indicator_x;
	int indicator_y;
	gboolean skin_controls_enabled;
	gboolean skin_file_enabled;
	const char *skin_label = "Skin file:";
	char *skin_value;

	if (!ui || !ui->prefs_visible)
		return;

	sdl_preferences_layout(ui, &layout);
	indicator_size = layout.indicator;
	skin_controls_enabled = !ui->skin_locked
		&& ui->emu && ui->emu->calc;
	skin_file_enabled = skin_controls_enabled && !ui->skin_disabled;

	SDL_SetRenderDrawBlendMode(ui->renderer, SDL_BLENDMODE_BLEND);
	SDL_SetRenderDrawColor(ui->renderer, overlay.r, overlay.g,
	                       overlay.b, overlay.a);
	SDL_RenderFillRect(ui->renderer, NULL);

	SDL_SetRenderDrawColor(ui->renderer, panel.r, panel.g,
	                       panel.b, panel.a);
	SDL_RenderFillRect(ui->renderer, &layout.panel);
	SDL_SetRenderDrawColor(ui->renderer, border.r, border.g,
	                       border.b, border.a);
	SDL_RenderDrawRect(ui->renderer, &layout.panel);

	sdl_draw_text_menu(ui,
	                   layout.panel.x + layout.padding,
	                   layout.title_y,
	                   "Preferences",
	                   text);

	sdl_draw_text_menu(ui,
	                   layout.panel.x + layout.padding,
	                   layout.speed_header_y,
	                   "Emulation Speed",
	                   dim);

	row = layout.speed_limit_rect;
	if (ui->prefs_hover == SDL_PREF_ITEM_SPEED_LIMIT) {
		SDL_SetRenderDrawColor(ui->renderer, highlight.r,
		                       highlight.g, highlight.b,
		                       highlight.a);
		SDL_RenderFillRect(ui->renderer, &row);
	}
	indicator_x = row.x + 4;
	indicator_y = row.y + (row.h - indicator_size) / 2;
	sdl_draw_radio(ui->renderer,
	               (SDL_Rect) { indicator_x, indicator_y,
	                            indicator_size, indicator_size },
	               ui->emu && ui->emu->limit_speed,
	               border, indicator_fill, indicator_mark);
	sdl_draw_text_menu(ui, indicator_x + indicator_size + 8,
	                   row.y + (row.h - layout.text_h) / 2,
	                   "Limit to actual calculator speed",
	                   text);

	row = layout.speed_fast_rect;
	if (ui->prefs_hover == SDL_PREF_ITEM_SPEED_FAST) {
		SDL_SetRenderDrawColor(ui->renderer, highlight.r,
		                       highlight.g, highlight.b,
		                       highlight.a);
		SDL_RenderFillRect(ui->renderer, &row);
	}
	indicator_x = row.x + 4;
	indicator_y = row.y + (row.h - indicator_size) / 2;
	sdl_draw_radio(ui->renderer,
	               (SDL_Rect) { indicator_x, indicator_y,
	                            indicator_size, indicator_size },
	               ui->emu && !ui->emu->limit_speed,
	               border, indicator_fill, indicator_mark);
	sdl_draw_text_menu(ui, indicator_x + indicator_size + 8,
	                   row.y + (row.h - layout.text_h) / 2,
	                   "As fast as possible",
	                   text);

	sdl_draw_text_menu(ui,
	                   layout.panel.x + layout.padding,
	                   layout.display_header_y,
	                   "Display",
	                   dim);

	row = layout.grayscale_rect;
	if (ui->prefs_hover == SDL_PREF_ITEM_GRAYSCALE) {
		SDL_SetRenderDrawColor(ui->renderer, highlight.r,
		                       highlight.g, highlight.b,
		                       highlight.a);
		SDL_RenderFillRect(ui->renderer, &row);
	}
	indicator_x = row.x + 4;
	indicator_y = row.y + (row.h - indicator_size) / 2;
	sdl_draw_checkbox(ui->renderer,
	                  (SDL_Rect) { indicator_x, indicator_y,
	                               indicator_size, indicator_size },
	                  ui->emu && ui->emu->grayscale,
	                  border, indicator_fill, indicator_mark);
	sdl_draw_text_menu(ui, indicator_x + indicator_size + 8,
	                   row.y + (row.h - layout.text_h) / 2,
	                   "Emulate grayscale",
	                   text);

	row = layout.smooth_rect;
	if (ui->prefs_hover == SDL_PREF_ITEM_SMOOTH) {
		SDL_SetRenderDrawColor(ui->renderer, highlight.r,
		                       highlight.g, highlight.b,
		                       highlight.a);
		SDL_RenderFillRect(ui->renderer, &row);
	}
	indicator_x = row.x + 4;
	indicator_y = row.y + (row.h - indicator_size) / 2;
	sdl_draw_checkbox(ui->renderer,
	                  (SDL_Rect) { indicator_x, indicator_y,
	                               indicator_size, indicator_size },
	                  ui->lcd_smooth_scale,
	                  border, indicator_fill, indicator_mark);
	sdl_draw_text_menu(ui, indicator_x + indicator_size + 8,
	                   row.y + (row.h - layout.text_h) / 2,
	                   "Use smooth scaling",
	                   text);

	row = layout.skin_toggle_rect;
	if (ui->prefs_hover == SDL_PREF_ITEM_USE_SKIN) {
		SDL_SetRenderDrawColor(ui->renderer, highlight.r,
		                       highlight.g, highlight.b,
		                       highlight.a);
		SDL_RenderFillRect(ui->renderer, &row);
	}
	indicator_x = row.x + 4;
	indicator_y = row.y + (row.h - indicator_size) / 2;
	sdl_draw_checkbox(ui->renderer,
	                  (SDL_Rect) { indicator_x, indicator_y,
	                               indicator_size, indicator_size },
	                  skin_controls_enabled && !ui->skin_disabled,
	                  border, indicator_fill, indicator_mark);
	sdl_draw_text_menu(ui, indicator_x + indicator_size + 8,
	                   row.y + (row.h - layout.text_h) / 2,
	                   "Use skin",
	                   skin_controls_enabled ? text : disabled);

	if (ui->skin_disabled)
		skin_value = g_strdup("Disabled");
	else if (ui->skin_file_name)
		skin_value = sdl_pref_trim_text(
			ui, ui->skin_file_name, layout.skin_value_rect.w);
	else
		skin_value = g_strdup("Default skin");

	sdl_draw_text_menu(ui,
	                   layout.skin_label_rect.x,
	                   layout.skin_label_rect.y
	                       + (layout.skin_label_rect.h - layout.text_h) / 2,
	                   skin_label,
	                   skin_controls_enabled ? dim : disabled);
	sdl_draw_text_menu(ui,
	                   layout.skin_value_rect.x,
	                   layout.skin_value_rect.y
	                       + (layout.skin_value_rect.h - layout.text_h) / 2,
	                   skin_value,
	                   skin_file_enabled ? text : disabled);

	SDL_SetRenderDrawColor(ui->renderer,
	                       (ui->prefs_hover == SDL_PREF_ITEM_SKIN_CHOOSE
	                        && skin_file_enabled)
	                           ? button_hover.r
	                           : button_fill.r,
	                       (ui->prefs_hover == SDL_PREF_ITEM_SKIN_CHOOSE
	                        && skin_file_enabled)
	                           ? button_hover.g
	                           : button_fill.g,
	                       (ui->prefs_hover == SDL_PREF_ITEM_SKIN_CHOOSE
	                        && skin_file_enabled)
	                           ? button_hover.b
	                           : button_fill.b,
	                       255);
	SDL_RenderFillRect(ui->renderer, &layout.skin_button_rect);
	SDL_SetRenderDrawColor(ui->renderer, border.r, border.g,
	                       border.b, border.a);
	SDL_RenderDrawRect(ui->renderer, &layout.skin_button_rect);
	sdl_draw_text_menu(ui,
	                   layout.skin_button_rect.x + 10,
	                   layout.skin_button_rect.y
	                       + (layout.skin_button_rect.h - layout.text_h) / 2,
	                   "Choose...",
	                   skin_file_enabled ? text : disabled);

	SDL_SetRenderDrawColor(ui->renderer,
	                       ui->prefs_hover == SDL_PREF_ITEM_CLOSE
	                           ? button_hover.r
	                           : button_fill.r,
	                       ui->prefs_hover == SDL_PREF_ITEM_CLOSE
	                           ? button_hover.g
	                           : button_fill.g,
	                       ui->prefs_hover == SDL_PREF_ITEM_CLOSE
	                           ? button_hover.b
	                           : button_fill.b,
	                       255);
	SDL_RenderFillRect(ui->renderer, &layout.close_rect);
	SDL_SetRenderDrawColor(ui->renderer, border.r, border.g,
	                       border.b, border.a);
	SDL_RenderDrawRect(ui->renderer, &layout.close_rect);
	sdl_draw_text_menu(ui,
	                   layout.close_rect.x + 12,
	                   layout.close_rect.y
	                       + (layout.close_rect.h - layout.text_h) / 2,
	                   "Close",
	                   text);

	SDL_SetRenderDrawBlendMode(ui->renderer, SDL_BLENDMODE_NONE);
	g_free(skin_value);
}

static int sdl_hex_value(char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return 10 + (c - 'a');
	if (c >= 'A' && c <= 'F')
		return 10 + (c - 'A');
	return -1;
}

static gboolean sdl_parse_color_string(const char *value,
                                       int *r, int *g, int *b)
{
	int v1;
	int v2;

	if (!value || value[0] != '#')
		return FALSE;
	if (strlen(value) == 4) {
		v1 = sdl_hex_value(value[1]);
		v2 = sdl_hex_value(value[2]);
		if (v1 < 0 || v2 < 0 || sdl_hex_value(value[3]) < 0)
			return FALSE;
		*r = v1 * 17;
		*g = v2 * 17;
		*b = sdl_hex_value(value[3]) * 17;
		return TRUE;
	}
	if (strlen(value) == 7) {
		v1 = sdl_hex_value(value[1]);
		v2 = sdl_hex_value(value[2]);
		if (v1 < 0 || v2 < 0)
			return FALSE;
		*r = v1 * 16 + v2;
		v1 = sdl_hex_value(value[3]);
		v2 = sdl_hex_value(value[4]);
		if (v1 < 0 || v2 < 0)
			return FALSE;
		*g = v1 * 16 + v2;
		v1 = sdl_hex_value(value[5]);
		v2 = sdl_hex_value(value[6]);
		if (v1 < 0 || v2 < 0)
			return FALSE;
		*b = v1 * 16 + v2;
		return TRUE;
	}
	return FALSE;
}

static char *sdl_color_to_string(int r, int g, int b)
{
	r = CLAMP(r, 0, 255);
	g = CLAMP(g, 0, 255);
	b = CLAMP(b, 0, 255);
	return g_strdup_printf("#%02X%02X%02X", r, g, b);
}

static void sdl_screenshot_free_preview(TilemSdlScreenshotDialog *dlg)
{
	if (!dlg)
		return;
	if (dlg->preview_texture) {
		SDL_DestroyTexture(dlg->preview_texture);
		dlg->preview_texture = NULL;
	}
	dlg->preview_width = 0;
	dlg->preview_height = 0;
}

static void sdl_screenshot_update_palette(TilemSdlScreenshotDialog *dlg)
{
	if (!dlg)
		return;
	if (dlg->preview_palette) {
		tilem_free(dlg->preview_palette);
		dlg->preview_palette = NULL;
	}

	dlg->preview_palette = tilem_color_palette_new(
		CLAMP(dlg->bg_r, 0, 255),
		CLAMP(dlg->bg_g, 0, 255),
		CLAMP(dlg->bg_b, 0, 255),
		CLAMP(dlg->fg_r, 0, 255),
		CLAMP(dlg->fg_g, 0, 255),
		CLAMP(dlg->fg_b, 0, 255),
		SDL_LCD_GAMMA);
	dlg->preview_dirty = TRUE;
}

static void sdl_screenshot_apply_anim_settings(TilemSdlScreenshotDialog *dlg)
{
	GdkColor fg;
	GdkColor bg;

	if (!dlg || !dlg->current_anim)
		return;

	tilem_animation_set_size(dlg->current_anim, dlg->width, dlg->height);
	tilem_animation_set_speed(dlg->current_anim,
	                          dlg->speed > 0.0 ? dlg->speed : 1.0);

	fg.red = (guint16) CLAMP(dlg->fg_r, 0, 255) * 257;
	fg.green = (guint16) CLAMP(dlg->fg_g, 0, 255) * 257;
	fg.blue = (guint16) CLAMP(dlg->fg_b, 0, 255) * 257;
	fg.pixel = 0;

	bg.red = (guint16) CLAMP(dlg->bg_r, 0, 255) * 257;
	bg.green = (guint16) CLAMP(dlg->bg_g, 0, 255) * 257;
	bg.blue = (guint16) CLAMP(dlg->bg_b, 0, 255) * 257;
	bg.pixel = 0;

	tilem_animation_set_colors(dlg->current_anim, &fg, &bg);
}

static void sdl_screenshot_set_current_anim(TilemSdlUi *ui,
                                            TilemAnimation *anim)
{
	TilemSdlScreenshotDialog *dlg;

	if (!ui)
		return;
	dlg = &ui->screenshot_dialog;

	if (anim)
		g_object_ref(anim);
	if (dlg->current_anim)
		g_object_unref(dlg->current_anim);
	dlg->current_anim = anim;

	dlg->preview_frame = NULL;
	dlg->preview_frame_start = 0;
	sdl_screenshot_free_preview(dlg);
	if (dlg->current_anim) {
		sdl_screenshot_update_palette(dlg);
		sdl_screenshot_apply_anim_settings(dlg);
		dlg->preview_dirty = TRUE;
	}
}

static const TilemSdlScreenshotSize *sdl_screenshot_sizes_for_calc(
	TilemSdlUi *ui, int *count)
{
	if (sdl_is_wide_screen(ui)) {
		*count = (int) G_N_ELEMENTS(sdl_screenshot_sizes_wide);
		return sdl_screenshot_sizes_wide;
	}
	*count = (int) G_N_ELEMENTS(sdl_screenshot_sizes_normal);
	return sdl_screenshot_sizes_normal;
}

static int sdl_screenshot_find_size_index(
	const TilemSdlScreenshotSize *sizes,
	int count,
	int width,
	int height)
{
	int i;

	for (i = 0; i < count; i++) {
		if (sizes[i].width == width && sizes[i].height == height)
			return i;
	}

	return count;
}

static void sdl_screenshot_set_size(TilemSdlUi *ui, int width, int height)
{
	TilemSdlScreenshotDialog *dlg;
	const TilemSdlScreenshotSize *sizes;
	int count;

	if (!ui)
		return;

	dlg = &ui->screenshot_dialog;
	dlg->width = CLAMP(width, 1, SDL_SCREENSHOT_MAX_WIDTH);
	dlg->height = CLAMP(height, 1, SDL_SCREENSHOT_MAX_HEIGHT);

	sizes = sdl_screenshot_sizes_for_calc(ui, &count);
	dlg->selected_size = sdl_screenshot_find_size_index(
		sizes, count, dlg->width, dlg->height);

	sdl_screenshot_apply_anim_settings(dlg);
	dlg->preview_dirty = TRUE;
}

static const char *sdl_screenshot_size_label(
	const TilemSdlScreenshotSize *sizes,
	int count,
	int index,
	char *buf,
	size_t buf_size)
{
	if (!buf || buf_size == 0)
		return "";
	if (index < 0 || index >= count) {
		g_snprintf(buf, buf_size, "Custom");
		return buf;
	}
	g_snprintf(buf, buf_size, "%d x %d",
	           sizes[index].width, sizes[index].height);
	return buf;
}

static void sdl_screenshot_dialog_layout(TilemSdlUi *ui,
                                         TilemSdlScreenshotLayout *layout)
{
	int text_h = sdl_pref_text_height(ui);
	int row_h = MAX(text_h + 8, 22);
	int padding = 16;
	int panel_w = ui->window_width - padding * 2;
	int panel_h;
	int preview_h;
	int button_w;
	int button_h = row_h + 4;
	int content_x;
	int content_w;
	int y;
	int label_w;
	int field_w;
	int color_w;
	int buttons_y;

	if (panel_w > 720)
		panel_w = 720;
	if (panel_w < 420)
		panel_w = MAX(ui->window_width - 10, 320);

	button_w = sdl_menu_text_width(ui, "Record") + 20;
	preview_h = row_h * 6;
	if (preview_h < 180)
		preview_h = 180;
	if (preview_h > 240)
		preview_h = 240;

	panel_h = padding * 2 + preview_h;
	panel_h += (row_h + 6) * 7;
	panel_h += button_h + 10;

	layout->panel.w = panel_w;
	layout->panel.h = panel_h;
	layout->panel.x = (ui->window_width - panel_w) / 2;
	layout->panel.y = (ui->window_height - panel_h) / 2;

	layout->text_h = text_h;
	layout->row_h = row_h;
	layout->padding = padding;

	content_x = layout->panel.x + padding;
	content_w = panel_w - padding * 2;
	y = layout->panel.y + padding;

	layout->preview_rect = (SDL_Rect) {
		content_x,
		y,
		content_w - button_w - padding,
		preview_h
	};

	layout->grab_rect = (SDL_Rect) {
		layout->preview_rect.x + layout->preview_rect.w + padding,
		y,
		button_w,
		button_h
	};
	layout->record_rect = (SDL_Rect) {
		layout->grab_rect.x,
		y + button_h + 6,
		button_w,
		button_h
	};
	layout->stop_rect = (SDL_Rect) {
		layout->grab_rect.x,
		y + (button_h + 6) * 2,
		button_w,
		button_h
	};

	y += preview_h + padding;

	layout->grayscale_rect = (SDL_Rect) { content_x, y, content_w, row_h };
	y += row_h + 6;
	layout->size_rect = (SDL_Rect) { content_x, y, content_w, row_h };
	y += row_h + 6;
	layout->width_rect = (SDL_Rect) { content_x, y, content_w, row_h };
	y += row_h + 6;
	layout->height_rect = (SDL_Rect) { content_x, y, content_w, row_h };
	y += row_h + 6;
	layout->speed_rect = (SDL_Rect) { content_x, y, content_w, row_h };
	y += row_h + 6;

	label_w = sdl_menu_text_width(ui, "Background:") + 8;
	field_w = sdl_menu_text_width(ui, "0000") + 18;
	color_w = row_h - 8;

	layout->fg_label_rect = (SDL_Rect) { content_x, y, label_w, row_h };
	layout->fg_r_rect = (SDL_Rect) {
		content_x + label_w,
		y,
		field_w,
		row_h
	};
	layout->fg_g_rect = (SDL_Rect) {
		layout->fg_r_rect.x + field_w + 6,
		y,
		field_w,
		row_h
	};
	layout->fg_b_rect = (SDL_Rect) {
		layout->fg_g_rect.x + field_w + 6,
		y,
		field_w,
		row_h
	};
	layout->fg_color_rect = (SDL_Rect) {
		layout->fg_b_rect.x + field_w + 6,
		y + (row_h - color_w) / 2,
		color_w,
		color_w
	};
	y += row_h + 6;

	layout->bg_label_rect = (SDL_Rect) { content_x, y, label_w, row_h };
	layout->bg_r_rect = (SDL_Rect) {
		content_x + label_w,
		y,
		field_w,
		row_h
	};
	layout->bg_g_rect = (SDL_Rect) {
		layout->bg_r_rect.x + field_w + 6,
		y,
		field_w,
		row_h
	};
	layout->bg_b_rect = (SDL_Rect) {
		layout->bg_g_rect.x + field_w + 6,
		y,
		field_w,
		row_h
	};
	layout->bg_color_rect = (SDL_Rect) {
		layout->bg_b_rect.x + field_w + 6,
		y + (row_h - color_w) / 2,
		color_w,
		color_w
	};

	buttons_y = layout->panel.y + layout->panel.h - padding - button_h;
	layout->save_rect = (SDL_Rect) {
		layout->panel.x + layout->panel.w - padding - button_w * 2 - 10,
		buttons_y,
		button_w,
		button_h
	};
	layout->cancel_rect = (SDL_Rect) {
		layout->panel.x + layout->panel.w - padding - button_w,
		buttons_y,
		button_w,
		button_h
	};
	(void) color_w;
}

static TilemSdlScreenshotItem sdl_screenshot_dialog_hit_test(
	TilemSdlUi *ui,
	const TilemSdlScreenshotLayout *layout,
	int x, int y)
{
	(void) ui;
	if (!sdl_point_in_rect(x, y, &layout->panel))
		return SDL_SS_ITEM_NONE;
	if (sdl_point_in_rect(x, y, &layout->grab_rect))
		return SDL_SS_ITEM_GRAB;
	if (sdl_point_in_rect(x, y, &layout->record_rect))
		return SDL_SS_ITEM_RECORD;
	if (sdl_point_in_rect(x, y, &layout->stop_rect))
		return SDL_SS_ITEM_STOP;
	if (sdl_point_in_rect(x, y, &layout->grayscale_rect))
		return SDL_SS_ITEM_GRAYSCALE;
	if (sdl_point_in_rect(x, y, &layout->size_rect))
		return SDL_SS_ITEM_SIZE;
	if (sdl_point_in_rect(x, y, &layout->width_rect))
		return SDL_SS_ITEM_WIDTH;
	if (sdl_point_in_rect(x, y, &layout->height_rect))
		return SDL_SS_ITEM_HEIGHT;
	if (sdl_point_in_rect(x, y, &layout->speed_rect))
		return SDL_SS_ITEM_SPEED;
	if (sdl_point_in_rect(x, y, &layout->fg_r_rect))
		return SDL_SS_ITEM_FG_R;
	if (sdl_point_in_rect(x, y, &layout->fg_g_rect))
		return SDL_SS_ITEM_FG_G;
	if (sdl_point_in_rect(x, y, &layout->fg_b_rect))
		return SDL_SS_ITEM_FG_B;
	if (sdl_point_in_rect(x, y, &layout->bg_r_rect))
		return SDL_SS_ITEM_BG_R;
	if (sdl_point_in_rect(x, y, &layout->bg_g_rect))
		return SDL_SS_ITEM_BG_G;
	if (sdl_point_in_rect(x, y, &layout->bg_b_rect))
		return SDL_SS_ITEM_BG_B;
	if (sdl_point_in_rect(x, y, &layout->save_rect))
		return SDL_SS_ITEM_SAVE;
	if (sdl_point_in_rect(x, y, &layout->cancel_rect))
		return SDL_SS_ITEM_CANCEL;
	return SDL_SS_ITEM_NONE;
}

static void sdl_screenshot_dialog_start_input(
	TilemSdlScreenshotDialog *dlg,
	TilemSdlScreenshotInput mode,
	const char *prefill)
{
	if (!dlg)
		return;
	dlg->input_mode = mode;
	dlg->input_len = 0;
	dlg->input_buf[0] = '\0';
	if (prefill) {
		dlg->input_len = (int) g_strlcpy(
			dlg->input_buf, prefill, sizeof(dlg->input_buf));
	}
	SDL_StartTextInput();
}

static void sdl_screenshot_dialog_cancel_input(TilemSdlScreenshotDialog *dlg)
{
	if (!dlg)
		return;
	dlg->input_mode = SDL_SS_INPUT_NONE;
	dlg->input_len = 0;
	dlg->input_buf[0] = '\0';
	SDL_StopTextInput();
}

static void sdl_screenshot_dialog_apply_input(TilemSdlUi *ui)
{
	TilemSdlScreenshotDialog *dlg;
	int value;
	double dvalue;

	if (!ui)
		return;
	dlg = &ui->screenshot_dialog;

	switch (dlg->input_mode) {
	case SDL_SS_INPUT_WIDTH:
		value = atoi(dlg->input_buf);
		if (value > 0)
			sdl_screenshot_set_size(ui, value, dlg->height);
		break;
	case SDL_SS_INPUT_HEIGHT:
		value = atoi(dlg->input_buf);
		if (value > 0)
			sdl_screenshot_set_size(ui, dlg->width, value);
		break;
	case SDL_SS_INPUT_SPEED:
		dvalue = g_ascii_strtod(dlg->input_buf, NULL);
		if (dvalue >= 0.1 && dvalue <= 100.0) {
			dlg->speed = dvalue;
			sdl_screenshot_apply_anim_settings(dlg);
			dlg->preview_dirty = TRUE;
		}
		break;
	case SDL_SS_INPUT_FG_R:
		value = atoi(dlg->input_buf);
		dlg->fg_r = CLAMP(value, 0, 255);
		sdl_screenshot_update_palette(dlg);
		sdl_screenshot_apply_anim_settings(dlg);
		break;
	case SDL_SS_INPUT_FG_G:
		value = atoi(dlg->input_buf);
		dlg->fg_g = CLAMP(value, 0, 255);
		sdl_screenshot_update_palette(dlg);
		sdl_screenshot_apply_anim_settings(dlg);
		break;
	case SDL_SS_INPUT_FG_B:
		value = atoi(dlg->input_buf);
		dlg->fg_b = CLAMP(value, 0, 255);
		sdl_screenshot_update_palette(dlg);
		sdl_screenshot_apply_anim_settings(dlg);
		break;
	case SDL_SS_INPUT_BG_R:
		value = atoi(dlg->input_buf);
		dlg->bg_r = CLAMP(value, 0, 255);
		sdl_screenshot_update_palette(dlg);
		sdl_screenshot_apply_anim_settings(dlg);
		break;
	case SDL_SS_INPUT_BG_G:
		value = atoi(dlg->input_buf);
		dlg->bg_g = CLAMP(value, 0, 255);
		sdl_screenshot_update_palette(dlg);
		sdl_screenshot_apply_anim_settings(dlg);
		break;
	case SDL_SS_INPUT_BG_B:
		value = atoi(dlg->input_buf);
		dlg->bg_b = CLAMP(value, 0, 255);
		sdl_screenshot_update_palette(dlg);
		sdl_screenshot_apply_anim_settings(dlg);
		break;
	default:
		break;
	}

	sdl_screenshot_dialog_cancel_input(dlg);
}

static void sdl_screenshot_dialog_grab(TilemSdlUi *ui)
{
	TilemSdlScreenshotDialog *dlg;
	TilemAnimation *anim;

	if (!ui || !ui->emu || !ui->emu->calc)
		return;

	dlg = &ui->screenshot_dialog;
	anim = tilem_calc_emulator_get_screenshot(ui->emu, dlg->grayscale);
	if (!anim)
		return;

	dlg->current_anim_grayscale = dlg->grayscale;
	sdl_screenshot_set_current_anim(ui, anim);
	g_object_unref(anim);
}

static void sdl_screenshot_dialog_record_start(TilemSdlUi *ui)
{
	TilemSdlScreenshotDialog *dlg;

	if (!ui || !ui->emu || !ui->emu->calc)
		return;
	dlg = &ui->screenshot_dialog;
	if (ui->emu->anim)
		return;

	tilem_calc_emulator_begin_animation(ui->emu, dlg->grayscale);
	dlg->current_anim_grayscale = dlg->grayscale;
}

static void sdl_screenshot_dialog_record_stop(TilemSdlUi *ui)
{
	TilemSdlScreenshotDialog *dlg;
	TilemAnimation *anim;

	if (!ui || !ui->emu || !ui->emu->calc)
		return;
	dlg = &ui->screenshot_dialog;

	if (!ui->emu->anim)
		return;

	anim = tilem_calc_emulator_end_animation(ui->emu);
	if (!anim)
		return;

	sdl_screenshot_set_current_anim(ui, anim);
	g_object_unref(anim);
}

static gboolean sdl_screenshot_anim_is_static(TilemAnimation *anim)
{
	TilemAnimFrame *first;
	TilemAnimFrame *next;

	if (!anim)
		return TRUE;
	first = tilem_animation_next_frame(anim, NULL);
	if (!first)
		return TRUE;
	next = tilem_animation_next_frame(anim, first);
	return (next == NULL);
}

static gboolean sdl_screenshot_dialog_save(TilemSdlUi *ui)
{
	TilemSdlScreenshotDialog *dlg;
	char *dir = NULL;
	char *format = NULL;
	char *filename = NULL;
	char *default_name = NULL;
	char *basename = NULL;
	char *final_name = NULL;
	char *format_lower = NULL;
	char *fg = NULL;
	char *bg = NULL;
	const char *format_opt = NULL;
	const char *width_opt = NULL;
	const char *height_opt = NULL;
	gboolean is_static;
	gboolean used_native = FALSE;
	GError *err = NULL;

	if (!ui || !ui->emu || !ui->emu->calc)
		return FALSE;
	dlg = &ui->screenshot_dialog;
	if (!dlg->current_anim)
		return FALSE;

	is_static = sdl_screenshot_anim_is_static(dlg->current_anim);
	tilem_config_get("screenshot",
	                 "directory/f", &dir,
	                 "static_format/s", &format,
	                 NULL);

	if (!dir)
		dir = g_get_current_dir();

	if (!is_static) {
		g_free(format);
		format = g_strdup("gif");
	}
	else if (!format) {
		format = g_strdup(SDL_SCREENSHOT_DEFAULT_FORMAT);
	}

	default_name = sdl_find_free_filename(dir, "screenshot", format);
	if (default_name) {
		basename = g_path_get_basename(default_name);
	}
	else {
		basename = g_strdup_printf("screenshot.%s", format);
	}

	filename = sdl_native_file_dialog("Save Screenshot", dir, basename,
	                                  TRUE, &used_native);
	if (!filename) {
		g_free(dir);
		g_free(format);
		g_free(default_name);
		g_free(basename);
		return FALSE;
	}

	if (!is_static) {
		format_lower = g_strdup("gif");
	}
	else {
		format_lower = sdl_guess_image_format(filename, format);
		if (g_strcmp0(format_lower, "png") != 0
		    && g_strcmp0(format_lower, "gif") != 0
		    && g_strcmp0(format_lower, "bmp") != 0
		    && g_strcmp0(format_lower, "jpg") != 0
		    && g_strcmp0(format_lower, "jpeg") != 0) {
			sdl_show_message(ui, "Screenshot",
			                 "File name does not have a "
			                 "recognized suffix.");
			g_free(dir);
			g_free(format);
			g_free(default_name);
			g_free(basename);
			g_free(filename);
			g_free(format_lower);
			return FALSE;
		}
	}

	final_name = sdl_ensure_extension(filename, format_lower);

	sdl_screenshot_apply_anim_settings(dlg);
	if (!tilem_animation_save(dlg->current_anim, final_name,
	                          format_lower, NULL, NULL, &err)) {
		sdl_report_error("Unable to save screenshot", err);
		g_free(dir);
		g_free(format);
		g_free(default_name);
		g_free(basename);
		g_free(filename);
		g_free(final_name);
		g_free(format_lower);
		return FALSE;
	}

	g_free(default_name);
	g_free(basename);
	g_free(filename);

	fg = sdl_color_to_string(dlg->fg_r, dlg->fg_g, dlg->fg_b);
	bg = sdl_color_to_string(dlg->bg_r, dlg->bg_g, dlg->bg_b);

	if (sdl_is_wide_screen(ui)) {
		width_opt = "width_128x64/i";
		height_opt = "height_128x64/i";
	}
	else {
		width_opt = "width_96x64/i";
		height_opt = "height_96x64/i";
	}

	if (is_static)
		format_opt = "static_format/s";

	tilem_config_set("screenshot",
	                 "directory/f", dir,
	                 "grayscale/b", dlg->current_anim_grayscale,
	                 "foreground/s", fg,
	                 "background/s", bg,
	                 width_opt, dlg->width,
	                 height_opt, dlg->height,
	                 format_opt, format_lower,
	                 NULL);

	g_free(dir);
	g_free(format);
	g_free(final_name);
	g_free(format_lower);
	g_free(fg);
	g_free(bg);
	return TRUE;
}

static void sdl_screenshot_dialog_open(TilemSdlUi *ui)
{
	TilemSdlScreenshotDialog *dlg;
	char *fg = NULL;
	char *bg = NULL;
	int grayscale = 1;
	int w96 = 0;
	int h96 = 0;
	int w128 = 0;
	int h128 = 0;
	int width;
	int height;

	if (!ui || !ui->emu || !ui->emu->calc)
		return;

	dlg = &ui->screenshot_dialog;
	dlg->visible = TRUE;
	dlg->size_menu_open = FALSE;
	dlg->size_menu_hover = -1;
	dlg->hover = SDL_SS_ITEM_NONE;
	dlg->input_mode = SDL_SS_INPUT_NONE;
	dlg->input_len = 0;
	dlg->input_buf[0] = '\0';

	tilem_config_get("screenshot",
	                 "grayscale/b=1", &grayscale,
	                 "width_96x64/i", &w96,
	                 "height_96x64/i", &h96,
	                 "width_128x64/i", &w128,
	                 "height_128x64/i", &h128,
	                 "foreground/s", &fg,
	                 "background/s", &bg,
	                 NULL);

	dlg->grayscale = grayscale;
	if (sdl_is_wide_screen(ui)) {
		width = (w128 > 0 ? w128 : SDL_SCREENSHOT_DEFAULT_WIDTH_128);
		height = (h128 > 0 ? h128 : SDL_SCREENSHOT_DEFAULT_HEIGHT_128);
	}
	else {
		width = (w96 > 0 ? w96 : SDL_SCREENSHOT_DEFAULT_WIDTH_96);
		height = (h96 > 0 ? h96 : SDL_SCREENSHOT_DEFAULT_HEIGHT_96);
	}

	dlg->fg_r = 0;
	dlg->fg_g = 0;
	dlg->fg_b = 0;
	dlg->bg_r = 255;
	dlg->bg_g = 255;
	dlg->bg_b = 255;
	if (fg)
		sdl_parse_color_string(fg, &dlg->fg_r, &dlg->fg_g, &dlg->fg_b);
	if (bg)
		sdl_parse_color_string(bg, &dlg->bg_r, &dlg->bg_g, &dlg->bg_b);

	dlg->speed = 1.0;

	sdl_screenshot_set_size(ui, width, height);
	sdl_screenshot_update_palette(dlg);
	sdl_screenshot_dialog_grab(ui);

	g_free(fg);
	g_free(bg);
	SDL_StopTextInput();
	sdl_menu_hide(ui);
}

static void sdl_screenshot_dialog_close(TilemSdlUi *ui)
{
	TilemSdlScreenshotDialog *dlg;
	TilemAnimation *anim;

	if (!ui)
		return;
	dlg = &ui->screenshot_dialog;

	if (ui->emu && ui->emu->anim) {
		anim = tilem_calc_emulator_end_animation(ui->emu);
		if (anim)
			g_object_unref(anim);
	}
	sdl_screenshot_set_current_anim(ui, NULL);
	dlg->visible = FALSE;
	dlg->size_menu_open = FALSE;
	dlg->size_menu_hover = -1;
	dlg->hover = SDL_SS_ITEM_NONE;
	sdl_screenshot_dialog_cancel_input(dlg);
}

static void sdl_screenshot_dialog_update_preview(TilemSdlUi *ui)
{
	TilemSdlScreenshotDialog *dlg;
	TilemAnimFrame *next;
	Uint32 now;
	int duration;
	double stretch;
	byte *indexed = NULL;
	guchar *rgb = NULL;
	int w;
	int h;
	int i;
	int total;

	if (!ui)
		return;
	dlg = &ui->screenshot_dialog;
	if (!dlg->current_anim)
		return;

	if (!dlg->preview_frame) {
		dlg->preview_frame = tilem_animation_next_frame(
			dlg->current_anim, NULL);
		dlg->preview_frame_start = SDL_GetTicks();
		dlg->preview_dirty = TRUE;
	}

	if (!dlg->preview_frame)
		return;

	now = SDL_GetTicks();
	duration = tilem_anim_frame_get_duration(dlg->preview_frame);
	if (dlg->speed > 0.0)
		stretch = 1.0 / dlg->speed;
	else
		stretch = 1.0;
	duration = (int) (duration * stretch + 0.5);
	if (duration < 20)
		duration = 20;

	if (now - dlg->preview_frame_start >= (Uint32) duration) {
		next = tilem_animation_next_frame(dlg->current_anim,
		                                  dlg->preview_frame);
		if (!next) {
			next = tilem_animation_next_frame(dlg->current_anim, NULL);
		}
		if (next) {
			dlg->preview_frame = next;
			dlg->preview_frame_start = now;
			dlg->preview_dirty = TRUE;
		}
	}

	if (!dlg->preview_dirty)
		return;

	sdl_screenshot_free_preview(dlg);
	if (!dlg->preview_palette)
		sdl_screenshot_update_palette(dlg);

	tilem_animation_get_indexed_image(dlg->current_anim,
	                                  dlg->preview_frame,
	                                  &indexed, &w, &h);
	if (!indexed)
		return;

	total = w * h;
	rgb = g_new(guchar, total * 3);
	for (i = 0; i < total; i++) {
		dword color = dlg->preview_palette[indexed[i]];
		rgb[i * 3] = (guchar) (color >> 16);
		rgb[i * 3 + 1] = (guchar) (color >> 8);
		rgb[i * 3 + 2] = (guchar) color;
	}

	dlg->preview_texture = SDL_CreateTexture(
		ui->renderer,
		SDL_PIXELFORMAT_RGB24,
		SDL_TEXTUREACCESS_STATIC,
		w, h);
	if (dlg->preview_texture) {
		SDL_UpdateTexture(dlg->preview_texture, NULL, rgb, w * 3);
		dlg->preview_width = w;
		dlg->preview_height = h;
	}

	g_free(indexed);
	g_free(rgb);
	dlg->preview_dirty = FALSE;
}

static void sdl_draw_button(TilemSdlUi *ui, SDL_Rect rect,
                            const char *label,
                            gboolean hover, gboolean enabled)
{
	SDL_Color border = { 90, 90, 90, 255 };
	SDL_Color fill = { 70, 70, 70, 255 };
	SDL_Color hover_fill = { 90, 90, 90, 255 };
	SDL_Color disabled = { 120, 120, 120, 255 };
	SDL_Color text = { 230, 230, 230, 255 };
	SDL_Color text_disabled = { 150, 150, 150, 255 };
	int text_h = sdl_pref_text_height(ui);

	SDL_SetRenderDrawColor(ui->renderer,
	                       enabled
	                           ? (hover ? hover_fill.r : fill.r)
	                           : disabled.r,
	                       enabled
	                           ? (hover ? hover_fill.g : fill.g)
	                           : disabled.g,
	                       enabled
	                           ? (hover ? hover_fill.b : fill.b)
	                           : disabled.b,
	                       255);
	SDL_RenderFillRect(ui->renderer, &rect);
	SDL_SetRenderDrawColor(ui->renderer, border.r, border.g,
	                       border.b, border.a);
	SDL_RenderDrawRect(ui->renderer, &rect);
	sdl_draw_text_menu(ui,
	                   rect.x + 10,
	                   rect.y + (rect.h - text_h) / 2,
	                   label,
	                   enabled ? text : text_disabled);
}

static void sdl_draw_input_box(TilemSdlUi *ui, SDL_Rect rect,
                               const char *text,
                               gboolean active, gboolean enabled)
{
	SDL_Color border = { 90, 90, 90, 255 };
	SDL_Color fill = { 40, 40, 40, 255 };
	SDL_Color active_fill = { 60, 60, 60, 255 };
	SDL_Color disabled = { 30, 30, 30, 255 };
	SDL_Color text_color = { 230, 230, 230, 255 };
	SDL_Color text_disabled = { 150, 150, 150, 255 };
	int text_h = sdl_pref_text_height(ui);

	SDL_SetRenderDrawColor(ui->renderer,
	                       enabled
	                           ? (active ? active_fill.r : fill.r)
	                           : disabled.r,
	                       enabled
	                           ? (active ? active_fill.g : fill.g)
	                           : disabled.g,
	                       enabled
	                           ? (active ? active_fill.b : fill.b)
	                           : disabled.b,
	                       255);
	SDL_RenderFillRect(ui->renderer, &rect);
	SDL_SetRenderDrawColor(ui->renderer, border.r, border.g,
	                       border.b, border.a);
	SDL_RenderDrawRect(ui->renderer, &rect);
	sdl_draw_text_menu(ui,
	                   rect.x + 6,
	                   rect.y + (rect.h - text_h) / 2,
	                   text ? text : "",
	                   enabled ? text_color : text_disabled);
}

static void sdl_render_screenshot_dialog(TilemSdlUi *ui)
{
	TilemSdlScreenshotDialog *dlg;
	TilemSdlScreenshotLayout layout;
	SDL_Color overlay = { 0, 0, 0, 150 };
	SDL_Color panel = { 45, 45, 45, 240 };
	SDL_Color border = { 90, 90, 90, 255 };
	SDL_Color text = { 230, 230, 230, 255 };
	SDL_Color dim = { 160, 160, 160, 255 };
	SDL_Color highlight = { 70, 120, 180, 120 };
	SDL_Rect preview_inner;
	SDL_Rect dst;
	char buf[64];
	char size_label[32];
	int text_h;
	int count;
	const TilemSdlScreenshotSize *sizes;
	gboolean can_interact;
	gboolean can_save;
	gboolean can_grab;
	gboolean can_record;
	gboolean can_stop;
	gboolean is_recording;

	if (!ui)
		return;
	dlg = &ui->screenshot_dialog;
	if (!dlg->visible)
		return;

	sdl_screenshot_dialog_layout(ui, &layout);
	text_h = layout.text_h;

	SDL_SetRenderDrawBlendMode(ui->renderer, SDL_BLENDMODE_BLEND);
	SDL_SetRenderDrawColor(ui->renderer, overlay.r, overlay.g,
	                       overlay.b, overlay.a);
	SDL_RenderFillRect(ui->renderer, NULL);

	SDL_SetRenderDrawColor(ui->renderer, panel.r, panel.g,
	                       panel.b, panel.a);
	SDL_RenderFillRect(ui->renderer, &layout.panel);
	SDL_SetRenderDrawColor(ui->renderer, border.r, border.g,
	                       border.b, border.a);
	SDL_RenderDrawRect(ui->renderer, &layout.panel);

	sdl_draw_text_menu(ui,
	                   layout.panel.x + layout.padding,
	                   layout.panel.y + layout.padding - 2,
	                   "Screenshot",
	                   text);

	preview_inner = layout.preview_rect;
	SDL_SetRenderDrawColor(ui->renderer, 30, 30, 30, 255);
	SDL_RenderFillRect(ui->renderer, &preview_inner);
	SDL_SetRenderDrawColor(ui->renderer, border.r, border.g,
	                       border.b, border.a);
	SDL_RenderDrawRect(ui->renderer, &preview_inner);

	sdl_screenshot_dialog_update_preview(ui);
	if (dlg->preview_texture && dlg->preview_width > 0
	    && dlg->preview_height > 0) {
		double scale_x = (double) preview_inner.w / dlg->preview_width;
		double scale_y = (double) preview_inner.h / dlg->preview_height;
		double scale = MIN(scale_x, scale_y);
		if (scale <= 0.0)
			scale = 1.0;
		dst.w = (int) (dlg->preview_width * scale);
		dst.h = (int) (dlg->preview_height * scale);
		dst.x = preview_inner.x + (preview_inner.w - dst.w) / 2;
		dst.y = preview_inner.y + (preview_inner.h - dst.h) / 2;
		SDL_RenderCopy(ui->renderer, dlg->preview_texture, NULL, &dst);
	}
	else {
		sdl_draw_text_menu(ui,
		                   preview_inner.x + 10,
		                   preview_inner.y + 10,
		                   "No preview",
		                   dim);
	}

	can_interact = ui->emu && ui->emu->calc;
	is_recording = can_interact && ui->emu->anim;
	can_grab = can_interact && !is_recording;
	can_record = can_interact && !is_recording;
	can_stop = can_interact && is_recording;
	can_save = (dlg->current_anim != NULL) && !is_recording;

	sdl_draw_button(ui, layout.grab_rect, "Grab",
	                dlg->hover == SDL_SS_ITEM_GRAB, can_grab);
	sdl_draw_button(ui, layout.record_rect, "Record",
	                dlg->hover == SDL_SS_ITEM_RECORD, can_record);
	sdl_draw_button(ui, layout.stop_rect, "Stop",
	                dlg->hover == SDL_SS_ITEM_STOP, can_stop);

	if (dlg->hover == SDL_SS_ITEM_GRAYSCALE) {
		SDL_SetRenderDrawColor(ui->renderer, highlight.r,
		                       highlight.g, highlight.b,
		                       highlight.a);
		SDL_RenderFillRect(ui->renderer, &layout.grayscale_rect);
	}
	sdl_draw_checkbox(ui->renderer,
	                  (SDL_Rect) { layout.grayscale_rect.x + 4,
	                               layout.grayscale_rect.y
	                                   + (layout.grayscale_rect.h - 16) / 2,
	                               16, 16 },
	                  dlg->grayscale,
	                  border,
	                  (SDL_Color) { 30, 30, 30, 255 },
	                  (SDL_Color) { 210, 210, 210, 255 });
	sdl_draw_text_menu(ui,
	                   layout.grayscale_rect.x + 28,
	                   layout.grayscale_rect.y
	                       + (layout.grayscale_rect.h - text_h) / 2,
	                   "Grayscale",
	                   text);

	if (dlg->hover == SDL_SS_ITEM_SIZE) {
		SDL_SetRenderDrawColor(ui->renderer, highlight.r,
		                       highlight.g, highlight.b,
		                       highlight.a);
		SDL_RenderFillRect(ui->renderer, &layout.size_rect);
	}
	sdl_draw_text_menu(ui,
	                   layout.size_rect.x,
	                   layout.size_rect.y
	                       + (layout.size_rect.h - text_h) / 2,
	                   "Image size:",
	                   text);
	sizes = sdl_screenshot_sizes_for_calc(ui, &count);
	sdl_screenshot_size_label(sizes, count, dlg->selected_size,
	                          size_label, sizeof(size_label));
	sdl_draw_input_box(ui,
	                   (SDL_Rect) {
	                       layout.size_rect.x + 120,
	                       layout.size_rect.y,
	                       layout.size_rect.w - 120,
	                       layout.size_rect.h
	                   },
	                   size_label,
	                   dlg->size_menu_open,
	                   TRUE);

	sdl_draw_text_menu(ui,
	                   layout.width_rect.x,
	                   layout.width_rect.y
	                       + (layout.width_rect.h - text_h) / 2,
	                   "Width:",
	                   text);
	snprintf(buf, sizeof(buf), "%d", dlg->width);
	sdl_draw_input_box(ui,
	                   (SDL_Rect) {
	                       layout.width_rect.x + 80,
	                       layout.width_rect.y,
	                       layout.width_rect.w - 80,
	                       layout.width_rect.h
	                   },
	                   buf,
	                   dlg->input_mode == SDL_SS_INPUT_WIDTH,
	                   TRUE);

	sdl_draw_text_menu(ui,
	                   layout.height_rect.x,
	                   layout.height_rect.y
	                       + (layout.height_rect.h - text_h) / 2,
	                   "Height:",
	                   text);
	snprintf(buf, sizeof(buf), "%d", dlg->height);
	sdl_draw_input_box(ui,
	                   (SDL_Rect) {
	                       layout.height_rect.x + 80,
	                       layout.height_rect.y,
	                       layout.height_rect.w - 80,
	                       layout.height_rect.h
	                   },
	                   buf,
	                   dlg->input_mode == SDL_SS_INPUT_HEIGHT,
	                   TRUE);

	sdl_draw_text_menu(ui,
	                   layout.speed_rect.x,
	                   layout.speed_rect.y
	                       + (layout.speed_rect.h - text_h) / 2,
	                   "Animation speed:",
	                   text);
	snprintf(buf, sizeof(buf), "%.1f", dlg->speed);
	sdl_draw_input_box(ui,
	                   (SDL_Rect) {
	                       layout.speed_rect.x + 140,
	                       layout.speed_rect.y,
	                       layout.speed_rect.w - 140,
	                       layout.speed_rect.h
	                   },
	                   buf,
	                   dlg->input_mode == SDL_SS_INPUT_SPEED,
	                   TRUE);

	sdl_draw_text_menu(ui,
	                   layout.fg_label_rect.x,
	                   layout.fg_label_rect.y
	                       + (layout.fg_label_rect.h - text_h) / 2,
	                   "Foreground:",
	                   text);
	snprintf(buf, sizeof(buf), "%d", dlg->fg_r);
	sdl_draw_input_box(ui, layout.fg_r_rect, buf,
	                   dlg->input_mode == SDL_SS_INPUT_FG_R, TRUE);
	snprintf(buf, sizeof(buf), "%d", dlg->fg_g);
	sdl_draw_input_box(ui, layout.fg_g_rect, buf,
	                   dlg->input_mode == SDL_SS_INPUT_FG_G, TRUE);
	snprintf(buf, sizeof(buf), "%d", dlg->fg_b);
	sdl_draw_input_box(ui, layout.fg_b_rect, buf,
	                   dlg->input_mode == SDL_SS_INPUT_FG_B, TRUE);
	SDL_SetRenderDrawColor(ui->renderer, dlg->fg_r, dlg->fg_g,
	                       dlg->fg_b, 255);
	SDL_RenderFillRect(ui->renderer, &layout.fg_color_rect);
	SDL_SetRenderDrawColor(ui->renderer, border.r, border.g,
	                       border.b, border.a);
	SDL_RenderDrawRect(ui->renderer, &layout.fg_color_rect);

	sdl_draw_text_menu(ui,
	                   layout.bg_label_rect.x,
	                   layout.bg_label_rect.y
	                       + (layout.bg_label_rect.h - text_h) / 2,
	                   "Background:",
	                   text);
	snprintf(buf, sizeof(buf), "%d", dlg->bg_r);
	sdl_draw_input_box(ui, layout.bg_r_rect, buf,
	                   dlg->input_mode == SDL_SS_INPUT_BG_R, TRUE);
	snprintf(buf, sizeof(buf), "%d", dlg->bg_g);
	sdl_draw_input_box(ui, layout.bg_g_rect, buf,
	                   dlg->input_mode == SDL_SS_INPUT_BG_G, TRUE);
	snprintf(buf, sizeof(buf), "%d", dlg->bg_b);
	sdl_draw_input_box(ui, layout.bg_b_rect, buf,
	                   dlg->input_mode == SDL_SS_INPUT_BG_B, TRUE);
	SDL_SetRenderDrawColor(ui->renderer, dlg->bg_r, dlg->bg_g,
	                       dlg->bg_b, 255);
	SDL_RenderFillRect(ui->renderer, &layout.bg_color_rect);
	SDL_SetRenderDrawColor(ui->renderer, border.r, border.g,
	                       border.b, border.a);
	SDL_RenderDrawRect(ui->renderer, &layout.bg_color_rect);

	sdl_draw_button(ui, layout.save_rect, "Save",
	                dlg->hover == SDL_SS_ITEM_SAVE, can_save);
	sdl_draw_button(ui, layout.cancel_rect, "Cancel",
	                dlg->hover == SDL_SS_ITEM_CANCEL, TRUE);

	if (dlg->size_menu_open) {
		SDL_Rect menu_rect;
		int item_h = layout.row_h;
		int i;
		menu_rect.x = layout.size_rect.x + 120;
		menu_rect.y = layout.size_rect.y + layout.size_rect.h + 2;
		menu_rect.w = layout.size_rect.w - 120;
		menu_rect.h = (count + 1) * item_h + SDL_MENU_SPACING * 2;

		SDL_SetRenderDrawColor(ui->renderer, 40, 40, 40, 240);
		SDL_RenderFillRect(ui->renderer, &menu_rect);
		SDL_SetRenderDrawColor(ui->renderer, border.r, border.g,
		                       border.b, border.a);
		SDL_RenderDrawRect(ui->renderer, &menu_rect);

		for (i = 0; i <= count; i++) {
			SDL_Rect item = {
				menu_rect.x,
				menu_rect.y + SDL_MENU_SPACING + i * item_h,
				menu_rect.w,
				item_h
			};
			if (i == dlg->size_menu_hover) {
				SDL_SetRenderDrawColor(ui->renderer,
				                       highlight.r, highlight.g,
				                       highlight.b, highlight.a);
				SDL_RenderFillRect(ui->renderer, &item);
			}
			sdl_screenshot_size_label(sizes, count, i,
			                          size_label,
			                          sizeof(size_label));
			sdl_draw_text_menu(ui,
			                   item.x + 6,
			                   item.y + (item.h - text_h) / 2,
			                   size_label,
			                   text);
		}
	}

	SDL_SetRenderDrawBlendMode(ui->renderer, SDL_BLENDMODE_NONE);
}

static gboolean sdl_screenshot_dialog_handle_event(TilemSdlUi *ui,
                                                   const SDL_Event *event)
{
	TilemSdlScreenshotDialog *dlg;
	TilemSdlScreenshotLayout layout;
	TilemSdlScreenshotItem hit;
	const TilemSdlScreenshotSize *sizes;
	SDL_Rect menu_rect;
	int count;
	int item_h;
	int idx;

	if (!ui || !event)
		return FALSE;
	dlg = &ui->screenshot_dialog;
	if (!dlg->visible)
		return FALSE;

	sdl_screenshot_dialog_layout(ui, &layout);

	switch (event->type) {
	case SDL_MOUSEMOTION:
		dlg->hover = sdl_screenshot_dialog_hit_test(
			ui, &layout, event->motion.x, event->motion.y);
		if (dlg->size_menu_open) {
			sizes = sdl_screenshot_sizes_for_calc(ui, &count);
			item_h = layout.row_h;
			menu_rect.x = layout.size_rect.x + 120;
			menu_rect.y = layout.size_rect.y + layout.size_rect.h + 2;
			menu_rect.w = layout.size_rect.w - 120;
			menu_rect.h = (count + 1) * item_h + SDL_MENU_SPACING * 2;

			if (sdl_point_in_rect(event->motion.x,
			                      event->motion.y,
			                      &menu_rect)) {
				idx = (event->motion.y - menu_rect.y
				       - SDL_MENU_SPACING) / item_h;
				if (idx >= 0 && idx <= count)
					dlg->size_menu_hover = idx;
				else
					dlg->size_menu_hover = -1;
			}
			else {
				dlg->size_menu_hover = -1;
			}
		}
		return TRUE;
	case SDL_MOUSEBUTTONDOWN:
		if (event->button.button != SDL_BUTTON_LEFT) {
			return TRUE;
		}
		if (!sdl_point_in_rect(event->button.x,
		                       event->button.y,
		                       &layout.panel)) {
			sdl_screenshot_dialog_close(ui);
			return TRUE;
		}
		if (dlg->size_menu_open) {
			sizes = sdl_screenshot_sizes_for_calc(ui, &count);
			item_h = layout.row_h;
			menu_rect.x = layout.size_rect.x + 120;
			menu_rect.y = layout.size_rect.y + layout.size_rect.h + 2;
			menu_rect.w = layout.size_rect.w - 120;
			menu_rect.h = (count + 1) * item_h + SDL_MENU_SPACING * 2;
			if (sdl_point_in_rect(event->button.x,
			                      event->button.y,
			                      &menu_rect)) {
				idx = (event->button.y - menu_rect.y
				       - SDL_MENU_SPACING) / item_h;
				if (idx >= 0 && idx <= count) {
					dlg->selected_size = idx;
					if (idx < count) {
						sdl_screenshot_set_size(
							ui,
							sizes[idx].width,
							sizes[idx].height);
					}
					dlg->size_menu_open = FALSE;
					dlg->size_menu_hover = -1;
				}
				return TRUE;
			}
			dlg->size_menu_open = FALSE;
			dlg->size_menu_hover = -1;
		}

		hit = sdl_screenshot_dialog_hit_test(
			ui, &layout, event->button.x, event->button.y);
		switch (hit) {
		case SDL_SS_ITEM_GRAB:
			if (!ui->emu->anim)
				sdl_screenshot_dialog_grab(ui);
			break;
		case SDL_SS_ITEM_RECORD:
			sdl_screenshot_dialog_record_start(ui);
			break;
		case SDL_SS_ITEM_STOP:
			sdl_screenshot_dialog_record_stop(ui);
			break;
		case SDL_SS_ITEM_GRAYSCALE:
			dlg->grayscale = !dlg->grayscale;
			break;
		case SDL_SS_ITEM_SIZE:
			dlg->size_menu_open = !dlg->size_menu_open;
			dlg->size_menu_hover = -1;
			break;
		case SDL_SS_ITEM_WIDTH:
			snprintf(dlg->input_buf, sizeof(dlg->input_buf),
			         "%d", dlg->width);
			sdl_screenshot_dialog_start_input(
				dlg, SDL_SS_INPUT_WIDTH, dlg->input_buf);
			break;
		case SDL_SS_ITEM_HEIGHT:
			snprintf(dlg->input_buf, sizeof(dlg->input_buf),
			         "%d", dlg->height);
			sdl_screenshot_dialog_start_input(
				dlg, SDL_SS_INPUT_HEIGHT, dlg->input_buf);
			break;
		case SDL_SS_ITEM_SPEED:
			snprintf(dlg->input_buf, sizeof(dlg->input_buf),
			         "%.1f", dlg->speed);
			sdl_screenshot_dialog_start_input(
				dlg, SDL_SS_INPUT_SPEED, dlg->input_buf);
			break;
		case SDL_SS_ITEM_FG_R:
			snprintf(dlg->input_buf, sizeof(dlg->input_buf),
			         "%d", dlg->fg_r);
			sdl_screenshot_dialog_start_input(
				dlg, SDL_SS_INPUT_FG_R, dlg->input_buf);
			break;
		case SDL_SS_ITEM_FG_G:
			snprintf(dlg->input_buf, sizeof(dlg->input_buf),
			         "%d", dlg->fg_g);
			sdl_screenshot_dialog_start_input(
				dlg, SDL_SS_INPUT_FG_G, dlg->input_buf);
			break;
		case SDL_SS_ITEM_FG_B:
			snprintf(dlg->input_buf, sizeof(dlg->input_buf),
			         "%d", dlg->fg_b);
			sdl_screenshot_dialog_start_input(
				dlg, SDL_SS_INPUT_FG_B, dlg->input_buf);
			break;
		case SDL_SS_ITEM_BG_R:
			snprintf(dlg->input_buf, sizeof(dlg->input_buf),
			         "%d", dlg->bg_r);
			sdl_screenshot_dialog_start_input(
				dlg, SDL_SS_INPUT_BG_R, dlg->input_buf);
			break;
		case SDL_SS_ITEM_BG_G:
			snprintf(dlg->input_buf, sizeof(dlg->input_buf),
			         "%d", dlg->bg_g);
			sdl_screenshot_dialog_start_input(
				dlg, SDL_SS_INPUT_BG_G, dlg->input_buf);
			break;
		case SDL_SS_ITEM_BG_B:
			snprintf(dlg->input_buf, sizeof(dlg->input_buf),
			         "%d", dlg->bg_b);
			sdl_screenshot_dialog_start_input(
				dlg, SDL_SS_INPUT_BG_B, dlg->input_buf);
			break;
		case SDL_SS_ITEM_SAVE:
			if (dlg->current_anim && !ui->emu->anim)
				sdl_screenshot_dialog_save(ui);
			break;
		case SDL_SS_ITEM_CANCEL:
			sdl_screenshot_dialog_close(ui);
			break;
		default:
			break;
		}
		return TRUE;
	case SDL_KEYDOWN:
		if (dlg->size_menu_open
		    && event->key.keysym.sym == SDLK_ESCAPE) {
			dlg->size_menu_open = FALSE;
			return TRUE;
		}
		if (dlg->input_mode != SDL_SS_INPUT_NONE) {
			if (event->key.keysym.sym == SDLK_RETURN
			    || event->key.keysym.sym == SDLK_KP_ENTER) {
				sdl_screenshot_dialog_apply_input(ui);
				return TRUE;
			}
			if (event->key.keysym.sym == SDLK_ESCAPE) {
				sdl_screenshot_dialog_cancel_input(dlg);
				return TRUE;
			}
			if (event->key.keysym.sym == SDLK_BACKSPACE) {
				if (dlg->input_len > 0) {
					dlg->input_len--;
					dlg->input_buf[dlg->input_len] = '\0';
				}
				return TRUE;
			}
		}
		if (event->key.keysym.sym == SDLK_ESCAPE) {
			sdl_screenshot_dialog_close(ui);
			return TRUE;
		}
		if (event->key.keysym.sym == SDLK_RETURN
		    || event->key.keysym.sym == SDLK_KP_ENTER) {
			if (dlg->current_anim && !ui->emu->anim)
				sdl_screenshot_dialog_save(ui);
			return TRUE;
		}
		return TRUE;
	case SDL_TEXTINPUT:
		if (dlg->input_mode == SDL_SS_INPUT_NONE)
			return TRUE;
		if (dlg->input_len >= (int) sizeof(dlg->input_buf) - 1)
			return TRUE;
		if (dlg->input_mode == SDL_SS_INPUT_SPEED) {
			if (event->text.text[0] != '.'
			    && !g_ascii_isdigit(event->text.text[0]))
				return TRUE;
		}
		else if (!g_ascii_isdigit(event->text.text[0])) {
			return TRUE;
		}
		dlg->input_buf[dlg->input_len] = event->text.text[0];
		dlg->input_len++;
		dlg->input_buf[dlg->input_len] = '\0';
		return TRUE;
	default:
		break;
	}

	return TRUE;
}

static int sdl_string_to_slot(const char *str)
{
	if (!str)
		return TI81_SLOT_AUTO;

	if (!g_ascii_strncasecmp(str, "prgm", 4))
		str += 4;
	else if (!g_ascii_strncasecmp(str, "ti81_", 5))
		str += 5;
	else
		return TI81_SLOT_AUTO;

	if (g_ascii_isdigit(str[0]) && !g_ascii_isalnum(str[1]))
		return TI81_SLOT_0 + str[0] - '0';
	if (g_ascii_isalpha(str[0]) && !g_ascii_isalnum(str[1]))
		return TI81_SLOT_A + g_ascii_toupper(str[0]) - 'A';
	if (str[0] == '@'
	    || !g_ascii_strncasecmp(str, "theta", 5)
	    || !strncmp(str, "\316\270", 2)
	    || !strncmp(str, "\316\230", 2))
		return TI81_SLOT_THETA;

	return TI81_SLOT_AUTO;
}

static int sdl_guess_slot(const char *filename)
{
	char *base;
	int slot;

	base = g_filename_display_basename(filename);
	slot = sdl_string_to_slot(base);
	g_free(base);
	return slot;
}

static int sdl_display_index_to_slot(int i)
{
	if (i < 9)
		return i + 1;
	if (i == 9)
		return 0;
	return i;
}

static int sdl_slot_order[TI81_SLOT_MAX + 1];
static gboolean sdl_slot_order_ready = FALSE;

static void sdl_slot_init_order(void)
{
	int i;

	if (sdl_slot_order_ready)
		return;

	for (i = 0; i <= TI81_SLOT_MAX; i++)
		sdl_slot_order[i] = sdl_display_index_to_slot(i);
	sdl_slot_order_ready = TRUE;
}

static int sdl_slot_next(int slot, int dir)
{
	int i;
	int n = TI81_SLOT_MAX + 1;

	sdl_slot_init_order();
	for (i = 0; i < n; i++) {
		if (sdl_slot_order[i] == slot)
			break;
	}
	if (i == n)
		return slot;

	i = (i + dir + n) % n;
	return sdl_slot_order[i];
}

static int sdl_slot_from_key(SDL_Keycode sym)
{
	if (sym >= SDLK_0 && sym <= SDLK_9)
		return TI81_SLOT_0 + (sym - SDLK_0);
	if (sym >= SDLK_a && sym <= SDLK_z)
		return TI81_SLOT_A + (sym - SDLK_a);
	if (sym == SDLK_AT)
		return TI81_SLOT_THETA;
	return TI81_SLOT_AUTO;
}

static char *sdl_slot_label(const TI81ProgInfo *info, int slot)
{
	char *slotstr;
	char *namestr = NULL;
	char *label;

	slotstr = ti81_program_slot_to_string(slot);
	if (!slotstr)
		return g_strdup("");

	if (info && info->size != 0)
		namestr = ti81_program_name_to_string(info->name);

	if (namestr && namestr[0])
		label = g_strdup_printf("%s (in use: %s)", slotstr, namestr);
	else if (info && info->size != 0)
		label = g_strdup_printf("%s (in use)", slotstr);
	else
		label = g_strdup(slotstr);

	g_free(slotstr);
	g_free(namestr);
	return label;
}

static void sdl_send_files(TilemSdlUi *ui, char **filenames, int *slots)
{
	int i;

	if (!ui || !ui->emu || !ui->emu->calc || !filenames)
		return;

	for (i = 0; filenames[i]; i++) {
		tilem_link_send_file(ui->emu, filenames[i],
		                     slots ? slots[i] : -1,
		                     (i == 0),
		                     (filenames[i + 1] == NULL));
		if (ui->emu->isMacroRecording)
			tilem_macro_add_action(ui->emu->macro, 1, filenames[i]);
	}
}

static void sdl_slot_dialog_clear(TilemSdlUi *ui)
{
	if (!ui)
		return;

	if (ui->slot_dialog.filenames)
		g_strfreev(ui->slot_dialog.filenames);
	g_free(ui->slot_dialog.slots);

	ui->slot_dialog.filenames = NULL;
	ui->slot_dialog.slots = NULL;
	ui->slot_dialog.nfiles = 0;
	ui->slot_dialog.selected = 0;
	ui->slot_dialog.top = 0;
	ui->slot_dialog.visible = FALSE;
}

static void sdl_slot_dialog_assign_defaults(TilemSdlSlotDialog *dlg)
{
	int used[TI81_SLOT_MAX + 1];
	int i, j, slot;

	memset(used, 0, sizeof(used));
	for (i = 0; i <= TI81_SLOT_MAX; i++) {
		if (dlg->info[i].size != 0)
			used[i] = 1;
	}

	for (i = 0; i < dlg->nfiles; i++) {
		slot = sdl_guess_slot(dlg->filenames[i]);
		if (dlg->slots[i] < 0)
			dlg->slots[i] = slot;
		if (slot >= 0)
			used[slot] = 1;
	}

	for (i = 0; i < dlg->nfiles; i++) {
		if (dlg->slots[i] < 0) {
			for (j = 0; j <= TI81_SLOT_MAX; j++) {
				slot = sdl_display_index_to_slot(j);
				if (!used[slot]) {
					dlg->slots[i] = slot;
					used[slot] = 1;
					break;
				}
			}
		}
		if (dlg->slots[i] < 0)
			dlg->slots[i] = TI81_SLOT_1;
	}
}

static void sdl_slot_dialog_layout(TilemSdlUi *ui,
                                   TilemSdlSlotLayout *layout)
{
	int padding = SDL_MENU_PADDING * 2;
	int row_h = sdl_menu_item_height(ui);
	int title_h = row_h;
	int button_h = row_h + 8;
	int button_w = sdl_menu_text_width(ui, "Cancel") + 20;
	int list_rows = ui->slot_dialog.nfiles;
	int list_h;
	int panel_w;
	int panel_h;
	int max_h;
	int buttons_y;

	if (list_rows < 1)
		list_rows = 1;
	if (list_rows > 10)
		list_rows = 10;

	list_h = (list_rows + 1) * row_h;
	panel_w = ui->window_width - padding * 2;
	if (panel_w > 640)
		panel_w = 640;
	panel_h = list_h + title_h + button_h + padding * 4;
	max_h = ui->window_height - padding * 2;
	if (panel_h > max_h)
		panel_h = max_h;

	layout->padding = padding;
	layout->row_h = row_h;
	layout->text_h = sdl_pref_text_height(ui);
	layout->panel.w = panel_w;
	layout->panel.h = panel_h;
	layout->panel.x = (ui->window_width - panel_w) / 2;
	layout->panel.y = (ui->window_height - panel_h) / 2;

	layout->list_rect.x = layout->panel.x + padding;
	layout->list_rect.y = layout->panel.y + padding + title_h + padding / 2;
	layout->list_rect.w = panel_w - padding * 2;
	layout->list_rect.h = panel_h - (title_h + button_h + padding * 3);

	buttons_y = layout->panel.y + panel_h - padding - button_h;
	layout->send_rect = (SDL_Rect) {
		layout->panel.x + panel_w - padding - button_w * 2 - 10,
		buttons_y, button_w, button_h
	};
	layout->cancel_rect = (SDL_Rect) {
		layout->panel.x + panel_w - padding - button_w,
		buttons_y, button_w, button_h
	};
}

static int sdl_slot_dialog_visible_rows(const TilemSdlSlotLayout *layout)
{
	int rows = layout->list_rect.h / layout->row_h - 1;
	if (rows < 1)
		rows = 1;
	return rows;
}

static void sdl_slot_dialog_commit(TilemSdlUi *ui)
{
	if (!ui || !ui->slot_dialog.visible)
		return;

	sdl_send_files(ui, ui->slot_dialog.filenames, ui->slot_dialog.slots);
	sdl_slot_dialog_clear(ui);
}

static gboolean sdl_slot_dialog_handle_event(TilemSdlUi *ui,
                                             const SDL_Event *event)
{
	TilemSdlSlotLayout layout;
	int rows;

	if (!ui || !ui->slot_dialog.visible || !event)
		return FALSE;

	sdl_slot_dialog_layout(ui, &layout);
	rows = sdl_slot_dialog_visible_rows(&layout);

	switch (event->type) {
	case SDL_MOUSEBUTTONDOWN:
		if (event->button.button != SDL_BUTTON_LEFT)
			return TRUE;
		if (!sdl_point_in_rect(event->button.x,
		                       event->button.y,
		                       &layout.panel)) {
			sdl_slot_dialog_clear(ui);
			return TRUE;
		}
		if (sdl_point_in_rect(event->button.x,
		                      event->button.y,
		                      &layout.send_rect)) {
			sdl_slot_dialog_commit(ui);
			return TRUE;
		}
		if (sdl_point_in_rect(event->button.x,
		                      event->button.y,
		                      &layout.cancel_rect)) {
			sdl_slot_dialog_clear(ui);
			return TRUE;
		}
		if (sdl_point_in_rect(event->button.x,
		                      event->button.y,
		                      &layout.list_rect)) {
			int rel_y = event->button.y - layout.list_rect.y;
			int idx = rel_y / layout.row_h - 1;
			int row = ui->slot_dialog.top + idx;

			if (idx >= 0 && row >= 0
			    && row < ui->slot_dialog.nfiles) {
				ui->slot_dialog.selected = row;
				if (event->button.clicks >= 2)
					sdl_slot_dialog_commit(ui);
			}
		}
		return TRUE;
	case SDL_MOUSEWHEEL:
		if (event->wheel.y > 0) {
			ui->slot_dialog.top = MAX(0,
			                          ui->slot_dialog.top - 1);
		} else if (event->wheel.y < 0) {
			ui->slot_dialog.top = MIN(
				MAX(0, ui->slot_dialog.nfiles - rows),
				ui->slot_dialog.top + 1);
		}
		return TRUE;
	case SDL_KEYDOWN: {
		SDL_Keycode sym = event->key.keysym.sym;
		int slot;

		if (sym >= 'A' && sym <= 'Z')
			sym = sym - 'A' + 'a';

		switch (sym) {
		case SDLK_ESCAPE:
			sdl_slot_dialog_clear(ui);
			return TRUE;
		case SDLK_RETURN:
		case SDLK_KP_ENTER:
			sdl_slot_dialog_commit(ui);
			return TRUE;
		case SDLK_UP:
			if (ui->slot_dialog.selected > 0)
				ui->slot_dialog.selected--;
			if (ui->slot_dialog.selected < ui->slot_dialog.top)
				ui->slot_dialog.top = ui->slot_dialog.selected;
			return TRUE;
		case SDLK_DOWN:
			if (ui->slot_dialog.selected
			    < ui->slot_dialog.nfiles - 1)
				ui->slot_dialog.selected++;
			if (ui->slot_dialog.selected
			    >= ui->slot_dialog.top + rows) {
				ui->slot_dialog.top =
					ui->slot_dialog.selected - rows + 1;
			}
			return TRUE;
		case SDLK_LEFT:
			ui->slot_dialog.slots[ui->slot_dialog.selected] =
				sdl_slot_next(
					ui->slot_dialog.slots[
						ui->slot_dialog.selected],
					-1);
			return TRUE;
		case SDLK_RIGHT:
			ui->slot_dialog.slots[ui->slot_dialog.selected] =
				sdl_slot_next(
					ui->slot_dialog.slots[
						ui->slot_dialog.selected],
					1);
			return TRUE;
		default:
			slot = sdl_slot_from_key(sym);
			if (slot >= 0) {
				ui->slot_dialog.slots[
					ui->slot_dialog.selected] = slot;
				return TRUE;
			}
		}
		return TRUE;
	}
	default:
		break;
	}

	return FALSE;
}

static void sdl_render_slot_dialog(TilemSdlUi *ui)
{
	TilemSdlSlotLayout layout;
	SDL_Color overlay = { 0, 0, 0, 150 };
	SDL_Color panel = { 45, 45, 45, 240 };
	SDL_Color border = { 90, 90, 90, 255 };
	SDL_Color text = { 230, 230, 230, 255 };
	SDL_Color dim = { 160, 160, 160, 255 };
	SDL_Color highlight = { 70, 120, 180, 120 };
	SDL_Color button_fill = { 70, 70, 70, 255 };
	int rows;
	int i;
	int start_y;
	int header_y;
	int col_file_x;
	int col_slot_x;
	int slot_w;

	if (!ui || !ui->slot_dialog.visible)
		return;

	sdl_slot_dialog_layout(ui, &layout);
	rows = sdl_slot_dialog_visible_rows(&layout);

	SDL_SetRenderDrawBlendMode(ui->renderer, SDL_BLENDMODE_BLEND);
	SDL_SetRenderDrawColor(ui->renderer, overlay.r, overlay.g,
	                       overlay.b, overlay.a);
	SDL_RenderFillRect(ui->renderer, NULL);

	SDL_SetRenderDrawColor(ui->renderer, panel.r, panel.g,
	                       panel.b, panel.a);
	SDL_RenderFillRect(ui->renderer, &layout.panel);
	SDL_SetRenderDrawColor(ui->renderer, border.r, border.g,
	                       border.b, border.a);
	SDL_RenderDrawRect(ui->renderer, &layout.panel);

	sdl_draw_text_menu(ui,
	                   layout.panel.x + layout.padding,
	                   layout.panel.y + layout.padding,
	                   "Select Program Slots",
	                   text);
	sdl_draw_text_menu(ui,
	                   layout.panel.x + layout.padding,
	                   layout.panel.y + layout.padding
	                       + layout.text_h + 4,
	                   "Choose a slot for each program.",
	                   dim);

	header_y = layout.list_rect.y;
	start_y = header_y + layout.row_h;
	slot_w = sdl_menu_text_width(ui, "PrgmM (in use)") + 20;
	col_file_x = layout.list_rect.x + 6;
	col_slot_x = layout.list_rect.x + layout.list_rect.w - slot_w;

	SDL_SetRenderDrawColor(ui->renderer, border.r, border.g,
	                       border.b, border.a);
	SDL_RenderDrawRect(ui->renderer, &layout.list_rect);

	sdl_draw_text_menu(ui, col_file_x,
	                   header_y + (layout.row_h - layout.text_h) / 2,
	                   "File", dim);
	sdl_draw_text_menu(ui, col_slot_x,
	                   header_y + (layout.row_h - layout.text_h) / 2,
	                   "Slot", dim);

	for (i = 0; i < rows; i++) {
		int row = ui->slot_dialog.top + i;
		SDL_Rect row_rect;
		char *base;
		char *slot_label;

		if (row >= ui->slot_dialog.nfiles)
			break;

		row_rect.x = layout.list_rect.x + 1;
		row_rect.y = start_y + i * layout.row_h;
		row_rect.w = layout.list_rect.w - 2;
		row_rect.h = layout.row_h;

		if (row == ui->slot_dialog.selected) {
			SDL_SetRenderDrawColor(ui->renderer, highlight.r,
			                       highlight.g, highlight.b,
			                       highlight.a);
			SDL_RenderFillRect(ui->renderer, &row_rect);
		}

		base = g_filename_display_basename(
			ui->slot_dialog.filenames[row]);
		slot_label = sdl_slot_label(&ui->slot_dialog.info[
			ui->slot_dialog.slots[row]],
			ui->slot_dialog.slots[row]);

		sdl_draw_text_menu(ui,
		                   col_file_x,
		                   row_rect.y
		                       + (layout.row_h - layout.text_h) / 2,
		                   base, text);
		sdl_draw_text_menu(ui,
		                   col_slot_x,
		                   row_rect.y
		                       + (layout.row_h - layout.text_h) / 2,
		                   slot_label, text);

		g_free(base);
		g_free(slot_label);
	}

	SDL_SetRenderDrawColor(ui->renderer, button_fill.r, button_fill.g,
	                       button_fill.b, 255);
	SDL_RenderFillRect(ui->renderer, &layout.send_rect);
	SDL_RenderFillRect(ui->renderer, &layout.cancel_rect);
	SDL_SetRenderDrawColor(ui->renderer, border.r, border.g,
	                       border.b, border.a);
	SDL_RenderDrawRect(ui->renderer, &layout.send_rect);
	SDL_RenderDrawRect(ui->renderer, &layout.cancel_rect);

	sdl_draw_text_menu(ui,
	                   layout.send_rect.x + 12,
	                   layout.send_rect.y
	                       + (layout.send_rect.h - layout.text_h) / 2,
	                   "Send",
	                   text);
	sdl_draw_text_menu(ui,
	                   layout.cancel_rect.x + 12,
	                   layout.cancel_rect.y
	                       + (layout.cancel_rect.h - layout.text_h) / 2,
	                   "Cancel",
	                   text);

	SDL_SetRenderDrawBlendMode(ui->renderer, SDL_BLENDMODE_NONE);
}

static void sdl_receive_dialog_clear(TilemSdlUi *ui)
{
	TilemSdlReceiveDialog *dlg;
	int i;

	if (!ui)
		return;

	dlg = &ui->receive_dialog;
	if (dlg->entries) {
		for (i = 0; i < (int) dlg->entries->len; i++) {
			TilemVarEntry *tve = g_ptr_array_index(dlg->entries, i);
			tilem_var_entry_free(tve);
		}
		g_ptr_array_free(dlg->entries, TRUE);
	}
	if (dlg->selected_flags)
		g_array_free(dlg->selected_flags, TRUE);

	dlg->entries = NULL;
	dlg->selected_flags = NULL;
	dlg->selected = 0;
	dlg->top = 0;
	dlg->hover = -1;
	dlg->visible = FALSE;
	dlg->refresh_pending = FALSE;
}

static void sdl_receive_dialog_set_entries(TilemSdlUi *ui, GSList *list)
{
	TilemSdlReceiveDialog *dlg;
	GSList *l;

	dlg = &ui->receive_dialog;
	sdl_receive_dialog_clear(ui);

	dlg->entries = g_ptr_array_new();
	for (l = list; l; l = l->next)
		g_ptr_array_add(dlg->entries, l->data);
	g_slist_free(list);

	dlg->selected_flags = g_array_sized_new(FALSE, TRUE,
	                                        sizeof(gboolean),
	                                        dlg->entries->len);
	g_array_set_size(dlg->selected_flags, dlg->entries->len);
	memset(dlg->selected_flags->data, 0,
	       dlg->entries->len * sizeof(gboolean));

	dlg->selected = 0;
	dlg->top = 0;
	dlg->hover = -1;
	dlg->is_81 = (ui->emu->calc->hw.model_id == TILEM_CALC_TI81);
	dlg->use_group = FALSE;
	if (!dlg->is_81)
		tilem_config_get("download", "save_as_group/b=1",
		                 &dlg->use_group, NULL);
	dlg->visible = TRUE;
	dlg->refresh_pending = FALSE;
	ui->menu_visible = FALSE;
	ui->submenu_visible = FALSE;
	ui->prefs_visible = FALSE;
}

static void sdl_receive_dialog_update(G_GNUC_UNUSED TilemCalcEmulator *emu,
                                      GSList *list,
                                      const char *error_message,
                                      gpointer data)
{
	TilemSdlUi *ui = data;
	GSList *l;

	if (!ui)
		return;

	ui->receive_dialog.refresh_pending = FALSE;

	if (error_message && *error_message) {
		sdl_show_message(ui, "Receive File", error_message);
		for (l = list; l; l = l->next)
			tilem_var_entry_free(l->data);
		g_slist_free(list);
		return;
	}

	if (!list) {
		sdl_show_message(ui, "Receive File",
		                 "No variables to receive.");
		return;
	}

	sdl_receive_dialog_set_entries(ui, list);
}

static void sdl_receive_dialog_request(TilemSdlUi *ui)
{
	if (!ui || !ui->emu || !ui->emu->calc)
		return;

	if (ui->receive_dialog.refresh_pending)
		return;

	ui->receive_dialog.refresh_pending = TRUE;
	tilem_link_get_dirlist_with_callback(
		ui->emu, TRUE, sdl_receive_dialog_update, ui);
}

static void sdl_receive_dialog_layout(TilemSdlUi *ui,
                                      TilemSdlReceiveLayout *layout)
{
	int padding = SDL_MENU_PADDING * 2;
	int row_h = sdl_menu_item_height(ui);
	int title_h = row_h;
	int button_h = row_h + 8;
	int panel_w = ui->window_width - padding * 2;
	int panel_h = ui->window_height - padding * 2;
	int mode_h = ui->receive_dialog.is_81 ? 0 : row_h;
	int buttons_y;

	if (panel_w > 720)
		panel_w = 720;
	if (panel_h > 520)
		panel_h = 520;
	if (panel_h < row_h * 6 + padding * 2)
		panel_h = row_h * 6 + padding * 2;

	layout->padding = padding;
	layout->row_h = row_h;
	layout->header_h = row_h;
	layout->text_h = sdl_pref_text_height(ui);
	layout->panel.w = panel_w;
	layout->panel.h = panel_h;
	layout->panel.x = (ui->window_width - panel_w) / 2;
	layout->panel.y = (ui->window_height - panel_h) / 2;

	layout->list_rect.x = layout->panel.x + padding;
	layout->list_rect.y = layout->panel.y + padding + title_h + padding / 2;
	layout->list_rect.w = panel_w - padding * 2;
	layout->list_rect.h = panel_h - (title_h + button_h + mode_h
	                                 + padding * 3);

	buttons_y = layout->panel.y + panel_h - padding - button_h;
	layout->refresh_rect = (SDL_Rect) {
		layout->panel.x + padding,
		buttons_y,
		sdl_menu_text_width(ui, "Refresh") + 24,
		button_h
	};
	layout->cancel_rect = (SDL_Rect) {
		layout->panel.x + panel_w - padding
			- sdl_menu_text_width(ui, "Cancel") - 24,
		buttons_y,
		sdl_menu_text_width(ui, "Cancel") + 24,
		button_h
	};
	layout->save_rect = (SDL_Rect) {
		layout->cancel_rect.x
			- sdl_menu_text_width(ui, "Save") - 34,
		buttons_y,
		sdl_menu_text_width(ui, "Save") + 24,
		button_h
	};

	layout->mode_label_rect = (SDL_Rect) {
		layout->panel.x + padding,
		buttons_y - mode_h - 6,
		sdl_menu_text_width(ui, "Save as:"),
		row_h
	};
	layout->mode_sep_rect = (SDL_Rect) {
		layout->mode_label_rect.x + layout->mode_label_rect.w + 10,
		layout->mode_label_rect.y,
		sdl_menu_text_width(ui, "Separate files") + 24,
		row_h
	};
	layout->mode_group_rect = (SDL_Rect) {
		layout->mode_sep_rect.x + layout->mode_sep_rect.w + 10,
		layout->mode_sep_rect.y,
		sdl_menu_text_width(ui, "Group file") + 24,
		row_h
	};
}

static int sdl_receive_dialog_visible_rows(const TilemSdlReceiveLayout *layout)
{
	int rows = layout->list_rect.h / layout->row_h - 1;
	if (rows < 1)
		rows = 1;
	return rows;
}

static int sdl_receive_dialog_hit_row(const TilemSdlReceiveLayout *layout,
                                      int x, int y)
{
	int rel_y;
	int idx;

	if (!sdl_point_in_rect(x, y, &layout->list_rect))
		return -1;

	rel_y = y - layout->list_rect.y;
	idx = rel_y / layout->row_h - 1;
	return idx;
}

static gboolean sdl_receive_dialog_is_selected(TilemSdlReceiveDialog *dlg,
                                               int idx)
{
	if (!dlg->selected_flags)
		return FALSE;
	if (idx < 0 || idx >= (int) dlg->selected_flags->len)
		return FALSE;
	return g_array_index(dlg->selected_flags, gboolean, idx);
}

static void sdl_receive_dialog_set_selected(TilemSdlReceiveDialog *dlg,
                                            int idx, gboolean selected)
{
	if (!dlg->selected_flags)
		return;
	if (idx < 0 || idx >= (int) dlg->selected_flags->len)
		return;
	g_array_index(dlg->selected_flags, gboolean, idx) = selected;
}

static GSList *sdl_receive_dialog_selected_entries(TilemSdlReceiveDialog *dlg)
{
	GSList *list = NULL;
	int i;

	if (!dlg->entries || !dlg->selected_flags)
		return NULL;

	for (i = 0; i < (int) dlg->entries->len; i++) {
		if (g_array_index(dlg->selected_flags, gboolean, i))
			list = g_slist_prepend(list,
			                       g_ptr_array_index(dlg->entries,
			                                         i));
	}

	return g_slist_reverse(list);
}

static gboolean sdl_prompt_overwrite(TilemSdlUi *ui, const char *dirname,
                                     char **filenames)
{
	GString *conflicts = NULL;
	SDL_MessageBoxButtonData buttons[] = {
		{ SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 1, "Replace" },
		{ SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, "Cancel" }
	};
	SDL_MessageBoxData msg;
	int i;
	int response = 0;
	char *dname;
	char *message;

	if (!filenames || !dirname)
		return TRUE;

	for (i = 0; filenames[i]; i++) {
		if (g_file_test(filenames[i], G_FILE_TEST_EXISTS)) {
			char *base = g_filename_display_basename(
				filenames[i]);
			if (!conflicts)
				conflicts = g_string_new(NULL);
			else
				g_string_append_c(conflicts, '\n');
			g_string_append(conflicts, base);
			g_free(base);
		}
	}

	if (!conflicts)
		return TRUE;

	dname = g_filename_display_basename(dirname);
	message = g_strdup_printf("The following files already exist in \"%s\":\n%s",
	                          dname, conflicts->str);

	memset(&msg, 0, sizeof(msg));
	msg.flags = SDL_MESSAGEBOX_WARNING;
	msg.window = ui ? ui->window : NULL;
	msg.title = "Replace existing files?";
	msg.message = message;
	msg.numbuttons = 2;
	msg.buttons = buttons;

	SDL_ShowMessageBox(&msg, &response);

	g_free(message);
	g_free(dname);
	g_string_free(conflicts, TRUE);
	return response == 1;
}

static char *sdl_pick_receive_save_file(TilemSdlUi *ui,
                                        const char *title,
                                        const char *suggest_name)
{
	char *dir = NULL;
	char *filename;
	gboolean used_native = FALSE;

	tilem_config_get("download", "receivefile_recentdir/f", &dir, NULL);
	if (!dir)
		dir = g_get_current_dir();

	filename = sdl_native_file_dialog(title, dir, suggest_name,
	                                  TRUE, &used_native);
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

static char *sdl_pick_receive_dir(TilemSdlUi *ui)
{
	char *dir = NULL;
	char *selected;
	gboolean used_native = FALSE;

	tilem_config_get("download", "receivefile_recentdir/f", &dir, NULL);
	if (!dir)
		dir = g_get_current_dir();

	selected = sdl_native_folder_dialog("Save Files to Directory",
	                                    dir, &used_native);
	g_free(dir);

	if (!selected && !used_native) {
		sdl_show_message(ui, "Receive File",
		                 "No native folder picker available.");
	}

	if (selected) {
		tilem_config_set("download", "receivefile_recentdir/f",
		                 selected, NULL);
	}

	return selected;
}

static void sdl_receive_free_entries(GSList *entries)
{
	GSList *l;

	for (l = entries; l; l = l->next)
		tilem_var_entry_free(l->data);
	g_slist_free(entries);
}

static gboolean sdl_receive_save_single(TilemSdlUi *ui, TilemVarEntry *tve)
{
	char *default_filename;
	char *default_filename_f;
	char *filename;
	TilemVarEntry *copy;
	gboolean will_free;

	default_filename = get_default_filename(tve);
	default_filename_f = utf8_to_filename(default_filename);
	g_free(default_filename);

	filename = sdl_pick_receive_save_file(ui, "Save File",
	                                      default_filename_f);
	g_free(default_filename_f);

	if (!filename)
		return FALSE;

	copy = tilem_var_entry_copy(tve);
	will_free = (copy->ve && copy->ve->data);
	tilem_link_receive_file(ui->emu, copy, filename);
	if (!will_free)
		tilem_var_entry_free(copy);

	g_free(filename);
	return TRUE;
}

static gboolean sdl_receive_save_group(TilemSdlUi *ui, GSList *rows)
{
	GSList *l;
	GSList *entries = NULL;
	gboolean can_group = TRUE;
	char *fext;
	char *default_filename;
	char *filename;
	TilemVarEntry *first;
	gboolean will_free;
	CalcModel tfmodel;

	for (l = rows; l; l = l->next) {
		TilemVarEntry *tve = l->data;
		if (!tve->can_group)
			can_group = FALSE;
	}

	tfmodel = get_calc_model(ui->emu->calc);
	fext = g_ascii_strdown(tifiles_fext_of_group(tfmodel), -1);
	default_filename = g_strdup_printf("untitled.%s",
	                                   can_group ? fext : "tig");
	g_free(fext);

	filename = sdl_pick_receive_save_file(ui, "Save File",
	                                      default_filename);
	g_free(default_filename);
	if (!filename)
		return FALSE;

	for (l = rows; l; l = l->next)
		entries = g_slist_prepend(entries,
		                          tilem_var_entry_copy(l->data));
	entries = g_slist_reverse(entries);

	first = entries->data;
	will_free = (first->ve && first->ve->data);
	tilem_link_receive_group(ui->emu, entries, filename);
	if (!will_free)
		sdl_receive_free_entries(entries);

	tilem_config_set("download", "save_as_group/b",
	                 TRUE, NULL);
	g_free(filename);
	return TRUE;
}

static gboolean sdl_receive_save_multiple(TilemSdlUi *ui, GSList *rows,
                                          gboolean use_group)
{
	GSList *l;
	char *dir;
	char **names;
	int n;
	int i;

	if (use_group && !ui->receive_dialog.is_81)
		return sdl_receive_save_group(ui, rows);

	dir = sdl_pick_receive_dir(ui);
	if (!dir)
		return FALSE;

	n = g_slist_length(rows);
	names = g_new(char *, n + 1);

	for (l = rows, i = 0; l; l = l->next, i++) {
		TilemVarEntry *tve = l->data;
		char *default_filename = get_default_filename(tve);
		char *default_filename_f = utf8_to_filename(default_filename);
		g_free(default_filename);
		names[i] = g_build_filename(dir, default_filename_f, NULL);
		g_free(default_filename_f);
	}
	names[i] = NULL;

	if (!sdl_prompt_overwrite(ui, dir, names)) {
		for (i = 0; names[i]; i++)
			g_free(names[i]);
		g_free(names);
		g_free(dir);
		return FALSE;
	}

	for (l = rows, i = 0; l; l = l->next, i++) {
		TilemVarEntry *copy = tilem_var_entry_copy(l->data);
		gboolean will_free = (copy->ve && copy->ve->data);
		tilem_link_receive_file(ui->emu, copy, names[i]);
		if (!will_free)
			tilem_var_entry_free(copy);
	}

	tilem_config_set("download", "save_as_group/b",
	                 use_group, NULL);

	for (i = 0; names[i]; i++)
		g_free(names[i]);
	g_free(names);
	g_free(dir);
	return TRUE;
}

static void sdl_receive_dialog_save(TilemSdlUi *ui)
{
	TilemSdlReceiveDialog *dlg = &ui->receive_dialog;
	GSList *rows;
	int count;

	rows = sdl_receive_dialog_selected_entries(dlg);
	count = g_slist_length(rows);
	if (!rows || count == 0) {
		sdl_show_message(ui, "Receive File",
		                 "No variables selected.");
		g_slist_free(rows);
		return;
	}

	if (count == 1) {
		sdl_receive_save_single(ui, rows->data);
	} else {
		sdl_receive_save_multiple(ui, rows, dlg->use_group);
	}

	g_slist_free(rows);
}

static gboolean sdl_receive_dialog_handle_event(TilemSdlUi *ui,
                                                const SDL_Event *event)
{
	TilemSdlReceiveLayout layout;
	TilemSdlReceiveDialog *dlg;
	int rows;

	if (!ui || !ui->receive_dialog.visible || !event)
		return FALSE;

	dlg = &ui->receive_dialog;
	sdl_receive_dialog_layout(ui, &layout);
	rows = sdl_receive_dialog_visible_rows(&layout);

	switch (event->type) {
	case SDL_MOUSEBUTTONDOWN:
		if (event->button.button != SDL_BUTTON_LEFT)
			return TRUE;
		if (!sdl_point_in_rect(event->button.x,
		                       event->button.y,
		                       &layout.panel)) {
			sdl_receive_dialog_clear(ui);
			return TRUE;
		}
		if (!dlg->is_81
		    && sdl_point_in_rect(event->button.x,
		                         event->button.y,
		                         &layout.mode_sep_rect)) {
			dlg->use_group = FALSE;
			return TRUE;
		}
		if (!dlg->is_81
		    && sdl_point_in_rect(event->button.x,
		                         event->button.y,
		                         &layout.mode_group_rect)) {
			dlg->use_group = TRUE;
			return TRUE;
		}
		if (sdl_point_in_rect(event->button.x,
		                      event->button.y,
		                      &layout.refresh_rect)) {
			if (!dlg->is_81)
				sdl_receive_dialog_request(ui);
			return TRUE;
		}
		if (sdl_point_in_rect(event->button.x,
		                      event->button.y,
		                      &layout.save_rect)) {
			sdl_receive_dialog_save(ui);
			return TRUE;
		}
		if (sdl_point_in_rect(event->button.x,
		                      event->button.y,
		                      &layout.cancel_rect)) {
			sdl_receive_dialog_clear(ui);
			return TRUE;
		}
		if (sdl_point_in_rect(event->button.x,
		                      event->button.y,
		                      &layout.list_rect)) {
			int idx = sdl_receive_dialog_hit_row(
				&layout, event->button.x,
				event->button.y);
			int row = dlg->top + idx;

			if (idx >= 0 && row >= 0
			    && row < (int) dlg->entries->len) {
				dlg->selected = row;
				sdl_receive_dialog_set_selected(
					dlg, row,
					!sdl_receive_dialog_is_selected(
						dlg, row));
				if (event->button.clicks >= 2)
					sdl_receive_dialog_save(ui);
			}
		}
		return TRUE;
	case SDL_MOUSEWHEEL:
		if (event->wheel.y > 0) {
			dlg->top = MAX(0, dlg->top - 1);
		} else if (event->wheel.y < 0) {
			dlg->top = MIN(
				MAX(0,
				    (int) dlg->entries->len - rows),
				dlg->top + 1);
		}
		return TRUE;
	case SDL_KEYDOWN: {
		SDL_Keycode sym = event->key.keysym.sym;

		if (sym >= 'A' && sym <= 'Z')
			sym = sym - 'A' + 'a';

		switch (sym) {
		case SDLK_ESCAPE:
			sdl_receive_dialog_clear(ui);
			return TRUE;
		case SDLK_RETURN:
		case SDLK_KP_ENTER:
			sdl_receive_dialog_save(ui);
			return TRUE;
		case SDLK_UP:
			if (dlg->selected > 0)
				dlg->selected--;
			if (dlg->selected < dlg->top)
				dlg->top = dlg->selected;
			sdl_receive_dialog_set_selected(dlg,
			                                dlg->selected,
			                                TRUE);
			return TRUE;
		case SDLK_DOWN:
			if (dlg->selected < (int) dlg->entries->len - 1)
				dlg->selected++;
			if (dlg->selected >= dlg->top + rows)
				dlg->top = dlg->selected - rows + 1;
			sdl_receive_dialog_set_selected(dlg,
			                                dlg->selected,
			                                TRUE);
			return TRUE;
		case SDLK_SPACE:
			sdl_receive_dialog_set_selected(
				dlg, dlg->selected,
				!sdl_receive_dialog_is_selected(
					dlg, dlg->selected));
			return TRUE;
		case SDLK_a:
			if (event->key.keysym.mod & KMOD_CTRL) {
				int i;
				for (i = 0;
				     i < (int) dlg->entries->len; i++)
					sdl_receive_dialog_set_selected(
						dlg, i, TRUE);
			}
			return TRUE;
		default:
			break;
		}
		return TRUE;
	}
	default:
		break;
	}

	return FALSE;
}

static void sdl_render_receive_dialog(TilemSdlUi *ui)
{
	TilemSdlReceiveLayout layout;
	TilemSdlReceiveDialog *dlg;
	SDL_Color overlay = { 0, 0, 0, 150 };
	SDL_Color panel = { 45, 45, 45, 240 };
	SDL_Color border = { 90, 90, 90, 255 };
	SDL_Color text = { 230, 230, 230, 255 };
	SDL_Color dim = { 160, 160, 160, 255 };
	SDL_Color highlight = { 70, 120, 180, 120 };
	SDL_Color button_fill = { 70, 70, 70, 255 };
	int rows;
	int i;
	int header_y;
	int start_y;
	int checkbox_size;
	int col_x;
	int slot_w = 0;
	int type_w = 0;
	int size_w;

	if (!ui || !ui->receive_dialog.visible)
		return;

	dlg = &ui->receive_dialog;
	sdl_receive_dialog_layout(ui, &layout);
	rows = sdl_receive_dialog_visible_rows(&layout);
	checkbox_size = layout.row_h - 6;

	SDL_SetRenderDrawBlendMode(ui->renderer, SDL_BLENDMODE_BLEND);
	SDL_SetRenderDrawColor(ui->renderer, overlay.r, overlay.g,
	                       overlay.b, overlay.a);
	SDL_RenderFillRect(ui->renderer, NULL);

	SDL_SetRenderDrawColor(ui->renderer, panel.r, panel.g,
	                       panel.b, panel.a);
	SDL_RenderFillRect(ui->renderer, &layout.panel);
	SDL_SetRenderDrawColor(ui->renderer, border.r, border.g,
	                       border.b, border.a);
	SDL_RenderDrawRect(ui->renderer, &layout.panel);

	sdl_draw_text_menu(ui,
	                   layout.panel.x + layout.padding,
	                   layout.panel.y + layout.padding,
	                   "Receive File",
	                   text);

	SDL_SetRenderDrawColor(ui->renderer, border.r, border.g,
	                       border.b, border.a);
	SDL_RenderDrawRect(ui->renderer, &layout.list_rect);

	header_y = layout.list_rect.y;
	start_y = header_y + layout.row_h;

	col_x = layout.list_rect.x + 6 + checkbox_size + 6;
	if (dlg->is_81) {
		slot_w = sdl_menu_text_width(ui, "PrgmM ") + 8;
	}
	type_w = sdl_menu_text_width(ui, "MMMMMM ") + 8;
	size_w = sdl_menu_text_width(ui, "00,000,000") + 8;

	if (dlg->is_81) {
		sdl_draw_text_menu(ui, col_x,
		                   header_y
		                       + (layout.row_h - layout.text_h) / 2,
		                   "Slot", dim);
		col_x += slot_w;
	}
	sdl_draw_text_menu(ui, col_x,
	                   header_y
	                       + (layout.row_h - layout.text_h) / 2,
	                   "Name", dim);
	if (!dlg->is_81) {
		col_x = layout.list_rect.x + layout.list_rect.w - size_w
			- type_w;
		sdl_draw_text_menu(ui, col_x,
		                   header_y
		                       + (layout.row_h - layout.text_h) / 2,
		                   "Type", dim);
	}
	sdl_draw_text_menu(ui,
	                   layout.list_rect.x + layout.list_rect.w - size_w,
	                   header_y
	                       + (layout.row_h - layout.text_h) / 2,
	                   "Size", dim);

	for (i = 0; i < rows; i++) {
		int row = dlg->top + i;
		SDL_Rect row_rect;
		SDL_Rect checkbox;
		TilemVarEntry *tve;
		char *size_str;
		int y;

		if (row >= (int) dlg->entries->len)
			break;

		tve = g_ptr_array_index(dlg->entries, row);
		y = start_y + i * layout.row_h;
		row_rect = (SDL_Rect) {
			layout.list_rect.x + 1, y,
			layout.list_rect.w - 2, layout.row_h
		};
		if (row == dlg->selected) {
			SDL_SetRenderDrawColor(ui->renderer, highlight.r,
			                       highlight.g, highlight.b,
			                       highlight.a);
			SDL_RenderFillRect(ui->renderer, &row_rect);
		}

		checkbox = (SDL_Rect) {
			layout.list_rect.x + 6,
			y + (layout.row_h - checkbox_size) / 2,
			checkbox_size, checkbox_size
		};
		sdl_draw_checkbox(ui->renderer, checkbox,
		                  sdl_receive_dialog_is_selected(dlg, row),
		                  border, panel, text);

		col_x = layout.list_rect.x + 6 + checkbox_size + 6;
		if (dlg->is_81) {
			sdl_draw_text_menu(ui, col_x,
			                   y
			                       + (layout.row_h
			                          - layout.text_h) / 2,
			                   tve->slot_str ? tve->slot_str : "",
			                   text);
			col_x += slot_w;
		}

		sdl_draw_text_menu(ui, col_x,
		                   y
		                       + (layout.row_h
		                          - layout.text_h) / 2,
		                   tve->name_str ? tve->name_str : "",
		                   text);

		if (!dlg->is_81) {
			sdl_draw_text_menu(
				ui,
				layout.list_rect.x + layout.list_rect.w
					- size_w - type_w,
				y + (layout.row_h - layout.text_h) / 2,
				tve->type_str ? tve->type_str : "",
				text);
		}
#ifdef G_OS_WIN32
		size_str = g_strdup_printf("%d", tve->size);
#else
		size_str = g_strdup_printf("%'d", tve->size);
#endif
		sdl_draw_text_menu(ui,
		                   layout.list_rect.x
		                       + layout.list_rect.w - size_w,
		                   y
		                       + (layout.row_h
		                          - layout.text_h) / 2,
		                   size_str, text);
		g_free(size_str);
	}

	if (!dlg->is_81) {
		sdl_draw_text_menu(ui,
		                   layout.mode_label_rect.x,
		                   layout.mode_label_rect.y
		                       + (layout.row_h - layout.text_h) / 2,
		                   "Save as:",
		                   dim);
		sdl_draw_radio(ui->renderer,
		               (SDL_Rect) {
		                   layout.mode_sep_rect.x,
		                   layout.mode_sep_rect.y
		                       + (layout.row_h - checkbox_size) / 2,
		                   checkbox_size, checkbox_size
		               },
		               !dlg->use_group,
		               border, panel, text);
		sdl_draw_text_menu(ui,
		                   layout.mode_sep_rect.x
		                       + checkbox_size + 8,
		                   layout.mode_sep_rect.y
		                       + (layout.row_h - layout.text_h) / 2,
		                   "Separate files",
		                   text);
		sdl_draw_radio(ui->renderer,
		               (SDL_Rect) {
		                   layout.mode_group_rect.x,
		                   layout.mode_group_rect.y
		                       + (layout.row_h - checkbox_size) / 2,
		                   checkbox_size, checkbox_size
		               },
		               dlg->use_group,
		               border, panel, text);
		sdl_draw_text_menu(ui,
		                   layout.mode_group_rect.x
		                       + checkbox_size + 8,
		                   layout.mode_group_rect.y
		                       + (layout.row_h - layout.text_h) / 2,
		                   "Group file",
		                   text);
	}

	SDL_SetRenderDrawColor(ui->renderer, button_fill.r, button_fill.g,
	                       button_fill.b, 255);
	SDL_RenderFillRect(ui->renderer, &layout.refresh_rect);
	SDL_RenderFillRect(ui->renderer, &layout.save_rect);
	SDL_RenderFillRect(ui->renderer, &layout.cancel_rect);
	SDL_SetRenderDrawColor(ui->renderer, border.r, border.g,
	                       border.b, border.a);
	SDL_RenderDrawRect(ui->renderer, &layout.refresh_rect);
	SDL_RenderDrawRect(ui->renderer, &layout.save_rect);
	SDL_RenderDrawRect(ui->renderer, &layout.cancel_rect);

	sdl_draw_text_menu(ui,
	                   layout.refresh_rect.x + 12,
	                   layout.refresh_rect.y
	                       + (layout.refresh_rect.h - layout.text_h) / 2,
	                   "Refresh",
	                   dlg->is_81 ? dim : text);
	sdl_draw_text_menu(ui,
	                   layout.save_rect.x + 12,
	                   layout.save_rect.y
	                       + (layout.save_rect.h - layout.text_h) / 2,
	                   "Save",
	                   text);
	sdl_draw_text_menu(ui,
	                   layout.cancel_rect.x + 12,
	                   layout.cancel_rect.y
	                       + (layout.cancel_rect.h - layout.text_h) / 2,
	                   "Cancel",
	                   text);

	SDL_SetRenderDrawBlendMode(ui->renderer, SDL_BLENDMODE_NONE);
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
	sdl_render_preferences(ui);
	sdl_render_slot_dialog(ui);
	sdl_render_receive_dialog(ui);
	sdl_render_screenshot_dialog(ui);
	SDL_RenderPresent(ui->renderer);
}

/* Find keycode for the key (if any) at the given position. */
static int scan_click(const TilemSdlSkin *skin, double x, double y)
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

	if (!ui || !ui->emu || (!ui->emu->calc && !ui->skin_override)) {
		sdl_free_skin(ui);
		sdl_set_palette(ui);
		return;
	}

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

static char **sdl_pick_send_files(TilemSdlUi *ui)
{
	char *dir = NULL;
	char **filenames;
	gboolean used_native = FALSE;

	if (!ui->emu->calc)
		return NULL;

	tilem_config_get("upload", "sendfile_recentdir/f", &dir, NULL);
	if (!dir)
		dir = g_get_current_dir();

	filenames = sdl_native_file_dialog_multi("Send File", dir,
	                                         &used_native);
	g_free(dir);

	if (!filenames && !used_native) {
		sdl_show_message(ui, "Send File",
		                 "No native file picker available.");
	}

	if (filenames) {
		dir = g_path_get_dirname(filenames[0]);
		tilem_config_set("upload", "sendfile_recentdir/f", dir, NULL);
		g_free(dir);
	}

	return filenames;
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
	if (ui->debugger)
		tilem_sdl_debugger_calc_changed(ui->debugger);
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

typedef struct {
	TilemSdlUi *ui;
	char **filenames;
	int nfiles;
	int *slots;
	TI81ProgInfo info[TI81_SLOT_MAX + 1];
} TilemSdlSlotTask;

static gboolean sdl_slot_task_main(TilemCalcEmulator *emu, gpointer data)
{
	TilemSdlSlotTask *task = data;
	int i;

	tilem_em_wake_up(emu, TRUE);
	for (i = 0; i <= TI81_SLOT_MAX; i++)
		ti81_get_program_info(emu->calc, i, &task->info[i]);

	return TRUE;
}

static void sdl_slot_task_finished(G_GNUC_UNUSED TilemCalcEmulator *emu,
                                   gpointer data,
                                   gboolean cancelled)
{
	TilemSdlSlotTask *task = data;
	TilemSdlUi *ui = task->ui;

	if (!cancelled && ui) {
		sdl_slot_dialog_clear(ui);
		ui->menu_visible = FALSE;
		ui->submenu_visible = FALSE;
		ui->prefs_visible = FALSE;
		ui->slot_dialog.visible = TRUE;
		ui->slot_dialog.filenames = task->filenames;
		ui->slot_dialog.nfiles = task->nfiles;
		ui->slot_dialog.slots = task->slots;
		memcpy(ui->slot_dialog.info, task->info,
		       sizeof(task->info));
		ui->slot_dialog.selected = 0;
		ui->slot_dialog.top = 0;
		sdl_slot_dialog_assign_defaults(&ui->slot_dialog);
		task->filenames = NULL;
		task->slots = NULL;
	}

	if (task->filenames)
		g_strfreev(task->filenames);
	g_free(task->slots);
	g_slice_free(TilemSdlSlotTask, task);
}

static void sdl_handle_send_files(TilemSdlUi *ui, char **filenames)
{
	TilemSdlSlotTask *task;
	int i;

	if (!filenames || !filenames[0])
		return;
	if (!ui->emu || !ui->emu->calc) {
		sdl_show_message(ui, "Send File",
		                 "No calculator loaded yet.");
		g_strfreev(filenames);
		return;
	}

	if (ui->emu->calc->hw.model_id == TILEM_CALC_TI81) {
		task = g_slice_new0(TilemSdlSlotTask);
		task->ui = ui;
		task->filenames = filenames;
		task->nfiles = g_strv_length(filenames);
		task->slots = g_new(int, task->nfiles);
		for (i = 0; i < task->nfiles; i++)
			task->slots[i] = TI81_SLOT_AUTO;
		tilem_calc_emulator_begin(ui->emu, &sdl_slot_task_main,
		                          &sdl_slot_task_finished, task);
		return;
	}

	sdl_send_files(ui, filenames, NULL);
	g_strfreev(filenames);
}

static void sdl_drop_begin(TilemSdlUi *ui)
{
	if (!ui)
		return;

	if (ui->drop_files)
		g_ptr_array_free(ui->drop_files, TRUE);
	ui->drop_files = g_ptr_array_new_with_free_func(g_free);
	ui->drop_active = TRUE;
}

static void sdl_drop_file(TilemSdlUi *ui, const char *filename)
{
	if (!ui || !filename || !*filename)
		return;

	if (!ui->drop_active)
		sdl_drop_begin(ui);

	if (ui->drop_files)
		g_ptr_array_add(ui->drop_files, g_strdup(filename));
}

static void sdl_drop_complete(TilemSdlUi *ui)
{
	char **files;
	guint i;

	if (!ui || !ui->drop_files) {
		if (ui)
			ui->drop_active = FALSE;
		return;
	}

	ui->drop_active = FALSE;
	if (ui->drop_files->len == 0) {
		g_ptr_array_free(ui->drop_files, TRUE);
		ui->drop_files = NULL;
		return;
	}

	files = g_new0(char *, ui->drop_files->len + 1);
	for (i = 0; i < ui->drop_files->len; i++) {
		files[i] = g_strdup(g_ptr_array_index(ui->drop_files, i));
	}
	files[ui->drop_files->len] = NULL;

	g_ptr_array_free(ui->drop_files, TRUE);
	ui->drop_files = NULL;

	sdl_handle_send_files(ui, files);
}

static void sdl_handle_send_file(TilemSdlUi *ui)
{
	char **filenames;

	if (!ui->emu->calc) {
		sdl_show_message(ui, "Send File",
		                 "No calculator loaded yet.");
		return;
	}

	filenames = sdl_pick_send_files(ui);
	if (!filenames)
		return;

	sdl_handle_send_files(ui, filenames);
}

static void sdl_handle_receive_file(TilemSdlUi *ui)
{
	if (!ui->emu->calc) {
		sdl_show_message(ui, "Receive File",
		                 "No calculator loaded yet.");
		return;
	}

	sdl_receive_dialog_request(ui);
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

static void sdl_handle_debugger(TilemSdlUi *ui)
{
	if (!ui->emu->calc) {
		sdl_show_message(ui, "Debugger",
		                 "No calculator loaded yet.");
		return;
	}

	if (!ui->debugger)
		ui->debugger = tilem_sdl_debugger_new(ui->emu);

	if (tilem_sdl_debugger_visible(ui->debugger))
		tilem_sdl_debugger_hide(ui->debugger);
	else
		tilem_sdl_debugger_show(ui->debugger);
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
	if (!ui->emu->calc) {
		sdl_show_message(ui, "Screenshot",
		                 "No calculator loaded yet.");
		return;
	}
	sdl_screenshot_dialog_open(ui);
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
	sdl_preferences_open(ui);
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
	case SDL_MENU_DEBUGGER:
		sdl_handle_debugger(ui);
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

static gboolean sdl_handle_shortcut(TilemSdlUi *ui, SDL_Keycode sym, int mods,
                                    gboolean *running)
{
	int accel = mods & (KMOD_CTRL | KMOD_GUI);

	if (sym >= 'A' && sym <= 'Z')
		sym = sym - 'A' + 'a';

	if (sym == SDLK_PAUSE) {
		sdl_handle_debugger(ui);
		return TRUE;
	}

	if (accel && sym == SDLK_q) {
		if (running)
			*running = FALSE;
		return TRUE;
	}

	if (accel && sym == SDLK_PRINTSCREEN) {
		if (mods & KMOD_SHIFT)
			sdl_handle_quick_screenshot(ui);
		else
			sdl_handle_screenshot(ui);
		return TRUE;
	}

	if (accel && (mods & KMOD_SHIFT) && sym == SDLK_DELETE) {
		if (ui->emu && ui->emu->calc)
			tilem_calc_emulator_reset(ui->emu);
		return TRUE;
	}

	if (accel && (sym == SDLK_o || sym == SDLK_s)) {
		if (mods & KMOD_SHIFT) {
			if (sym == SDLK_o)
				sdl_handle_open_calc(ui);
			else
				sdl_handle_save_calc(ui);
		}
		else {
			if (sym == SDLK_o)
				sdl_handle_send_file(ui);
			else
				sdl_handle_receive_file(ui);
		}
		return TRUE;
	}

	return FALSE;
}

static void sdl_cleanup(TilemSdlUi *ui)
{
	if (ui->debugger) {
		tilem_sdl_debugger_free(ui->debugger);
		ui->debugger = NULL;
	}

	sdl_slot_dialog_clear(ui);
	sdl_receive_dialog_clear(ui);
	if (ui->screenshot_dialog.current_anim) {
		g_object_unref(ui->screenshot_dialog.current_anim);
		ui->screenshot_dialog.current_anim = NULL;
	}
	sdl_screenshot_free_preview(&ui->screenshot_dialog);
	if (ui->screenshot_dialog.preview_palette) {
		tilem_free(ui->screenshot_dialog.preview_palette);
		ui->screenshot_dialog.preview_palette = NULL;
	}
	if (ui->drop_files) {
		g_ptr_array_free(ui->drop_files, TRUE);
		ui->drop_files = NULL;
	}

	sdl_free_skin(ui);

	if (ui->icons) {
		tilem_sdl_icons_free(ui->icons);
		ui->icons = NULL;
	}

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

	sdl_shutdown_image(ui);
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

static void sdl_init_image(TilemSdlUi *ui)
{
	int flags = IMG_INIT_PNG | IMG_INIT_JPG;
	int inited;

	inited = IMG_Init(flags);
	if ((inited & flags) != flags) {
		g_printerr("SDL_image init failed: %s\n", IMG_GetError());
		return;
	}
	ui->image_ready = TRUE;
}

static void sdl_shutdown_ttf(TilemSdlUi *ui)
{
	if (ui->menu_font) {
		TTF_CloseFont(ui->menu_font);
		ui->menu_font = NULL;
	}

	if (TTF_WasInit())
		TTF_Quit();
	ui->ttf_ready = FALSE;
}

static void sdl_shutdown_image(TilemSdlUi *ui)
{
	if (ui->image_ready)
		IMG_Quit();
	ui->image_ready = FALSE;
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
	}
	else {
		ui.lcd_width = 96;
		ui.lcd_height = 64;
	}

	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
		g_printerr("SDL_Init failed: %s\n", SDL_GetError());
		sdl_cleanup(&ui);
		return 1;
	}
	SDL_EventState(SDL_DROPFILE, SDL_ENABLE);
	SDL_EventState(SDL_DROPBEGIN, SDL_ENABLE);
	SDL_EventState(SDL_DROPCOMPLETE, SDL_ENABLE);
	sdl_init_image(&ui);
	sdl_update_skin_for_calc(&ui);

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

	ui.icons = tilem_sdl_icons_load(ui.renderer);
	if (ui.icons && ui.icons->app_surface)
		SDL_SetWindowIcon(ui.window, ui.icons->app_surface);

	if (ui.skin && !ui.skin_texture) {
		ui.skin_texture = SDL_CreateTextureFromSurface(
			ui.renderer, ui.skin->surface);
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
			if (ui.debugger
			    && tilem_sdl_debugger_handle_event(
			           ui.debugger, &event))
				continue;
			if (ui.screenshot_dialog.visible
			    && sdl_screenshot_dialog_handle_event(&ui, &event))
				continue;
			if (ui.slot_dialog.visible
			    && sdl_slot_dialog_handle_event(&ui, &event))
				continue;
			if (ui.receive_dialog.visible
			    && sdl_receive_dialog_handle_event(&ui, &event))
				continue;
			if (ui.prefs_visible
			    && sdl_preferences_handle_event(&ui, &event))
				continue;
			switch (event.type) {
			case SDL_QUIT:
				running = FALSE;
				break;
			case SDL_WINDOWEVENT:
				if (event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED)
					sdl_update_layout(&ui, event.window.data1,
					                  event.window.data2);
				break;
			case SDL_DROPBEGIN:
				sdl_drop_begin(&ui);
				break;
			case SDL_DROPFILE:
				sdl_drop_file(&ui, event.drop.file);
				SDL_free(event.drop.file);
				break;
			case SDL_DROPCOMPLETE:
				sdl_drop_complete(&ui);
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
					if (sdl_handle_shortcut(&ui, sym, mods,
					                        &running))
						break;
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
		while (g_main_context_iteration(NULL, FALSE)) {
		}

		sdl_render(&ui);
		if (ui.debugger)
			tilem_sdl_debugger_render(ui.debugger);
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
