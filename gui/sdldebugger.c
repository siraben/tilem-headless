/*
 * TilEm II - SDL2 debugger UI
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
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <glib.h>
#include <glib/gstdio.h>
#include <ticalcs.h>
#include <tilem.h>
#include <tilemdb.h>
#include <scancodes.h>

#include "sdldebugger.h"
#include "files.h"
#include "sdlicons.h"

void tilem_config_get(const char *group, const char *option, ...);
void tilem_config_set(const char *group, const char *option, ...);

#define SDL_DEBUGGER_TITLE "TilEm Debugger"
#define SDL_DEBUGGER_DEFAULT_WIDTH 900
#define SDL_DEBUGGER_DEFAULT_HEIGHT 600
#define SDL_DEBUGGER_FONT_SIZE 14
#define SDL_DEBUGGER_MARGIN 8
#define SDL_DEBUGGER_LINE_GAP 2
#define SDL_DEBUGGER_MEM_COLS 16
#define SDL_DEBUGGER_MEM_ROWS 8
#define SDL_DEBUGGER_STACK_ROWS 8
#define SDL_DEBUGGER_MAX_LINES 128
#define SDL_DEBUGGER_LINE_BUFSZ 160
#define SDL_DEBUGGER_MAX_DISASM_BACK 6
#define SDL_DEBUGGER_INPUT_MAX 128
#define SDL_DEBUGGER_BP_LINES 14
#define SDL_DEBUGGER_KEYPAD_COLS 8
#define SDL_DEBUGGER_KEYPAD_ROWS 7
#define SDL_DEBUGGER_ICON_GAP 6
#define SDL_DEBUGGER_MENU_PADDING 6
#define SDL_DEBUGGER_MENU_SPACING 2
#define SDL_DEBUGGER_MENU_ICON_GAP 6

enum {
	SDL_DBG_BP_LOGICAL,
	SDL_DBG_BP_PHYSICAL,
	SDL_DBG_BP_PORT,
	SDL_DBG_BP_OPCODE
};

enum {
	SDL_DBG_BP_READ = 4,
	SDL_DBG_BP_WRITE = 2,
	SDL_DBG_BP_EXEC = 1
};

typedef enum {
	SDL_DBG_INPUT_NONE,
	SDL_DBG_INPUT_GOTO,
	SDL_DBG_INPUT_BP_ADD,
	SDL_DBG_INPUT_BP_EDIT
} TilemSdlInputMode;

typedef enum {
	SDL_DBG_MENU_BAR_NONE = -1,
	SDL_DBG_MENU_BAR_DEBUG,
	SDL_DBG_MENU_BAR_VIEW,
	SDL_DBG_MENU_BAR_GO,
	SDL_DBG_MENU_BAR_COUNT
} TilemSdlMenuBar;

typedef enum {
	SDL_DBG_MENU_ACTION_NONE,
	SDL_DBG_MENU_ACTION_RUN,
	SDL_DBG_MENU_ACTION_PAUSE,
	SDL_DBG_MENU_ACTION_STEP,
	SDL_DBG_MENU_ACTION_STEP_OVER,
	SDL_DBG_MENU_ACTION_FINISH,
	SDL_DBG_MENU_ACTION_BREAKPOINTS,
	SDL_DBG_MENU_ACTION_CLOSE,
	SDL_DBG_MENU_ACTION_VIEW_KEYPAD,
	SDL_DBG_MENU_ACTION_VIEW_LOGICAL,
	SDL_DBG_MENU_ACTION_VIEW_ABSOLUTE,
	SDL_DBG_MENU_ACTION_GO_ADDRESS,
	SDL_DBG_MENU_ACTION_GO_PC,
	SDL_DBG_MENU_ACTION_PREV_STACK,
	SDL_DBG_MENU_ACTION_NEXT_STACK
} TilemSdlDbgMenuAction;

enum {
	SDL_DBG_MENU_FLAG_TOGGLE = 1 << 0,
	SDL_DBG_MENU_FLAG_RADIO = 1 << 1
};

typedef struct {
	const char *label;
	TilemSdlDbgMenuAction action;
	gboolean separator;
	int flags;
} TilemSdlDbgMenuItem;

typedef struct {
	TilemSdlDbgMenuAction action;
	const TilemSdlIcon *icon;
	const char *label;
	SDL_Rect rect;
} TilemSdlDbgToolbarButton;

typedef struct {
	int type;
	int mode;
	dword start;
	dword end;
	dword mask;
	int disabled;
	int id[3];
} TilemSdlDebugBreakpoint;

typedef struct {
	SDL_Window *window;
	SDL_Renderer *renderer;
	Uint32 window_id;
	int width;
	int height;
	gboolean visible;
} TilemSdlKeypad;

typedef struct {
	SDL_Rect disasm_rect;
	SDL_Rect regs_rect;
	SDL_Rect stack_rect;
	SDL_Rect mem_rect;
	SDL_Rect toolbar_rect;
	SDL_Rect menu_rect;
	int disasm_lines;
	int mem_rows;
	int stack_rows;
	int reg_lines;
	int disasm_icon_w;
	int toolbar_icon_h;
} TilemSdlDebuggerLayout;

struct _TilemSdlDebugger {
	TilemCalcEmulator *emu;
	SDL_Window *window;
	SDL_Renderer *renderer;
	TTF_Font *font;
	gboolean ttf_ready;
	gboolean visible;
	gboolean resume_on_hide;
	Uint32 window_id;
	int window_width;
	int window_height;
	int line_height;
	int char_width;
	TilemDisasm *dasm;
	GArray *breakpoints;
	int step_bp;
	dword step_next_addr;
	dword disasm_base;
	dword disasm_cursor;
	gboolean disasm_follow_pc;
	dword mem_addr;
	gboolean mem_follow_pc;
	gboolean mem_logical;
	int stack_index;
	int last_bp_type;
	int last_bp_mode;
	gboolean show_breakpoints;
	int bp_selected;
	TilemSdlInputMode input_mode;
	char input_buf[SDL_DEBUGGER_INPUT_MAX];
	int input_len;
	char input_error[SDL_DEBUGGER_INPUT_MAX];
	int input_bp_index;
	TilemSdlKeypad keypad;
	TilemSdlIcons *icons;
	gboolean menu_open;
	TilemSdlMenuBar menu_active;
	int menu_selected;
	SDL_Rect menu_bar_items[SDL_DBG_MENU_BAR_COUNT];
};

static const char * const sdl_dbg_menu_bar_labels[] = {
	"Debug",
	"View",
	"Go"
};

static const TilemSdlDbgMenuItem sdl_dbg_menu_debug_items[] = {
	{ "Run", SDL_DBG_MENU_ACTION_RUN, FALSE, 0 },
	{ "Pause", SDL_DBG_MENU_ACTION_PAUSE, FALSE, 0 },
	{ NULL, SDL_DBG_MENU_ACTION_NONE, TRUE, 0 },
	{ "Step", SDL_DBG_MENU_ACTION_STEP, FALSE, 0 },
	{ "Step Over", SDL_DBG_MENU_ACTION_STEP_OVER, FALSE, 0 },
	{ "Finish Subroutine", SDL_DBG_MENU_ACTION_FINISH, FALSE, 0 },
	{ NULL, SDL_DBG_MENU_ACTION_NONE, TRUE, 0 },
	{ "Breakpoints", SDL_DBG_MENU_ACTION_BREAKPOINTS, FALSE, 0 },
	{ NULL, SDL_DBG_MENU_ACTION_NONE, TRUE, 0 },
	{ "Close", SDL_DBG_MENU_ACTION_CLOSE, FALSE, 0 }
};

static const TilemSdlDbgMenuItem sdl_dbg_menu_view_items[] = {
	{ "Keypad", SDL_DBG_MENU_ACTION_VIEW_KEYPAD, FALSE,
	  SDL_DBG_MENU_FLAG_TOGGLE },
	{ NULL, SDL_DBG_MENU_ACTION_NONE, TRUE, 0 },
	{ "Logical Addresses", SDL_DBG_MENU_ACTION_VIEW_LOGICAL, FALSE,
	  SDL_DBG_MENU_FLAG_RADIO },
	{ "Absolute Addresses", SDL_DBG_MENU_ACTION_VIEW_ABSOLUTE, FALSE,
	  SDL_DBG_MENU_FLAG_RADIO }
};

static const TilemSdlDbgMenuItem sdl_dbg_menu_go_items[] = {
	{ "Address...", SDL_DBG_MENU_ACTION_GO_ADDRESS, FALSE, 0 },
	{ "Current PC", SDL_DBG_MENU_ACTION_GO_PC, FALSE, 0 },
	{ NULL, SDL_DBG_MENU_ACTION_NONE, TRUE, 0 },
	{ "Previous Stack Entry", SDL_DBG_MENU_ACTION_PREV_STACK, FALSE, 0 },
	{ "Next Stack Entry", SDL_DBG_MENU_ACTION_NEXT_STACK, FALSE, 0 }
};

static void sdl_dbg_start_input(TilemSdlDebugger *dbg,
                                TilemSdlInputMode mode,
                                const char *prefill);
static void sdl_dbg_toggle_breakpoints_dialog(TilemSdlDebugger *dbg);
static void sdl_dbg_set_mem_mode(TilemSdlDebugger *dbg, gboolean logical);
static void sdl_dbg_go_to_pc(TilemSdlDebugger *dbg);
static void sdl_dbg_go_to_stack_pos(TilemSdlDebugger *dbg, int pos);
static void sdl_dbg_step(TilemSdlDebugger *dbg);
static void sdl_dbg_step_over(TilemSdlDebugger *dbg);
static void sdl_dbg_finish(TilemSdlDebugger *dbg);
static void sdl_dbg_keypad_show(TilemSdlDebugger *dbg);
static void sdl_dbg_keypad_hide(TilemSdlDebugger *dbg);

static char *sdl_dbg_find_mono_font_path(void)
{
	const char *env = g_getenv("TILEM_SDL_MONO_FONT");
	const char *candidates[] = {
		"/System/Library/Fonts/Menlo.ttc",
		"/System/Library/Fonts/Supplemental/Courier New.ttf",
		"/System/Library/Fonts/Supplemental/Menlo.ttc",
		"/Library/Fonts/Menlo.ttc",
		"/Library/Fonts/Courier New.ttf",
		"/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
		"/usr/share/fonts/truetype/liberation/LiberationMono-Regular.ttf",
		"/usr/share/fonts/truetype/freefont/FreeMono.ttf",
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

static void sdl_dbg_draw_text(TilemSdlDebugger *dbg, int x, int y,
                              const char *text, SDL_Color color)
{
	SDL_Surface *surface;
	SDL_Texture *texture;
	SDL_Rect dst;

	if (!dbg->font || !text || !*text)
		return;

	surface = TTF_RenderUTF8_Blended(dbg->font, text, color);
	if (!surface)
		return;

	texture = SDL_CreateTextureFromSurface(dbg->renderer, surface);
	if (!texture) {
		SDL_FreeSurface(surface);
		return;
	}

	dst.x = x;
	dst.y = y;
	dst.w = surface->w;
	dst.h = surface->h;
	SDL_RenderCopy(dbg->renderer, texture, NULL, &dst);
	SDL_DestroyTexture(texture);
	SDL_FreeSurface(surface);
}

static int sdl_dbg_text_width(TilemSdlDebugger *dbg, const char *text)
{
	int width = 0;
	int height = 0;

	if (!dbg || !dbg->font || !text)
		return 0;

	if (TTF_SizeUTF8(dbg->font, text, &width, &height) == 0)
		return width;
	return 0;
}

static gboolean sdl_dbg_init_ttf(TilemSdlDebugger *dbg)
{
	char *font_path;
	int w = 0;
	int h = 0;

	if (TTF_WasInit())
		dbg->ttf_ready = TRUE;

	if (!dbg->ttf_ready) {
		if (TTF_Init() != 0) {
			g_printerr("SDL_ttf init failed: %s\n", TTF_GetError());
			return FALSE;
		}
		dbg->ttf_ready = TRUE;
	}

	font_path = sdl_dbg_find_mono_font_path();
	if (!font_path) {
		g_printerr("SDL debugger font not found\n");
		return FALSE;
	}

	dbg->font = TTF_OpenFont(font_path, SDL_DEBUGGER_FONT_SIZE);
	g_free(font_path);
	if (!dbg->font) {
		g_printerr("SDL_ttf open failed: %s\n", TTF_GetError());
		return FALSE;
	}

	dbg->line_height = TTF_FontHeight(dbg->font) + SDL_DEBUGGER_LINE_GAP;
	if (TTF_SizeUTF8(dbg->font, "M", &w, &h) == 0 && w > 0)
		dbg->char_width = w;
	else
		dbg->char_width = 8;

	if (dbg->line_height <= 0)
		dbg->line_height = 12;

	return TRUE;
}

static void sdl_dbg_shutdown_ttf(TilemSdlDebugger *dbg)
{
	if (dbg->font) {
		TTF_CloseFont(dbg->font);
		dbg->font = NULL;
	}
	dbg->ttf_ready = FALSE;
}

static int sdl_dbg_icon_size(const TilemSdlIcon *icon)
{
	if (!icon || !icon->texture)
		return 0;
	return (icon->width > icon->height) ? icon->width : icon->height;
}

static gboolean sdl_dbg_has_disasm_icons(TilemSdlDebugger *dbg)
{
	if (!dbg || !dbg->icons)
		return FALSE;
	return (dbg->icons->disasm_pc.texture
	        || dbg->icons->disasm_break.texture
	        || dbg->icons->disasm_break_pc.texture);
}

static int sdl_dbg_menu_bar_height(TilemSdlDebugger *dbg)
{
	int font_h;

	if (!dbg || !dbg->font)
		return dbg ? dbg->line_height + SDL_DEBUGGER_MENU_PADDING * 2
		           : 0;

	font_h = TTF_FontHeight(dbg->font);
	if (font_h <= 0)
		font_h = dbg->line_height;
	return font_h + SDL_DEBUGGER_MENU_PADDING * 2;
}

static const TilemSdlDbgMenuItem *sdl_dbg_menu_items(TilemSdlMenuBar menu,
                                                     size_t *out_len)
{
	switch (menu) {
	case SDL_DBG_MENU_BAR_DEBUG:
		if (out_len)
			*out_len = G_N_ELEMENTS(sdl_dbg_menu_debug_items);
		return sdl_dbg_menu_debug_items;
	case SDL_DBG_MENU_BAR_VIEW:
		if (out_len)
			*out_len = G_N_ELEMENTS(sdl_dbg_menu_view_items);
		return sdl_dbg_menu_view_items;
	case SDL_DBG_MENU_BAR_GO:
		if (out_len)
			*out_len = G_N_ELEMENTS(sdl_dbg_menu_go_items);
		return sdl_dbg_menu_go_items;
	default:
		break;
	}

	if (out_len)
		*out_len = 0;
	return NULL;
}

static const TilemSdlIcon *sdl_dbg_menu_item_icon(TilemSdlDebugger *dbg,
                                                  TilemSdlDbgMenuAction action)
{
	if (!dbg || !dbg->icons)
		return NULL;

	switch (action) {
	case SDL_DBG_MENU_ACTION_RUN:
		return dbg->icons->db_run.texture ? &dbg->icons->db_run : NULL;
	case SDL_DBG_MENU_ACTION_PAUSE:
		return dbg->icons->db_pause.texture ? &dbg->icons->db_pause
		                                    : NULL;
	case SDL_DBG_MENU_ACTION_STEP:
		return dbg->icons->db_step.texture ? &dbg->icons->db_step : NULL;
	case SDL_DBG_MENU_ACTION_STEP_OVER:
		return dbg->icons->db_step_over.texture
			? &dbg->icons->db_step_over
			: NULL;
	case SDL_DBG_MENU_ACTION_FINISH:
		return dbg->icons->db_finish.texture
			? &dbg->icons->db_finish
			: NULL;
	case SDL_DBG_MENU_ACTION_CLOSE:
		if (dbg->icons->menu[TILEM_SDL_MENU_ICON_CLOSE].texture)
			return &dbg->icons->menu[TILEM_SDL_MENU_ICON_CLOSE];
		return NULL;
	default:
		return NULL;
	}
}

static int sdl_dbg_menu_indicator_width(const TilemSdlDbgMenuItem *items,
                                        size_t n_items)
{
	size_t i;

	if (!items)
		return 0;

	for (i = 0; i < n_items; i++) {
		if (items[i].separator)
			continue;
		if (items[i].flags & (SDL_DBG_MENU_FLAG_TOGGLE
		                      | SDL_DBG_MENU_FLAG_RADIO))
			return 10;
	}

	return 0;
}

static int sdl_dbg_menu_icon_column_width(TilemSdlDebugger *dbg,
                                          const TilemSdlDbgMenuItem *items,
                                          size_t n_items)
{
	size_t i;
	int maxw = 0;
	int indicator_w = sdl_dbg_menu_indicator_width(items, n_items);

	if (!dbg || !items)
		return 0;

	for (i = 0; i < n_items; i++) {
		const TilemSdlIcon *icon;

		if (items[i].separator)
			continue;
		icon = sdl_dbg_menu_item_icon(dbg, items[i].action);
		if (icon && icon->width > maxw)
			maxw = icon->width;
	}

	if (indicator_w > maxw)
		maxw = indicator_w;

	if (maxw > 0)
		return maxw + SDL_DEBUGGER_MENU_ICON_GAP;
	return 0;
}

static int sdl_dbg_menu_item_height(TilemSdlDebugger *dbg,
                                    const TilemSdlDbgMenuItem *items,
                                    size_t n_items)
{
	int font_h = 0;
	int icon_h = 0;
	size_t i;

	if (dbg && dbg->font)
		font_h = TTF_FontHeight(dbg->font);
	if (font_h <= 0)
		font_h = dbg ? dbg->line_height : 0;

	if (dbg && items) {
		for (i = 0; i < n_items; i++) {
			const TilemSdlIcon *icon;

			if (items[i].separator)
				continue;
			icon = sdl_dbg_menu_item_icon(dbg, items[i].action);
			if (icon && icon->height > icon_h)
				icon_h = icon->height;
		}
	}

	if (icon_h > font_h)
		font_h = icon_h;

	return font_h + SDL_DEBUGGER_MENU_PADDING * 2;
}

static gboolean sdl_dbg_is_paused(TilemSdlDebugger *dbg)
{
	return dbg && dbg->emu && dbg->emu->paused;
}

static gboolean sdl_dbg_menu_action_enabled(TilemSdlDebugger *dbg,
                                            TilemSdlDbgMenuAction action)
{
	gboolean paused = sdl_dbg_is_paused(dbg);

	if (!dbg || !dbg->emu)
		return FALSE;

	switch (action) {
	case SDL_DBG_MENU_ACTION_RUN:
		return paused;
	case SDL_DBG_MENU_ACTION_PAUSE:
		return !paused;
	case SDL_DBG_MENU_ACTION_STEP:
	case SDL_DBG_MENU_ACTION_STEP_OVER:
	case SDL_DBG_MENU_ACTION_FINISH:
	case SDL_DBG_MENU_ACTION_BREAKPOINTS:
	case SDL_DBG_MENU_ACTION_VIEW_LOGICAL:
	case SDL_DBG_MENU_ACTION_VIEW_ABSOLUTE:
	case SDL_DBG_MENU_ACTION_GO_ADDRESS:
	case SDL_DBG_MENU_ACTION_GO_PC:
		return paused;
	case SDL_DBG_MENU_ACTION_PREV_STACK:
		return paused && dbg->stack_index >= 0;
	case SDL_DBG_MENU_ACTION_NEXT_STACK:
		return paused;
	case SDL_DBG_MENU_ACTION_VIEW_KEYPAD:
	case SDL_DBG_MENU_ACTION_CLOSE:
		return TRUE;
	default:
		return TRUE;
	}
}

static gboolean sdl_dbg_menu_action_checked(TilemSdlDebugger *dbg,
                                            const TilemSdlDbgMenuItem *item)
{
	if (!dbg || !item)
		return FALSE;

	switch (item->action) {
	case SDL_DBG_MENU_ACTION_VIEW_KEYPAD:
		return dbg->keypad.visible;
	case SDL_DBG_MENU_ACTION_VIEW_LOGICAL:
		return dbg->mem_logical;
	case SDL_DBG_MENU_ACTION_VIEW_ABSOLUTE:
		return !dbg->mem_logical;
	default:
		return FALSE;
	}
}

static void sdl_dbg_menu_close(TilemSdlDebugger *dbg)
{
	if (!dbg)
		return;

	dbg->menu_open = FALSE;
	dbg->menu_active = SDL_DBG_MENU_BAR_NONE;
	dbg->menu_selected = -1;
}

static int sdl_dbg_menu_first_selectable(const TilemSdlDbgMenuItem *items,
                                         size_t n_items)
{
	size_t i;

	if (!items)
		return -1;

	for (i = 0; i < n_items; i++) {
		if (!items[i].separator)
			return (int) i;
	}

	return -1;
}

static void sdl_dbg_menu_bar_layout(TilemSdlDebugger *dbg,
                                    const TilemSdlDebuggerLayout *layout)
{
	int x;
	int i;

	if (!dbg || !layout)
		return;

	x = layout->menu_rect.x + SDL_DEBUGGER_MENU_PADDING;
	for (i = 0; i < SDL_DBG_MENU_BAR_COUNT; i++) {
		int w = sdl_dbg_text_width(dbg, sdl_dbg_menu_bar_labels[i])
		        + SDL_DEBUGGER_MENU_PADDING * 2;
		dbg->menu_bar_items[i].x = x;
		dbg->menu_bar_items[i].y = layout->menu_rect.y;
		dbg->menu_bar_items[i].w = w;
		dbg->menu_bar_items[i].h = layout->menu_rect.h;
		x += w + SDL_DEBUGGER_MENU_SPACING;
	}
}

static TilemSdlMenuBar sdl_dbg_menu_hit_test_bar(TilemSdlDebugger *dbg,
                                                 const TilemSdlDebuggerLayout *layout,
                                                 int x, int y)
{
	int i;

	if (!dbg || !layout)
		return SDL_DBG_MENU_BAR_NONE;
	if (x < layout->menu_rect.x || x >= layout->menu_rect.x + layout->menu_rect.w
	    || y < layout->menu_rect.y || y >= layout->menu_rect.y + layout->menu_rect.h)
		return SDL_DBG_MENU_BAR_NONE;

	sdl_dbg_menu_bar_layout(dbg, layout);
	for (i = 0; i < SDL_DBG_MENU_BAR_COUNT; i++) {
		SDL_Rect rect = dbg->menu_bar_items[i];
		if (x >= rect.x && x < rect.x + rect.w
		    && y >= rect.y && y < rect.y + rect.h)
			return (TilemSdlMenuBar) i;
	}

	return SDL_DBG_MENU_BAR_NONE;
}

static void sdl_dbg_menu_calc_size(TilemSdlDebugger *dbg,
                                   const TilemSdlDbgMenuItem *items,
                                   size_t n_items,
                                   int *out_w,
                                   int *out_h)
{
	size_t i;
	int maxw = 0;
	int icon_w = sdl_dbg_menu_icon_column_width(dbg, items, n_items);
	int item_h = sdl_dbg_menu_item_height(dbg, items, n_items);
	int text_w;

	for (i = 0; i < n_items; i++) {
		if (items[i].separator)
			continue;
		text_w = sdl_dbg_text_width(dbg, items[i].label);
		if (text_w > maxw)
			maxw = text_w;
	}

	if (out_w)
		*out_w = maxw + icon_w + SDL_DEBUGGER_MENU_PADDING * 2;
	if (out_h)
		*out_h = (int) n_items * item_h + SDL_DEBUGGER_MENU_SPACING * 2;
}

static void sdl_dbg_menu_panel_bounds(TilemSdlDebugger *dbg,
                                      const TilemSdlDebuggerLayout *layout,
                                      TilemSdlMenuBar menu,
                                      int *out_x,
                                      int *out_y,
                                      int *out_w,
                                      int *out_h)
{
	const TilemSdlDbgMenuItem *items;
	size_t n_items;
	int x;
	int y;
	int w;
	int h;

	items = sdl_dbg_menu_items(menu, &n_items);
	if (!items || n_items == 0) {
		if (out_x) *out_x = 0;
		if (out_y) *out_y = 0;
		if (out_w) *out_w = 0;
		if (out_h) *out_h = 0;
		return;
	}

	sdl_dbg_menu_bar_layout(dbg, layout);
	x = dbg->menu_bar_items[menu].x;
	y = layout->menu_rect.y + layout->menu_rect.h;
	sdl_dbg_menu_calc_size(dbg, items, n_items, &w, &h);

	if (x + w > dbg->window_width)
		x = MAX(dbg->window_width - w, 0);
	if (y + h > dbg->window_height)
		y = MAX(dbg->window_height - h, 0);

	if (out_x) *out_x = x;
	if (out_y) *out_y = y;
	if (out_w) *out_w = w;
	if (out_h) *out_h = h;
}

static int sdl_dbg_toolbar_layout(TilemSdlDebugger *dbg,
                                  const TilemSdlDebuggerLayout *layout,
                                  TilemSdlDbgToolbarButton *buttons,
                                  int max_buttons)
{
	static const struct {
		TilemSdlDbgMenuAction action;
		const char *label;
	} button_defs[] = {
		{ SDL_DBG_MENU_ACTION_RUN, "Run" },
		{ SDL_DBG_MENU_ACTION_PAUSE, "Pause" },
		{ SDL_DBG_MENU_ACTION_STEP, "Step" },
		{ SDL_DBG_MENU_ACTION_STEP_OVER, "Step Over" },
		{ SDL_DBG_MENU_ACTION_FINISH, "Finish" }
	};
	int count = 0;
	int i;
	int x;

	if (!dbg || !layout || !buttons || max_buttons <= 0)
		return 0;
	if (layout->toolbar_rect.h <= 0)
		return 0;

	x = layout->toolbar_rect.x;
	for (i = 0; i < (int) G_N_ELEMENTS(button_defs); i++) {
		const TilemSdlIcon *icon =
			sdl_dbg_menu_item_icon(dbg, button_defs[i].action);
		int content_w;
		int w;

		content_w = icon ? icon->width
		                 : sdl_dbg_text_width(dbg,
		                                      button_defs[i].label);
		w = content_w + SDL_DEBUGGER_MENU_PADDING * 2;

		buttons[count].action = button_defs[i].action;
		buttons[count].icon = icon;
		buttons[count].label = button_defs[i].label;
		buttons[count].rect.x = x;
		buttons[count].rect.y = layout->toolbar_rect.y;
		buttons[count].rect.w = w;
		buttons[count].rect.h = layout->toolbar_rect.h;

		x += w + SDL_DEBUGGER_ICON_GAP;
		count++;
		if (count >= max_buttons)
			break;
	}

	return count;
}

static int sdl_dbg_menu_hit_test_items(TilemSdlDebugger *dbg,
                                       const TilemSdlDbgMenuItem *items,
                                       size_t n_items,
                                       int menu_x, int menu_y,
                                       int menu_w, int menu_h,
                                       int x, int y)
{
	int rel_y;
	int item_h;
	int index;

	if (!items)
		return -1;
	if (x < menu_x || y < menu_y || x >= menu_x + menu_w
	    || y >= menu_y + menu_h)
		return -1;

	item_h = sdl_dbg_menu_item_height(dbg, items, n_items);
	rel_y = y - menu_y - SDL_DEBUGGER_MENU_SPACING;
	if (rel_y < 0)
		return -1;

	index = rel_y / item_h;
	if (index < 0 || index >= (int) n_items)
		return -1;
	if (items[index].separator)
		return -1;

	return index;
}

static void sdl_dbg_start_goto(TilemSdlDebugger *dbg)
{
	char prefill[SDL_DEBUGGER_LINE_BUFSZ];
	dword addr;
	int addr_width;

	if (!dbg)
		return;

	addr = dbg->disasm_cursor;
	addr_width = dbg->mem_logical ? 4 : 6;
	snprintf(prefill, sizeof(prefill), "%0*X", addr_width, addr);
	sdl_dbg_start_input(dbg, SDL_DBG_INPUT_GOTO, prefill);
}

static void sdl_dbg_handle_menu_action(TilemSdlDebugger *dbg,
                                       TilemSdlDbgMenuAction action)
{
	if (!dbg || !dbg->emu)
		return;
	if (!sdl_dbg_menu_action_enabled(dbg, action))
		return;

	switch (action) {
	case SDL_DBG_MENU_ACTION_RUN:
		tilem_calc_emulator_run(dbg->emu);
		break;
	case SDL_DBG_MENU_ACTION_PAUSE:
		tilem_calc_emulator_pause(dbg->emu);
		break;
	case SDL_DBG_MENU_ACTION_STEP:
		sdl_dbg_step(dbg);
		break;
	case SDL_DBG_MENU_ACTION_STEP_OVER:
		sdl_dbg_step_over(dbg);
		break;
	case SDL_DBG_MENU_ACTION_FINISH:
		sdl_dbg_finish(dbg);
		break;
	case SDL_DBG_MENU_ACTION_BREAKPOINTS:
		sdl_dbg_toggle_breakpoints_dialog(dbg);
		break;
	case SDL_DBG_MENU_ACTION_CLOSE:
		tilem_sdl_debugger_hide(dbg);
		break;
	case SDL_DBG_MENU_ACTION_VIEW_KEYPAD:
		if (dbg->keypad.visible)
			sdl_dbg_keypad_hide(dbg);
		else
			sdl_dbg_keypad_show(dbg);
		break;
	case SDL_DBG_MENU_ACTION_VIEW_LOGICAL:
		sdl_dbg_set_mem_mode(dbg, TRUE);
		break;
	case SDL_DBG_MENU_ACTION_VIEW_ABSOLUTE:
		sdl_dbg_set_mem_mode(dbg, FALSE);
		break;
	case SDL_DBG_MENU_ACTION_GO_ADDRESS:
		sdl_dbg_start_goto(dbg);
		break;
	case SDL_DBG_MENU_ACTION_GO_PC:
		sdl_dbg_go_to_pc(dbg);
		break;
	case SDL_DBG_MENU_ACTION_PREV_STACK:
		sdl_dbg_go_to_stack_pos(dbg, dbg->stack_index - 1);
		break;
	case SDL_DBG_MENU_ACTION_NEXT_STACK:
		sdl_dbg_go_to_stack_pos(dbg, dbg->stack_index + 1);
		break;
	default:
		break;
	}
}

static void sdl_dbg_menu_open(TilemSdlDebugger *dbg,
                              TilemSdlMenuBar menu)
{
	size_t n_items = 0;
	const TilemSdlDbgMenuItem *items;

	if (!dbg)
		return;

	dbg->menu_open = TRUE;
	dbg->menu_active = menu;
	items = sdl_dbg_menu_items(menu, &n_items);
	dbg->menu_selected = sdl_dbg_menu_first_selectable(items, n_items);
}

static gboolean sdl_dbg_menu_handle_key(TilemSdlDebugger *dbg,
                                        SDL_Keycode sym)
{
	const TilemSdlDbgMenuItem *items;
	size_t n_items = 0;
	int idx;
	int i;

	if (!dbg || !dbg->menu_open || dbg->menu_active == SDL_DBG_MENU_BAR_NONE)
		return FALSE;

	items = sdl_dbg_menu_items(dbg->menu_active, &n_items);
	if (!items || n_items == 0) {
		sdl_dbg_menu_close(dbg);
		return TRUE;
	}

	switch (sym) {
	case SDLK_ESCAPE:
		sdl_dbg_menu_close(dbg);
		return TRUE;
	case SDLK_UP:
		if (dbg->menu_selected < 0)
			dbg->menu_selected = sdl_dbg_menu_first_selectable(items,
			                                                   n_items);
		for (i = dbg->menu_selected - 1; i >= 0; i--) {
			if (!items[i].separator) {
				dbg->menu_selected = i;
				break;
			}
		}
		return TRUE;
	case SDLK_DOWN:
		if (dbg->menu_selected < 0)
			dbg->menu_selected = sdl_dbg_menu_first_selectable(items,
			                                                   n_items);
		for (i = dbg->menu_selected + 1; i < (int) n_items; i++) {
			if (!items[i].separator) {
				dbg->menu_selected = i;
				break;
			}
		}
		return TRUE;
	case SDLK_LEFT:
	case SDLK_RIGHT: {
		int dir = (sym == SDLK_RIGHT) ? 1 : -1;
		int next = (int) dbg->menu_active + dir;
		if (next < 0)
			next = SDL_DBG_MENU_BAR_COUNT - 1;
		if (next >= SDL_DBG_MENU_BAR_COUNT)
			next = 0;
		sdl_dbg_menu_open(dbg, (TilemSdlMenuBar) next);
		return TRUE;
	}
	case SDLK_RETURN:
	case SDLK_KP_ENTER:
		idx = dbg->menu_selected;
		if (idx >= 0 && idx < (int) n_items) {
			if (sdl_dbg_menu_action_enabled(dbg,
			                                items[idx].action)) {
				sdl_dbg_menu_close(dbg);
				sdl_dbg_handle_menu_action(
					dbg, items[idx].action);
			}
		}
		return TRUE;
	default:
		return TRUE;
	}
}


static void sdl_dbg_compute_layout(TilemSdlDebugger *dbg,
                                   TilemSdlDebuggerLayout *layout)
{
	int menu_h = sdl_dbg_menu_bar_height(dbg);
	int toolbar_h = 0;
	int toolbar_gap = 0;
	int header_h;
	int content_h;
	int mem_rows = SDL_DEBUGGER_MEM_ROWS;
	int mem_h;
	int top_h;
	int disasm_w;
	int side_w;
	int reg_lines = 9;
	int reg_h;
	int side_x;
	int disasm_icon_w = 0;
	int icon_size;

	if (dbg && dbg->icons) {
		icon_size = sdl_dbg_icon_size(&dbg->icons->db_run);
		icon_size = MAX(icon_size,
		                sdl_dbg_icon_size(&dbg->icons->db_pause));
		icon_size = MAX(icon_size,
		                sdl_dbg_icon_size(&dbg->icons->db_step));
		icon_size = MAX(icon_size,
		                sdl_dbg_icon_size(&dbg->icons->db_step_over));
		icon_size = MAX(icon_size,
		                sdl_dbg_icon_size(&dbg->icons->db_finish));
	}
	toolbar_h = MAX(icon_size, dbg->line_height);
	toolbar_gap = SDL_DEBUGGER_MENU_SPACING;

	header_h = menu_h + toolbar_gap + toolbar_h + SDL_DEBUGGER_MARGIN;
	content_h = dbg->window_height - header_h - SDL_DEBUGGER_MARGIN;

	if (content_h < dbg->line_height * 6)
		content_h = dbg->line_height * 6;

	mem_h = mem_rows * dbg->line_height;
	if (mem_h > content_h / 2) {
		mem_rows = MAX(4, content_h / (dbg->line_height * 3));
		mem_h = mem_rows * dbg->line_height;
	}

	top_h = content_h - mem_h - SDL_DEBUGGER_MARGIN;
	if (top_h < dbg->line_height * 4)
		top_h = dbg->line_height * 4;

	side_w = dbg->window_width / 3;
	if (side_w < 200)
		side_w = 200;

	disasm_w = dbg->window_width - side_w - SDL_DEBUGGER_MARGIN * 3;
	if (disasm_w < 200)
		disasm_w = 200;

	side_x = SDL_DEBUGGER_MARGIN + disasm_w + SDL_DEBUGGER_MARGIN;

	reg_h = reg_lines * dbg->line_height;
	if (reg_h > top_h / 2) {
		reg_lines = MAX(4, (top_h / 2) / dbg->line_height);
		reg_h = reg_lines * dbg->line_height;
	}

	layout->disasm_rect.x = SDL_DEBUGGER_MARGIN;
	layout->disasm_rect.y = header_h;
	layout->disasm_rect.w = disasm_w;
	layout->disasm_rect.h = top_h;

	layout->regs_rect.x = side_x;
	layout->regs_rect.y = header_h;
	layout->regs_rect.w = dbg->window_width - side_x - SDL_DEBUGGER_MARGIN;
	layout->regs_rect.h = reg_h;

	layout->stack_rect.x = side_x;
	layout->stack_rect.y = header_h + reg_h + SDL_DEBUGGER_MARGIN;
	layout->stack_rect.w = dbg->window_width - side_x - SDL_DEBUGGER_MARGIN;
	layout->stack_rect.h = top_h - reg_h - SDL_DEBUGGER_MARGIN;
	if (layout->stack_rect.h < dbg->line_height * 2)
		layout->stack_rect.h = dbg->line_height * 2;

	layout->mem_rect.x = SDL_DEBUGGER_MARGIN;
	layout->mem_rect.y = header_h + top_h + SDL_DEBUGGER_MARGIN;
	layout->mem_rect.w = dbg->window_width - SDL_DEBUGGER_MARGIN * 2;
	layout->mem_rect.h = mem_h;

	layout->menu_rect.x = 0;
	layout->menu_rect.y = 0;
	layout->menu_rect.w = dbg->window_width;
	layout->menu_rect.h = menu_h;

	if (toolbar_h > 0) {
		layout->toolbar_rect.x = SDL_DEBUGGER_MARGIN;
		layout->toolbar_rect.y = menu_h + toolbar_gap;
		layout->toolbar_rect.w = dbg->window_width
		                         - SDL_DEBUGGER_MARGIN * 2;
		layout->toolbar_rect.h = toolbar_h;
	}
	else {
		layout->toolbar_rect.x = 0;
		layout->toolbar_rect.y = 0;
		layout->toolbar_rect.w = 0;
		layout->toolbar_rect.h = 0;
	}

	if (sdl_dbg_has_disasm_icons(dbg)) {
		icon_size = sdl_dbg_icon_size(&dbg->icons->disasm_pc);
		icon_size = MAX(icon_size,
		                sdl_dbg_icon_size(&dbg->icons->disasm_break));
		icon_size = MAX(icon_size,
		                sdl_dbg_icon_size(&dbg->icons->disasm_break_pc));
		if (icon_size > 0)
			disasm_icon_w = icon_size + SDL_DEBUGGER_ICON_GAP;
	}

	layout->disasm_lines = layout->disasm_rect.h / dbg->line_height;
	layout->mem_rows = layout->mem_rect.h / dbg->line_height;
	layout->stack_rows = layout->stack_rect.h / dbg->line_height;
	layout->reg_lines = reg_lines;
	layout->disasm_icon_w = disasm_icon_w;
	layout->toolbar_icon_h = toolbar_h;

	if (layout->disasm_lines > SDL_DEBUGGER_MAX_LINES)
		layout->disasm_lines = SDL_DEBUGGER_MAX_LINES;
	if (layout->mem_rows > SDL_DEBUGGER_MAX_LINES)
		layout->mem_rows = SDL_DEBUGGER_MAX_LINES;
	if (layout->stack_rows > SDL_DEBUGGER_MAX_LINES)
		layout->stack_rows = SDL_DEBUGGER_MAX_LINES;
}

static const TilemSdlIcon *sdl_dbg_pick_disasm_icon(TilemSdlDebugger *dbg,
                                                    gboolean is_pc,
                                                    gboolean is_bp)
{
	if (!dbg || !dbg->icons)
		return NULL;
	if (is_pc && is_bp && dbg->icons->disasm_break_pc.texture)
		return &dbg->icons->disasm_break_pc;
	if (is_bp && dbg->icons->disasm_break.texture)
		return &dbg->icons->disasm_break;
	if (is_pc && dbg->icons->disasm_pc.texture)
		return &dbg->icons->disasm_pc;
	return NULL;
}

static byte sdl_dbg_read_mem_byte(TilemCalc *calc, dword addr)
{
	dword phys;

	phys = (*calc->hw.mem_ltop)(calc, addr & 0xffff);
	return calc->mem[phys];
}

static dword sdl_dbg_read_mem_word(TilemCalc *calc, dword addr)
{
	dword lo = sdl_dbg_read_mem_byte(calc, addr);
	dword hi = sdl_dbg_read_mem_byte(calc, addr + 1);
	return lo | (hi << 8);
}

static byte sdl_dbg_read_mem_byte_physical(TilemCalc *calc, dword addr)
{
	dword limit = calc->hw.romsize + calc->hw.ramsize;

	if (addr >= limit)
		return 0;
	return calc->mem[addr];
}

static dword sdl_dbg_read_mem_word_physical(TilemCalc *calc, dword addr)
{
	dword lo = sdl_dbg_read_mem_byte_physical(calc, addr);
	dword hi = sdl_dbg_read_mem_byte_physical(calc, addr + 1);
	return lo | (hi << 8);
}

static gboolean sdl_dbg_parse_num(TilemSdlDebugger *dbg, const char *s,
                                  dword *out)
{
	const char *n;
	char *e;

	if (!s || !*s)
		return FALSE;

	if (s[0] == '$')
		n = s + 1;
	else if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
		n = s + 2;
	else
		n = s;

	*out = strtol(n, &e, 16);
	if (e != n) {
		if (*e == 'h' || *e == 'H')
			e++;
		if (*e == 0)
			return TRUE;
	}

	if (dbg && dbg->dasm && tilem_disasm_get_label(dbg->dasm, s, out))
		return TRUE;

	return FALSE;
}

static gboolean sdl_dbg_parse_physical(TilemSdlDebugger *dbg, const char *s,
                                       dword *out)
{
	const TilemCalc *calc;
	dword page;
	dword addr;
	char buf[SDL_DEBUGGER_INPUT_MAX];
	char *sep;

	if (!dbg || !dbg->emu || !dbg->emu->calc)
		return FALSE;

	calc = dbg->emu->calc;

	g_strlcpy(buf, s, sizeof(buf));
	sep = strchr(buf, ':');
	if (!sep) {
		if (!sdl_dbg_parse_num(dbg, s, &addr))
			return FALSE;
		*out = addr;
		return TRUE;
	}

	*sep = '\0';
	if (!sdl_dbg_parse_num(dbg, buf, &page)) {
		return FALSE;
	}
	if (!sdl_dbg_parse_num(dbg, sep + 1, &addr)) {
		return FALSE;
	}

	addr &= 0x3fff;
	if (page >= calc->hw.rampagemask) {
		addr += ((page - calc->hw.rampagemask) << 14);
		addr %= calc->hw.ramsize;
		addr += calc->hw.romsize;
	}
	else {
		addr += (page << 14);
		addr %= calc->hw.romsize;
	}

	*out = addr;
	return TRUE;
}

static int sdl_dbg_get_core_bp_type(int type, int mode)
{
	switch (type) {
	case SDL_DBG_BP_LOGICAL:
		switch (mode) {
		case SDL_DBG_BP_READ:
			return TILEM_BREAK_MEM_READ;
		case SDL_DBG_BP_WRITE:
			return TILEM_BREAK_MEM_WRITE;
		case SDL_DBG_BP_EXEC:
			return TILEM_BREAK_MEM_EXEC;
		}
		break;
	case SDL_DBG_BP_PHYSICAL:
		switch (mode) {
		case SDL_DBG_BP_READ:
			return TILEM_BREAK_MEM_READ | TILEM_BREAK_PHYSICAL;
		case SDL_DBG_BP_WRITE:
			return TILEM_BREAK_MEM_WRITE | TILEM_BREAK_PHYSICAL;
		case SDL_DBG_BP_EXEC:
			return TILEM_BREAK_MEM_EXEC | TILEM_BREAK_PHYSICAL;
		}
		break;
	case SDL_DBG_BP_PORT:
		switch (mode) {
		case SDL_DBG_BP_READ:
			return TILEM_BREAK_PORT_READ;
		case SDL_DBG_BP_WRITE:
			return TILEM_BREAK_PORT_WRITE;
		}
		break;
	case SDL_DBG_BP_OPCODE:
		return TILEM_BREAK_EXECUTE;
	}
	return 0;
}

static void sdl_dbg_set_bp(TilemSdlDebugger *dbg,
                           TilemSdlDebugBreakpoint *bp)
{
	int i;
	int t;

	if (!bp || bp->disabled)
		return;

	tilem_calc_emulator_lock(dbg->emu);
	for (i = 0; i < 3; i++) {
		if (bp->mode & (1 << i)) {
			t = sdl_dbg_get_core_bp_type(bp->type, (1 << i));
			if (t) {
				bp->id[i] = tilem_z80_add_breakpoint(
					dbg->emu->calc, t,
					bp->start, bp->end, bp->mask,
					NULL, NULL);
			}
		}
		else {
			bp->id[i] = 0;
		}
	}
	tilem_calc_emulator_unlock(dbg->emu);
}

static void sdl_dbg_unset_bp(TilemSdlDebugger *dbg,
                             TilemSdlDebugBreakpoint *bp)
{
	int i;

	if (!bp || !dbg->emu || !dbg->emu->calc)
		return;

	tilem_calc_emulator_lock(dbg->emu);
	for (i = 0; i < 3; i++) {
		if (bp->id[i])
			tilem_z80_remove_breakpoint(dbg->emu->calc, bp->id[i]);
		bp->id[i] = 0;
	}
	tilem_calc_emulator_unlock(dbg->emu);
}

static gboolean sdl_dbg_is_mem_exec_bp(const TilemSdlDebugBreakpoint *bp)
{
	return (bp->mode & SDL_DBG_BP_EXEC)
	       && (bp->type == SDL_DBG_BP_LOGICAL
	           || bp->type == SDL_DBG_BP_PHYSICAL);
}

static gboolean sdl_dbg_bp_matches_addr(const TilemSdlDebugBreakpoint *bp,
                                        dword addr, gboolean logical)
{
	dword masked;

	if (bp->disabled)
		return FALSE;
	if (logical && bp->type != SDL_DBG_BP_LOGICAL)
		return FALSE;
	if (!logical && bp->type != SDL_DBG_BP_PHYSICAL)
		return FALSE;

	masked = addr & bp->mask;
	return (masked >= bp->start && masked <= bp->end);
}

static int sdl_dbg_find_exec_breakpoint(TilemSdlDebugger *dbg,
                                        dword addr, gboolean logical)
{
	guint i;

	for (i = 0; i < dbg->breakpoints->len; i++) {
		TilemSdlDebugBreakpoint *bp =
			&g_array_index(dbg->breakpoints,
			               TilemSdlDebugBreakpoint, i);
		if (!sdl_dbg_is_mem_exec_bp(bp))
			continue;
		if (sdl_dbg_bp_matches_addr(bp, addr, logical))
			return (int) i;
	}

	return -1;
}

static void sdl_dbg_toggle_breakpoint(TilemSdlDebugger *dbg, dword addr)
{
	int idx;
	TilemSdlDebugBreakpoint bp;

	if (!dbg || !dbg->emu || !dbg->emu->calc)
		return;

	idx = sdl_dbg_find_exec_breakpoint(dbg, addr, dbg->mem_logical);
	if (idx >= 0) {
		bp = g_array_index(dbg->breakpoints,
		                   TilemSdlDebugBreakpoint, idx);
		sdl_dbg_unset_bp(dbg, &bp);
		g_array_remove_index(dbg->breakpoints, idx);
		return;
	}

	memset(&bp, 0, sizeof(bp));
	bp.type = dbg->mem_logical ? SDL_DBG_BP_LOGICAL : SDL_DBG_BP_PHYSICAL;
	bp.mode = SDL_DBG_BP_EXEC;
	bp.mask = (bp.type == SDL_DBG_BP_LOGICAL) ? 0xffff : 0xffffffff;
	bp.start = addr & bp.mask;
	bp.end = bp.start;
	bp.disabled = 0;
	sdl_dbg_set_bp(dbg, &bp);
	g_array_append_val(dbg->breakpoints, bp);
	dbg->last_bp_type = bp.type;
	dbg->last_bp_mode = bp.mode;
}

static void sdl_dbg_clear_breakpoints(TilemSdlDebugger *dbg)
{
	guint i;

	if (!dbg || !dbg->breakpoints)
		return;

	for (i = 0; i < dbg->breakpoints->len; i++) {
		TilemSdlDebugBreakpoint *bp =
			&g_array_index(dbg->breakpoints,
			               TilemSdlDebugBreakpoint, i);
		sdl_dbg_unset_bp(dbg, bp);
	}

	g_array_set_size(dbg->breakpoints, 0);
}

static int sdl_dbg_parse_bp_type(const char *s, int fallback)
{
	if (!s || !*s)
		return fallback;
	if (g_ascii_strcasecmp(s, "logical") == 0
	    || g_ascii_strcasecmp(s, "log") == 0
	    || g_ascii_strcasecmp(s, "l") == 0
	    || g_ascii_strcasecmp(s, "mem") == 0)
		return SDL_DBG_BP_LOGICAL;
	if (g_ascii_strcasecmp(s, "physical") == 0
	    || g_ascii_strcasecmp(s, "phys") == 0
	    || g_ascii_strcasecmp(s, "abs") == 0
	    || g_ascii_strcasecmp(s, "p") == 0)
		return SDL_DBG_BP_PHYSICAL;
	if (g_ascii_strcasecmp(s, "port") == 0
	    || g_ascii_strcasecmp(s, "io") == 0
	    || g_ascii_strcasecmp(s, "i/o") == 0)
		return SDL_DBG_BP_PORT;
	if (g_ascii_strcasecmp(s, "opcode") == 0
	    || g_ascii_strcasecmp(s, "op") == 0)
		return SDL_DBG_BP_OPCODE;
	return fallback;
}

static gboolean sdl_dbg_strcasestr(const char *haystack,
                                   const char *needle)
{
	size_t nlen;
	const char *p;

	if (!haystack || !needle)
		return FALSE;

	nlen = strlen(needle);
	if (nlen == 0)
		return TRUE;

	for (p = haystack; *p; p++) {
		if (g_ascii_strncasecmp(p, needle, nlen) == 0)
			return TRUE;
	}

	return FALSE;
}

static int sdl_dbg_parse_bp_mode(const char *s)
{
	int mode = 0;

	if (!s || !*s)
		return -1;

	if (sdl_dbg_strcasestr(s, "read") || strchr(s, 'r'))
		mode |= SDL_DBG_BP_READ;
	if (sdl_dbg_strcasestr(s, "write") || strchr(s, 'w'))
		mode |= SDL_DBG_BP_WRITE;
	if (sdl_dbg_strcasestr(s, "exec") || strchr(s, 'x'))
		mode |= SDL_DBG_BP_EXEC;

	if (mode == 0)
		return -1;
	return mode;
}

static gboolean sdl_dbg_parse_bp_addr(TilemSdlDebugger *dbg, int type,
                                      const char *token,
                                      dword *start, dword *end,
                                      char *err, size_t err_len)
{
	char buf[SDL_DEBUGGER_INPUT_MAX];
	char *dash;
	const char *first;
	const char *second = NULL;
	gboolean ok;

	g_strlcpy(buf, token, sizeof(buf));
	dash = strchr(buf, '-');
	if (dash) {
		*dash = '\0';
		second = dash + 1;
	}
	first = buf;

	if (type == SDL_DBG_BP_PHYSICAL)
		ok = sdl_dbg_parse_physical(dbg, first, start);
	else
		ok = sdl_dbg_parse_num(dbg, first, start);
	if (!ok) {
		g_snprintf(err, err_len, "Invalid address.");
		return FALSE;
	}

	if (second && *second) {
		if (type == SDL_DBG_BP_PHYSICAL)
			ok = sdl_dbg_parse_physical(dbg, second, end);
		else
			ok = sdl_dbg_parse_num(dbg, second, end);
		if (!ok) {
			g_snprintf(err, err_len, "Invalid end address.");
			return FALSE;
		}
	}
	else {
		*end = *start;
	}

	return TRUE;
}

static gboolean sdl_dbg_parse_breakpoint(TilemSdlDebugger *dbg,
                                         const char *input,
                                         TilemSdlDebugBreakpoint *out,
                                         char *err, size_t err_len)
{
	gchar **tokens;
	int type = dbg->last_bp_type;
	int mode = dbg->last_bp_mode;
	gboolean type_set = FALSE;
	gboolean mode_set = FALSE;
	dword mask = 0;
	gboolean have_mask = FALSE;
	const char *addr_token = NULL;
	int i;

	if (!input || !*input) {
		g_snprintf(err, err_len, "Empty input.");
		return FALSE;
	}

	tokens = g_strsplit_set(input, " \t", -1);
	for (i = 0; tokens[i]; i++) {
		char *tok = g_strstrip(tokens[i]);
		if (!tok[0])
			continue;
		if (g_ascii_strncasecmp(tok, "mask=", 5) == 0) {
			if (!sdl_dbg_parse_num(dbg, tok + 5, &mask)) {
				g_snprintf(err, err_len, "Invalid mask.");
				g_strfreev(tokens);
				return FALSE;
			}
			have_mask = TRUE;
			continue;
		}
		if (g_ascii_strncasecmp(tok, "mask:", 5) == 0) {
			if (!sdl_dbg_parse_num(dbg, tok + 5, &mask)) {
				g_snprintf(err, err_len, "Invalid mask.");
				g_strfreev(tokens);
				return FALSE;
			}
			have_mask = TRUE;
			continue;
		}
		if (!type_set) {
			int parsed_type = sdl_dbg_parse_bp_type(tok, -1);
			if (parsed_type != -1) {
				type = parsed_type;
				type_set = TRUE;
				continue;
			}
		}
		if (!mode_set) {
			int parsed_mode = sdl_dbg_parse_bp_mode(tok);
			if (parsed_mode != -1) {
				mode = parsed_mode;
				mode_set = TRUE;
				continue;
			}
		}
		if (!addr_token)
			addr_token = tok;
	}

	if (!addr_token) {
		g_snprintf(err, err_len, "Missing address.");
		g_strfreev(tokens);
		return FALSE;
	}

	memset(out, 0, sizeof(*out));
	out->type = type;
	out->mode = mode;

	if (out->type == SDL_DBG_BP_OPCODE)
		out->mode = SDL_DBG_BP_EXEC;
	else if (out->type == SDL_DBG_BP_PORT)
		out->mode &= (SDL_DBG_BP_READ | SDL_DBG_BP_WRITE);
	else
		out->mode &= (SDL_DBG_BP_READ | SDL_DBG_BP_WRITE | SDL_DBG_BP_EXEC);

	if (out->type == SDL_DBG_BP_PORT && out->mode == 0)
		out->mode = SDL_DBG_BP_READ;
	if ((out->type == SDL_DBG_BP_LOGICAL
	     || out->type == SDL_DBG_BP_PHYSICAL)
	    && out->mode == 0)
		out->mode = SDL_DBG_BP_EXEC;

	if (out->type != SDL_DBG_BP_OPCODE && out->mode == 0) {
		g_snprintf(err, err_len, "Invalid access mode.");
		g_strfreev(tokens);
		return FALSE;
	}

	if (!sdl_dbg_parse_bp_addr(dbg, out->type, addr_token,
	                           &out->start, &out->end,
	                           err, err_len)) {
		g_strfreev(tokens);
		return FALSE;
	}

	if (out->type == SDL_DBG_BP_LOGICAL)
		out->mask = 0xffff;
	else if (out->type == SDL_DBG_BP_PORT)
		out->mask = 0xff;
	else
		out->mask = 0xffffffff;

	if (have_mask && (out->type == SDL_DBG_BP_PHYSICAL
	                  || out->type == SDL_DBG_BP_OPCODE)) {
		out->mask = mask;
	}

	out->start &= out->mask;
	out->end &= out->mask;

	if (out->end < out->start) {
		g_snprintf(err, err_len, "End < start.");
		g_strfreev(tokens);
		return FALSE;
	}

	g_strfreev(tokens);
	return TRUE;
}

static void sdl_dbg_format_breakpoint(const TilemSdlDebugBreakpoint *bp,
                                      char *buf, size_t buflen)
{
	const char *type_label = "?";
	char mode_buf[4];
	char addr_buf[64];
	int mode_len = 0;
	int addr_width = 4;

	switch (bp->type) {
	case SDL_DBG_BP_LOGICAL:
		type_label = "L";
		break;
	case SDL_DBG_BP_PHYSICAL:
		type_label = "P";
		addr_width = (bp->end > 0xffff) ? 6 : 4;
		break;
	case SDL_DBG_BP_PORT:
		type_label = "IO";
		addr_width = 2;
		break;
	case SDL_DBG_BP_OPCODE:
		type_label = "OP";
		addr_width = (bp->end > 0xffff) ? 6 : 2;
		break;
	default:
		break;
	}

	if (bp->mode & SDL_DBG_BP_READ)
		mode_buf[mode_len++] = 'R';
	if (bp->mode & SDL_DBG_BP_WRITE)
		mode_buf[mode_len++] = 'W';
	if (bp->mode & SDL_DBG_BP_EXEC)
		mode_buf[mode_len++] = 'X';
	mode_buf[mode_len] = '\0';

	if (bp->start == bp->end) {
		g_snprintf(addr_buf, sizeof(addr_buf), "%0*X",
		           addr_width, bp->start);
	}
	else {
		g_snprintf(addr_buf, sizeof(addr_buf), "%0*X-%0*X",
		           addr_width, bp->start,
		           addr_width, bp->end);
	}

	g_snprintf(buf, buflen, "%s %s %s%s",
	           type_label, mode_buf, addr_buf,
	           bp->disabled ? " (disabled)" : "");
}

static void sdl_dbg_start_input(TilemSdlDebugger *dbg,
                                TilemSdlInputMode mode,
                                const char *prefill)
{
	if (!dbg)
		return;

	sdl_dbg_menu_close(dbg);
	dbg->input_mode = mode;
	dbg->input_len = 0;
	dbg->input_buf[0] = '\0';
	dbg->input_error[0] = '\0';

	if (prefill) {
		g_strlcpy(dbg->input_buf, prefill,
		          sizeof(dbg->input_buf));
		dbg->input_len = (int) strlen(dbg->input_buf);
	}

	SDL_StartTextInput();
}

static void sdl_dbg_cancel_input(TilemSdlDebugger *dbg)
{
	if (!dbg)
		return;

	dbg->input_mode = SDL_DBG_INPUT_NONE;
	dbg->input_len = 0;
	dbg->input_buf[0] = '\0';
	dbg->input_error[0] = '\0';
	SDL_StopTextInput();
}

static void sdl_dbg_apply_breakpoint(TilemSdlDebugger *dbg,
                                     const TilemSdlDebugBreakpoint *bp_in,
                                     int index)
{
	TilemSdlDebugBreakpoint bp;

	bp = *bp_in;
	if (index >= 0 && index < (int) dbg->breakpoints->len) {
		TilemSdlDebugBreakpoint old =
			g_array_index(dbg->breakpoints,
			              TilemSdlDebugBreakpoint, index);
		bp.disabled = old.disabled;
		sdl_dbg_unset_bp(dbg, &old);
		sdl_dbg_set_bp(dbg, &bp);
		g_array_index(dbg->breakpoints,
		              TilemSdlDebugBreakpoint, index) = bp;
	}
	else {
		sdl_dbg_set_bp(dbg, &bp);
		g_array_append_val(dbg->breakpoints, bp);
		dbg->bp_selected = (int) dbg->breakpoints->len - 1;
	}

	dbg->last_bp_type = bp.type;
	dbg->last_bp_mode = bp.mode;
}

static void sdl_dbg_accept_input(TilemSdlDebugger *dbg)
{
	TilemSdlDebugBreakpoint bp;
	dword addr;
	gboolean ok = FALSE;

	if (!dbg)
		return;

	if (dbg->input_mode == SDL_DBG_INPUT_GOTO) {
		if (dbg->mem_logical)
			ok = sdl_dbg_parse_num(dbg, dbg->input_buf, &addr);
		else
			ok = sdl_dbg_parse_physical(dbg, dbg->input_buf, &addr);

		if (!ok) {
			g_strlcpy(dbg->input_error, "Invalid address.",
			          sizeof(dbg->input_error));
			return;
		}

		dbg->disasm_follow_pc = FALSE;
		dbg->mem_follow_pc = FALSE;
		dbg->disasm_base = addr;
		dbg->disasm_cursor = addr;
		dbg->mem_addr = addr & ~(SDL_DEBUGGER_MEM_COLS - 1);
		sdl_dbg_cancel_input(dbg);
		return;
	}

	if (dbg->input_mode == SDL_DBG_INPUT_BP_ADD
	    || dbg->input_mode == SDL_DBG_INPUT_BP_EDIT) {
		if (!sdl_dbg_parse_breakpoint(dbg, dbg->input_buf, &bp,
		                              dbg->input_error,
		                              sizeof(dbg->input_error)))
			return;

		if (dbg->input_mode == SDL_DBG_INPUT_BP_EDIT)
			sdl_dbg_apply_breakpoint(dbg, &bp, dbg->input_bp_index);
		else
			sdl_dbg_apply_breakpoint(dbg, &bp, -1);

		sdl_dbg_cancel_input(dbg);
		return;
	}
}

static void sdl_dbg_keypad_show(TilemSdlDebugger *dbg)
{
	if (!dbg)
		return;

	if (!dbg->font && !sdl_dbg_init_ttf(dbg))
		return;

	if (!dbg->keypad.window) {
		dbg->keypad.width = 640;
		dbg->keypad.height = 360;
		dbg->keypad.window = SDL_CreateWindow("Keypad",
		                                      SDL_WINDOWPOS_CENTERED,
		                                      SDL_WINDOWPOS_CENTERED,
		                                      dbg->keypad.width,
		                                      dbg->keypad.height,
		                                      SDL_WINDOW_RESIZABLE);
		if (!dbg->keypad.window) {
			g_printerr("SDL_CreateWindow failed: %s\n",
			           SDL_GetError());
			return;
		}

		dbg->keypad.renderer = SDL_CreateRenderer(dbg->keypad.window,
		                                          -1,
		                                          SDL_RENDERER_ACCELERATED
		                                          | SDL_RENDERER_PRESENTVSYNC);
		if (!dbg->keypad.renderer) {
			dbg->keypad.renderer = SDL_CreateRenderer(
				dbg->keypad.window, -1, 0);
			if (!dbg->keypad.renderer) {
				g_printerr("SDL_CreateRenderer failed: %s\n",
				           SDL_GetError());
				SDL_DestroyWindow(dbg->keypad.window);
				dbg->keypad.window = NULL;
				return;
			}
		}

		dbg->keypad.window_id = SDL_GetWindowID(dbg->keypad.window);
	}
	else {
		SDL_ShowWindow(dbg->keypad.window);
		SDL_RaiseWindow(dbg->keypad.window);
		SDL_GetWindowSize(dbg->keypad.window, &dbg->keypad.width,
		                  &dbg->keypad.height);
	}

	dbg->keypad.visible = TRUE;
}

static void sdl_dbg_keypad_hide(TilemSdlDebugger *dbg)
{
	if (!dbg)
		return;

	if (dbg->keypad.window)
		SDL_HideWindow(dbg->keypad.window);
	dbg->keypad.visible = FALSE;
}

static gboolean sdl_dbg_keypad_event_matches(TilemSdlDebugger *dbg,
                                             const SDL_Event *event)
{
	Uint32 window_id = dbg->keypad.window_id;

	switch (event->type) {
	case SDL_WINDOWEVENT:
		return event->window.windowID == window_id;
	case SDL_MOUSEBUTTONDOWN:
	case SDL_MOUSEBUTTONUP:
		return event->button.windowID == window_id;
	case SDL_MOUSEMOTION:
		return event->motion.windowID == window_id;
	default:
		break;
	}

	return FALSE;
}

static void sdl_dbg_keypad_handle_click(TilemSdlDebugger *dbg,
                                        int x, int y)
{
	int margin = SDL_DEBUGGER_MARGIN;
	int cell_w = MAX(64, dbg->char_width * 6);
	int cell_h = dbg->line_height + 6;
	int grid_x = margin;
	int grid_y = margin + dbg->line_height * 2;
	int out_y = margin;
	int out_h = dbg->line_height + 4;
	int i;
	int j;
	int key;

	if (!dbg->emu || !dbg->emu->calc)
		return;

	if (y >= out_y && y < out_y + out_h) {
		if (!dbg->emu->paused)
			return;
		for (i = 0; i < SDL_DEBUGGER_KEYPAD_ROWS; i++) {
			int rx = grid_x + i * (cell_w + 4);
			if (x >= rx && x < rx + cell_w) {
				tilem_calc_emulator_lock(dbg->emu);
				dbg->emu->calc->keypad.group ^= (1 << i);
				tilem_calc_emulator_unlock(dbg->emu);
				return;
			}
		}
	}

	if (x < grid_x || y < grid_y)
		return;

	i = (x - grid_x) / (cell_w + 4);
	j = (y - grid_y) / (cell_h + 4);
	if (i < 0 || i >= SDL_DEBUGGER_KEYPAD_ROWS
	    || j < 0 || j >= SDL_DEBUGGER_KEYPAD_COLS)
		return;

	key = i * 8 + j + 1;
	if (key == TILEM_KEY_ON)
		return;
	if (!dbg->emu->calc->hw.keynames[key - 1])
		return;

	{
		gboolean down;
		tilem_calc_emulator_lock(dbg->emu);
		down = (dbg->emu->calc->keypad.keysdown[i] & (1 << j)) != 0;
		tilem_calc_emulator_unlock(dbg->emu);
		if (down)
			tilem_calc_emulator_release_key(dbg->emu, key);
		else
			tilem_calc_emulator_press_key(dbg->emu, key);
	}
}

static gboolean sdl_dbg_keypad_handle_event(TilemSdlDebugger *dbg,
                                            const SDL_Event *event)
{
	if (!dbg->keypad.visible || !dbg->keypad.window)
		return FALSE;

	if (!sdl_dbg_keypad_event_matches(dbg, event))
		return FALSE;

	switch (event->type) {
	case SDL_WINDOWEVENT:
		if (event->window.event == SDL_WINDOWEVENT_CLOSE) {
			sdl_dbg_keypad_hide(dbg);
		}
		else if (event->window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
			dbg->keypad.width = event->window.data1;
			dbg->keypad.height = event->window.data2;
		}
		return TRUE;
	case SDL_MOUSEBUTTONDOWN:
		if (event->button.button == SDL_BUTTON_LEFT)
			sdl_dbg_keypad_handle_click(dbg, event->button.x,
			                            event->button.y);
		return TRUE;
	default:
		return TRUE;
	}
}

static void sdl_dbg_keypad_render(TilemSdlDebugger *dbg)
{
	SDL_Color text = { 220, 220, 220, 255 };
	SDL_Color highlight = { 60, 110, 170, 255 };
	SDL_Color border = { 90, 90, 90, 255 };
	SDL_Color bg = { 25, 25, 25, 255 };
	int margin = SDL_DEBUGGER_MARGIN;
	int cell_w = MAX(64, dbg->char_width * 6);
	int cell_h = dbg->line_height + 6;
	int grid_x = margin;
	int grid_y = margin + dbg->line_height * 2;
	int out_y = margin;
	int out_h = dbg->line_height + 4;
	int i;
	int j;
	char label[32];
	byte keys[SDL_DEBUGGER_KEYPAD_ROWS];
	byte outval;
	byte inval;

	if (!dbg->keypad.visible || !dbg->keypad.window)
		return;
	if (!dbg->emu || !dbg->emu->calc)
		return;

	tilem_calc_emulator_lock(dbg->emu);
	for (i = 0; i < SDL_DEBUGGER_KEYPAD_ROWS; i++)
		keys[i] = dbg->emu->calc->keypad.keysdown[i];
	outval = dbg->emu->calc->keypad.group;
	inval = tilem_keypad_read_keys(dbg->emu->calc);
	tilem_calc_emulator_unlock(dbg->emu);

	SDL_SetRenderDrawColor(dbg->keypad.renderer, bg.r, bg.g, bg.b, bg.a);
	SDL_RenderClear(dbg->keypad.renderer);

	sdl_dbg_draw_text(dbg, margin, margin,
	                  "Outputs (click to toggle when paused)", text);

	for (i = 0; i < SDL_DEBUGGER_KEYPAD_ROWS; i++) {
		SDL_Rect rect;
		gboolean active = !(outval & (1 << i));
		rect.x = grid_x + i * (cell_w + 4);
		rect.y = out_y;
		rect.w = cell_w;
		rect.h = out_h;

		if (active) {
			SDL_SetRenderDrawColor(dbg->keypad.renderer,
			                       highlight.r, highlight.g,
			                       highlight.b, highlight.a);
			SDL_RenderFillRect(dbg->keypad.renderer, &rect);
		}

		SDL_SetRenderDrawColor(dbg->keypad.renderer,
		                       border.r, border.g,
		                       border.b, border.a);
		SDL_RenderDrawRect(dbg->keypad.renderer, &rect);

		snprintf(label, sizeof(label), "O%d", i);
		sdl_dbg_draw_text(dbg, rect.x + 6, rect.y + 2,
		                  label, text);
	}

	for (i = 0; i < SDL_DEBUGGER_KEYPAD_ROWS; i++) {
		for (j = 0; j < SDL_DEBUGGER_KEYPAD_COLS; j++) {
			SDL_Rect rect;
			int key = i * 8 + j + 1;
			const char *name = dbg->emu->calc->hw.keynames[key - 1];
			gboolean down = (keys[i] & (1 << j)) != 0;

			rect.x = grid_x + i * (cell_w + 4);
			rect.y = grid_y + j * (cell_h + 4);
			rect.w = cell_w;
			rect.h = cell_h;

			if (name && key != TILEM_KEY_ON) {
				if (down) {
					SDL_SetRenderDrawColor(dbg->keypad.renderer,
					                       highlight.r,
					                       highlight.g,
					                       highlight.b,
					                       highlight.a);
					SDL_RenderFillRect(dbg->keypad.renderer,
					                   &rect);
				}

				SDL_SetRenderDrawColor(dbg->keypad.renderer,
				                       border.r, border.g,
				                       border.b, border.a);
				SDL_RenderDrawRect(dbg->keypad.renderer, &rect);
				sdl_dbg_draw_text(dbg, rect.x + 6, rect.y + 2,
				                  name, text);
			}
		}
	}

	if (dbg->emu->paused) {
		int in_y = grid_y + SDL_DEBUGGER_KEYPAD_COLS * (cell_h + 4)
			+ dbg->line_height;
		sdl_dbg_draw_text(dbg, margin, in_y, "Inputs:", text);
		for (j = 0; j < SDL_DEBUGGER_KEYPAD_COLS; j++) {
			SDL_Rect rect;
			gboolean active = (inval & (1 << j)) != 0;
			rect.x = grid_x + j * (cell_w + 4);
			rect.y = in_y + dbg->line_height;
			rect.w = cell_w;
			rect.h = out_h;

			if (active) {
				SDL_SetRenderDrawColor(dbg->keypad.renderer,
				                       highlight.r,
				                       highlight.g,
				                       highlight.b,
				                       highlight.a);
				SDL_RenderFillRect(dbg->keypad.renderer, &rect);
			}

			SDL_SetRenderDrawColor(dbg->keypad.renderer,
			                       border.r, border.g,
			                       border.b, border.a);
			SDL_RenderDrawRect(dbg->keypad.renderer, &rect);
			snprintf(label, sizeof(label), "I%d", j);
			sdl_dbg_draw_text(dbg, rect.x + 6, rect.y + 2,
			                  label, text);
		}
	}

	SDL_RenderPresent(dbg->keypad.renderer);
}

static void sdl_dbg_set_mem_mode(TilemSdlDebugger *dbg, gboolean logical)
{
	if (!dbg)
		return;
	if (dbg->mem_logical == logical)
		return;

	dbg->mem_logical = logical;
	dbg->disasm_follow_pc = TRUE;
	dbg->mem_follow_pc = TRUE;
	dbg->stack_index = -1;

	tilem_config_set("debugger",
	                 "mem_logical/b", dbg->mem_logical,
	                 NULL);
}

static void sdl_dbg_go_to_pc(TilemSdlDebugger *dbg)
{
	TilemCalc *calc;
	dword addr;

	if (!dbg || !dbg->emu || !dbg->emu->calc)
		return;

	calc = dbg->emu->calc;
	tilem_calc_emulator_lock(dbg->emu);
	if (dbg->mem_logical)
		addr = calc->z80.r.pc.w.l;
	else
		addr = (*calc->hw.mem_ltop)(calc, calc->z80.r.pc.w.l);
	tilem_calc_emulator_unlock(dbg->emu);

	dbg->disasm_follow_pc = FALSE;
	dbg->mem_follow_pc = FALSE;
	dbg->disasm_base = addr;
	dbg->disasm_cursor = addr;
	dbg->mem_addr = addr & ~(SDL_DEBUGGER_MEM_COLS - 1);
	dbg->stack_index = -1;
}

static void sdl_dbg_go_to_stack_pos(TilemSdlDebugger *dbg, int pos)
{
	TilemCalc *calc;
	dword addr;

	if (!dbg || !dbg->emu || !dbg->emu->calc)
		return;

	dbg->stack_index = pos;
	calc = dbg->emu->calc;

	tilem_calc_emulator_lock(dbg->emu);
	if (pos < 0)
		addr = calc->z80.r.pc.w.l;
	else
		addr = sdl_dbg_read_mem_word(calc,
		                             calc->z80.r.sp.w.l + 2 * pos);
	if (!dbg->mem_logical)
		addr = (*calc->hw.mem_ltop)(calc, addr);
	tilem_calc_emulator_unlock(dbg->emu);

	dbg->disasm_follow_pc = FALSE;
	dbg->mem_follow_pc = FALSE;
	dbg->disasm_base = addr;
	dbg->disasm_cursor = addr;
	dbg->mem_addr = addr & ~(SDL_DEBUGGER_MEM_COLS - 1);
}

static void sdl_dbg_toggle_breakpoints_dialog(TilemSdlDebugger *dbg)
{
	if (!dbg)
		return;

	sdl_dbg_menu_close(dbg);
	dbg->show_breakpoints = !dbg->show_breakpoints;
	dbg->input_error[0] = '\0';
	if (!dbg->show_breakpoints)
		sdl_dbg_cancel_input(dbg);
	if (dbg->bp_selected >= (int) dbg->breakpoints->len)
		dbg->bp_selected = (int) dbg->breakpoints->len - 1;
	if (dbg->bp_selected < 0)
		dbg->bp_selected = 0;
}

static void sdl_dbg_render_breakpoints(TilemSdlDebugger *dbg)
{
	SDL_Rect panel;
	SDL_Color text = { 230, 230, 230, 255 };
	SDL_Color highlight = { 60, 110, 170, 255 };
	SDL_Color border = { 90, 90, 90, 255 };
	SDL_Color bg = { 20, 20, 20, 240 };
	int i;
	int start_y;
	int max_lines;
	int line_idx = 0;
	int total = (int) dbg->breakpoints->len;

	if (!dbg->show_breakpoints
	    && dbg->input_mode != SDL_DBG_INPUT_GOTO)
		return;

	panel.x = SDL_DEBUGGER_MARGIN * 2;
	panel.y = SDL_DEBUGGER_MARGIN * 2;
	panel.w = dbg->window_width - SDL_DEBUGGER_MARGIN * 4;
	panel.h = dbg->window_height - SDL_DEBUGGER_MARGIN * 4;

	if (!dbg->show_breakpoints
	    && dbg->input_mode == SDL_DBG_INPUT_GOTO) {
		panel.h = dbg->line_height * 4;
		panel.w = MIN(panel.w, dbg->window_width - SDL_DEBUGGER_MARGIN * 6);
		panel.x = (dbg->window_width - panel.w) / 2;
		panel.y = (dbg->window_height - panel.h) / 2;
	}

	SDL_SetRenderDrawColor(dbg->renderer, bg.r, bg.g, bg.b, bg.a);
	SDL_RenderFillRect(dbg->renderer, &panel);
	SDL_SetRenderDrawColor(dbg->renderer, border.r, border.g, border.b,
	                       border.a);
	SDL_RenderDrawRect(dbg->renderer, &panel);

	start_y = panel.y + SDL_DEBUGGER_MARGIN;

	if (!dbg->show_breakpoints
	    && dbg->input_mode == SDL_DBG_INPUT_GOTO) {
		sdl_dbg_draw_text(dbg, panel.x + SDL_DEBUGGER_MARGIN, start_y,
		                  "Go to address:", text);
		sdl_dbg_draw_text(dbg, panel.x + SDL_DEBUGGER_MARGIN,
		                  start_y + dbg->line_height,
		                  dbg->input_buf, text);
		if (dbg->input_error[0]) {
			sdl_dbg_draw_text(dbg, panel.x + SDL_DEBUGGER_MARGIN,
			                  start_y + dbg->line_height * 2,
			                  dbg->input_error, text);
		}
		return;
	}
	sdl_dbg_draw_text(dbg, panel.x + SDL_DEBUGGER_MARGIN, start_y,
	                  "Breakpoints", text);
	sdl_dbg_draw_text(dbg, panel.x + SDL_DEBUGGER_MARGIN,
	                  start_y + dbg->line_height,
	                  "A add  E edit  D delete  T toggle  Esc close",
	                  text);
	start_y += dbg->line_height * 2 + SDL_DEBUGGER_MARGIN;

	max_lines = (panel.h - dbg->line_height * 4) / dbg->line_height;
	if (max_lines > SDL_DEBUGGER_BP_LINES)
		max_lines = SDL_DEBUGGER_BP_LINES;
	if (max_lines < 1)
		max_lines = 1;

	if (total == 0) {
		sdl_dbg_draw_text(dbg, panel.x + SDL_DEBUGGER_MARGIN, start_y,
		                  "No breakpoints.", text);
	}

	for (i = 0; i < total && line_idx < max_lines; i++) {
		char line[SDL_DEBUGGER_LINE_BUFSZ];
		int y = start_y + line_idx * dbg->line_height;
		SDL_Rect hl;
		TilemSdlDebugBreakpoint *bp =
			&g_array_index(dbg->breakpoints,
			               TilemSdlDebugBreakpoint, i);

		sdl_dbg_format_breakpoint(bp, line, sizeof(line));
		if (i == dbg->bp_selected) {
			hl = panel;
			hl.y = y;
			hl.h = dbg->line_height;
			SDL_SetRenderDrawColor(dbg->renderer,
			                       highlight.r, highlight.g,
			                       highlight.b, highlight.a);
			SDL_RenderFillRect(dbg->renderer, &hl);
		}
		sdl_dbg_draw_text(dbg, panel.x + SDL_DEBUGGER_MARGIN, y,
		                  line, text);
		line_idx++;
	}

	if (dbg->input_mode == SDL_DBG_INPUT_BP_ADD
	    || dbg->input_mode == SDL_DBG_INPUT_BP_EDIT) {
		int y = panel.y + panel.h - dbg->line_height * 2;
		sdl_dbg_draw_text(dbg, panel.x + SDL_DEBUGGER_MARGIN, y,
		                  "Format: <type> <mode> <addr[-end]> [mask=XXXX]",
		                  text);
		sdl_dbg_draw_text(dbg, panel.x + SDL_DEBUGGER_MARGIN,
		                  y + dbg->line_height,
		                  dbg->input_buf, text);
	}

	if (dbg->input_mode == SDL_DBG_INPUT_GOTO) {
		int y = panel.y + panel.h - dbg->line_height * 2;
		sdl_dbg_draw_text(dbg, panel.x + SDL_DEBUGGER_MARGIN, y,
		                  "Go to address:", text);
		sdl_dbg_draw_text(dbg, panel.x + SDL_DEBUGGER_MARGIN,
		                  y + dbg->line_height,
		                  dbg->input_buf, text);
	}

	if (dbg->input_error[0]) {
		int y = panel.y + panel.h - dbg->line_height * 3;
		sdl_dbg_draw_text(dbg, panel.x + SDL_DEBUGGER_MARGIN, y,
		                  dbg->input_error, text);
	}
}

static const char *sdl_dbg_get_sys_name(const TilemCalc *calc)
{
	g_return_val_if_fail(calc != NULL, NULL);

	switch (calc->hw.model_id) {
	case TILEM_CALC_TI83:
	case TILEM_CALC_TI76:
		return "ti83";

	case TILEM_CALC_TI83P:
	case TILEM_CALC_TI83P_SE:
	case TILEM_CALC_TI84P:
	case TILEM_CALC_TI84P_SE:
	case TILEM_CALC_TI84P_NSPIRE:
		return "ti83p";

	default:
		return calc->hw.name;
	}
}

static void sdl_dbg_load_default_symbols(TilemSdlDebugger *dbg)
{
	char *base;
	char *path;
	char *dname;
	const char *errstr;
	FILE *symfile;

	if (!dbg || !dbg->emu || !dbg->emu->calc || !dbg->dasm)
		return;

	base = g_strdup_printf("%s.sym", sdl_dbg_get_sys_name(dbg->emu->calc));
	path = get_shared_file_path("symbols", base, NULL);
	g_free(base);
	if (!path)
		return;

	symfile = g_fopen(path, "rb");
	if (!symfile) {
		errstr = g_strerror(errno);
		dname = g_filename_display_name(path);
		SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,
		                         "Debugger",
		                         "Unable to read symbols file.",
		                         dbg->window);
		g_printerr("Unable to read %s: %s\n", dname, errstr);
		g_free(dname);
		g_free(path);
		return;
	}

	tilem_disasm_read_symbol_file(dbg->dasm, symfile);

	fclose(symfile);
	g_free(path);
}

static void sdl_dbg_cancel_step_bp(TilemSdlDebugger *dbg)
{
	if (!dbg || !dbg->step_bp || !dbg->emu || !dbg->emu->calc)
		return;

	tilem_calc_emulator_lock(dbg->emu);
	tilem_z80_remove_breakpoint(dbg->emu->calc, dbg->step_bp);
	tilem_calc_emulator_unlock(dbg->emu);
	dbg->step_bp = 0;
}

static int sdl_dbg_bptest_step(TilemCalc *calc, dword op, void *data)
{
	(void) data;

	if (op != 0x76 && (op & ~0x2000) != 0xdd76)
		return 1;
	else if (calc->z80.interrupts != 0 && calc->z80.r.iff1)
		return 1;
	else
		return 0;
}

static int sdl_dbg_bptest_step_over(TilemCalc *calc, dword op, void *data)
{
	TilemSdlDebugger *dbg = data;
	dword destaddr;

	if ((op & ~0x20ff) == 0xdd00)
		op &= 0xff;

	if (op == 0xc3 || op == 0xc9 || op == 0xe9
	    || (op & ~0x38) == 0
	    || (op & ~0x38) == 0xc2
	    || (op & ~0x38) == 0xc0
	    || (op & ~0x38) == 0xed45)
		destaddr = calc->z80.r.pc.d;
	else
		destaddr = dbg->step_next_addr;

	destaddr &= 0xffff;

	tilem_z80_remove_breakpoint(calc, dbg->step_bp);
	dbg->step_bp = tilem_z80_add_breakpoint(calc, TILEM_BREAK_MEM_EXEC,
	                                        destaddr, destaddr, 0xffff,
	                                        NULL, NULL);
	return 0;
}

static int sdl_dbg_bptest_finish(TilemCalc *calc, dword op, void *data)
{
	dword exitsp = TILEM_PTR_TO_DWORD(data);
	byte f;

	if (calc->z80.r.sp.w.l <= exitsp)
		return 0;

	if ((op & ~0x20ff) == 0xdd00)
		op &= 0xff;

	f = calc->z80.r.af.b.l;

	switch (op) {
	case 0xc9:
	case 0xe9:
	case 0xed45:
	case 0xed4d:
	case 0xed55:
	case 0xed5d:
	case 0xed65:
	case 0xed6d:
	case 0xed75:
	case 0xed7d:
		return 1;
	case 0xc0: return !(f & 0x40);
	case 0xc8: return (f & 0x40);
	case 0xd0: return !(f & 0x01);
	case 0xd8: return (f & 0x01);
	case 0xe0: return !(f & 0x04);
	case 0xe8: return (f & 0x04);
	case 0xf0: return !(f & 0x80);
	case 0xf8: return (f & 0x80);
	default:
		return 0;
	}
}

static void sdl_dbg_run_with_step_condition(TilemSdlDebugger *dbg,
                                            TilemZ80BreakpointFunc func,
                                            void *data)
{
	if (!dbg || !dbg->emu || !dbg->emu->calc)
		return;

	tilem_calc_emulator_lock(dbg->emu);
	dbg->step_bp = tilem_z80_add_breakpoint(dbg->emu->calc,
	                                        TILEM_BREAK_EXECUTE,
	                                        0, 0, 0, func, data);
	tilem_calc_emulator_unlock(dbg->emu);
	tilem_calc_emulator_run(dbg->emu);
}

static void sdl_dbg_step(TilemSdlDebugger *dbg)
{
	if (!dbg || !dbg->emu || !dbg->emu->calc || !dbg->emu->paused)
		return;

	sdl_dbg_cancel_step_bp(dbg);
	sdl_dbg_run_with_step_condition(dbg, &sdl_dbg_bptest_step, NULL);
}

static void sdl_dbg_step_over(TilemSdlDebugger *dbg)
{
	if (!dbg || !dbg->emu || !dbg->emu->calc || !dbg->emu->paused)
		return;

	sdl_dbg_cancel_step_bp(dbg);
	tilem_calc_emulator_lock(dbg->emu);
	tilem_disasm_disassemble(dbg->dasm, dbg->emu->calc, 0,
	                         dbg->emu->calc->z80.r.pc.w.l,
	                         &dbg->step_next_addr,
	                         NULL, 0);
	tilem_calc_emulator_unlock(dbg->emu);

	sdl_dbg_run_with_step_condition(dbg, &sdl_dbg_bptest_step_over, dbg);
}

static void sdl_dbg_finish(TilemSdlDebugger *dbg)
{
	dword sp;

	if (!dbg || !dbg->emu || !dbg->emu->calc || !dbg->emu->paused)
		return;

	sdl_dbg_cancel_step_bp(dbg);
	tilem_calc_emulator_lock(dbg->emu);
	sp = dbg->emu->calc->z80.r.sp.w.l;
	tilem_calc_emulator_unlock(dbg->emu);

	sdl_dbg_run_with_step_condition(dbg, &sdl_dbg_bptest_finish,
	                                TILEM_DWORD_TO_PTR(sp));
}

static dword sdl_dbg_disasm_prev_addr(TilemCalc *calc, TilemDisasm *dasm,
                                      dword addr, gboolean logical)
{
	dword start;
	dword cur;
	dword next;
	dword best = addr;

	if (addr == 0)
		return 0;

	start = addr > SDL_DEBUGGER_MAX_DISASM_BACK
		? addr - SDL_DEBUGGER_MAX_DISASM_BACK
		: 0;

	cur = start;
	while (cur < addr) {
		tilem_disasm_disassemble(dasm, calc, logical ? 0 : 1,
		                         cur, &next, NULL, 0);
		if (next == addr)
			return cur;
		if (next <= cur)
			break;
		best = cur;
		cur = next;
	}

	if (addr > 0)
		return addr - 1;

	return best;
}

static dword sdl_dbg_disasm_next_addr(TilemCalc *calc, TilemDisasm *dasm,
                                      dword addr, gboolean logical)
{
	dword next = addr + 1;

	tilem_disasm_disassemble(dasm, calc, logical ? 0 : 1,
	                         addr, &next, NULL, 0);
	if (next <= addr)
		next = addr + 1;

	return next;
}

static dword sdl_dbg_address_at_line(TilemCalc *calc, TilemDisasm *dasm,
                                     dword base, int line,
                                     gboolean logical)
{
	dword addr = base;
	int i;

	for (i = 0; i < line; i++) {
		addr = sdl_dbg_disasm_next_addr(calc, dasm, addr, logical);
	}

	return addr;
}

static int sdl_dbg_find_visible_line(TilemCalc *calc, TilemDisasm *dasm,
                                     dword base, int lines, dword addr,
                                     gboolean logical)
{
	dword cur = base;
	int i;

	for (i = 0; i < lines; i++) {
		if ((cur & 0xffff) == (addr & 0xffff))
			return i;
		cur = sdl_dbg_disasm_next_addr(calc, dasm, cur, logical);
	}

	return -1;
}

static void sdl_dbg_scroll_lines(TilemSdlDebugger *dbg, int delta,
                                 int lines)
{
	TilemCalc *calc;
	int i;

	if (!dbg || !dbg->emu || !dbg->emu->calc)
		return;

	calc = dbg->emu->calc;
	tilem_calc_emulator_lock(dbg->emu);
	if (delta > 0) {
		for (i = 0; i < delta; i++)
			dbg->disasm_base = sdl_dbg_disasm_next_addr(
				calc, dbg->dasm, dbg->disasm_base,
				dbg->mem_logical);
	}
	else if (delta < 0) {
		for (i = 0; i < -delta; i++)
			dbg->disasm_base = sdl_dbg_disasm_prev_addr(
				calc, dbg->dasm, dbg->disasm_base,
				dbg->mem_logical);
	}
	tilem_calc_emulator_unlock(dbg->emu);

	if (sdl_dbg_find_visible_line(calc, dbg->dasm, dbg->disasm_base,
	                              lines, dbg->disasm_cursor,
	                              dbg->mem_logical) < 0) {
		dbg->disasm_cursor = dbg->disasm_base;
	}

	dbg->disasm_follow_pc = FALSE;
}

static void sdl_dbg_adjust_mem(TilemSdlDebugger *dbg, int rows)
{
	dword addr;

	addr = dbg->mem_addr + (dword) (rows * SDL_DEBUGGER_MEM_COLS);
	if (dbg->mem_logical)
		dbg->mem_addr = addr & 0xffff;
	else
		dbg->mem_addr = addr;
	dbg->mem_follow_pc = FALSE;
}

static gboolean sdl_dbg_event_matches(TilemSdlDebugger *dbg,
                                      const SDL_Event *event)
{
	Uint32 window_id = dbg->window_id;

	switch (event->type) {
	case SDL_WINDOWEVENT:
		return event->window.windowID == window_id;
	case SDL_MOUSEBUTTONDOWN:
	case SDL_MOUSEBUTTONUP:
		return event->button.windowID == window_id;
	case SDL_MOUSEMOTION:
		return event->motion.windowID == window_id;
	case SDL_MOUSEWHEEL:
		return event->wheel.windowID == window_id;
	case SDL_KEYDOWN:
	case SDL_KEYUP:
		return event->key.windowID == window_id;
	case SDL_TEXTINPUT:
		return event->text.windowID == window_id;
	default:
		break;
	}

	return FALSE;
}

TilemSdlDebugger *tilem_sdl_debugger_new(TilemCalcEmulator *emu)
{
	TilemSdlDebugger *dbg;
	gboolean mem_logical = TRUE;

	g_return_val_if_fail(emu != NULL, NULL);

	dbg = g_new0(TilemSdlDebugger, 1);
	dbg->emu = emu;
	dbg->dasm = tilem_disasm_new();
	dbg->breakpoints = g_array_new(FALSE, FALSE,
	                               sizeof(TilemSdlDebugBreakpoint));
	dbg->disasm_follow_pc = TRUE;
	dbg->mem_follow_pc = TRUE;
	dbg->stack_index = -1;
	dbg->last_bp_type = SDL_DBG_BP_LOGICAL;
	dbg->last_bp_mode = SDL_DBG_BP_EXEC;
	dbg->show_breakpoints = FALSE;
	dbg->bp_selected = 0;
	dbg->input_mode = SDL_DBG_INPUT_NONE;
	dbg->menu_open = FALSE;
	dbg->menu_active = SDL_DBG_MENU_BAR_NONE;
	dbg->menu_selected = -1;

	tilem_config_get("debugger", "mem_logical/b=1", &mem_logical, NULL);
	dbg->mem_logical = mem_logical;

	return dbg;
}

void tilem_sdl_debugger_free(TilemSdlDebugger *dbg)
{
	if (!dbg)
		return;

	sdl_dbg_clear_breakpoints(dbg);

	if (dbg->keypad.window) {
		SDL_DestroyRenderer(dbg->keypad.renderer);
		SDL_DestroyWindow(dbg->keypad.window);
		dbg->keypad.renderer = NULL;
		dbg->keypad.window = NULL;
		dbg->keypad.visible = FALSE;
	}

	if (dbg->icons) {
		tilem_sdl_icons_free(dbg->icons);
		dbg->icons = NULL;
	}

	if (dbg->window) {
		tilem_config_set("debugger",
		                 "width/i", dbg->window_width,
		                 "height/i", dbg->window_height,
		                 NULL);
		SDL_DestroyRenderer(dbg->renderer);
		SDL_DestroyWindow(dbg->window);
		dbg->renderer = NULL;
		dbg->window = NULL;
	}

	if (dbg->dasm)
		tilem_disasm_free(dbg->dasm);
	g_array_free(dbg->breakpoints, TRUE);

	sdl_dbg_shutdown_ttf(dbg);
	g_free(dbg);
}

void tilem_sdl_debugger_show(TilemSdlDebugger *dbg)
{
	int defwidth = SDL_DEBUGGER_DEFAULT_WIDTH;
	int defheight = SDL_DEBUGGER_DEFAULT_HEIGHT;

	if (!dbg)
		return;
	if (!dbg->emu || !dbg->emu->calc)
		return;

	if (!dbg->font && !sdl_dbg_init_ttf(dbg))
		return;

	if (!dbg->window) {
		tilem_config_get("debugger",
		                 "width/i", &defwidth,
		                 "height/i", &defheight,
		                 NULL);
		if (defwidth <= 0 || defheight <= 0) {
			defwidth = SDL_DEBUGGER_DEFAULT_WIDTH;
			defheight = SDL_DEBUGGER_DEFAULT_HEIGHT;
		}

		dbg->window = SDL_CreateWindow(SDL_DEBUGGER_TITLE,
		                               SDL_WINDOWPOS_CENTERED,
		                               SDL_WINDOWPOS_CENTERED,
		                               defwidth, defheight,
		                               SDL_WINDOW_RESIZABLE);
		if (!dbg->window) {
			g_printerr("SDL_CreateWindow failed: %s\n", SDL_GetError());
			return;
		}

		dbg->renderer = SDL_CreateRenderer(dbg->window, -1,
		                                  SDL_RENDERER_ACCELERATED
		                                  | SDL_RENDERER_PRESENTVSYNC);
		if (!dbg->renderer) {
			dbg->renderer = SDL_CreateRenderer(dbg->window, -1, 0);
			if (!dbg->renderer) {
				g_printerr("SDL_CreateRenderer failed: %s\n",
				           SDL_GetError());
				SDL_DestroyWindow(dbg->window);
				dbg->window = NULL;
				return;
			}
		}

		dbg->icons = tilem_sdl_icons_load(dbg->renderer);
		if (dbg->icons && dbg->icons->app_surface)
			SDL_SetWindowIcon(dbg->window,
			                  dbg->icons->app_surface);

		dbg->window_id = SDL_GetWindowID(dbg->window);
		SDL_GetWindowSize(dbg->window,
		                  &dbg->window_width,
		                  &dbg->window_height);
	}
	else {
		SDL_ShowWindow(dbg->window);
		SDL_RaiseWindow(dbg->window);
	}

	dbg->visible = TRUE;
	dbg->resume_on_hide = !dbg->emu->paused;
	tilem_calc_emulator_pause(dbg->emu);
	sdl_dbg_cancel_step_bp(dbg);
	sdl_dbg_menu_close(dbg);

	sdl_dbg_load_default_symbols(dbg);

	dbg->disasm_follow_pc = TRUE;
	dbg->mem_follow_pc = TRUE;
}

void tilem_sdl_debugger_hide(TilemSdlDebugger *dbg)
{
	if (!dbg)
		return;

	if (dbg->input_mode != SDL_DBG_INPUT_NONE)
		sdl_dbg_cancel_input(dbg);

	sdl_dbg_menu_close(dbg);

	if (dbg->keypad.visible)
		sdl_dbg_keypad_hide(dbg);

	if (dbg->window) {
		SDL_GetWindowSize(dbg->window, &dbg->window_width,
		                  &dbg->window_height);
		tilem_config_set("debugger",
		                 "width/i", dbg->window_width,
		                 "height/i", dbg->window_height,
		                 NULL);
		SDL_HideWindow(dbg->window);
	}
	dbg->visible = FALSE;
	dbg->show_breakpoints = FALSE;

	if (dbg->resume_on_hide && dbg->emu)
		tilem_calc_emulator_run(dbg->emu);
}

gboolean tilem_sdl_debugger_visible(TilemSdlDebugger *dbg)
{
	return dbg && dbg->visible;
}

void tilem_sdl_debugger_calc_changed(TilemSdlDebugger *dbg)
{
	if (!dbg)
		return;

	sdl_dbg_clear_breakpoints(dbg);
	sdl_dbg_cancel_step_bp(dbg);
	dbg->stack_index = -1;

	dbg->disasm_follow_pc = TRUE;
	dbg->mem_follow_pc = TRUE;

	sdl_dbg_load_default_symbols(dbg);
}

static void sdl_dbg_handle_key(TilemSdlDebugger *dbg,
                               const SDL_KeyboardEvent *event)
{
	TilemSdlDebuggerLayout layout;
	SDL_Keycode sym;
	Uint16 mods;
	int lines;
	int idx;

	if (event->repeat)
		return;

	sym = event->keysym.sym;
	mods = event->keysym.mod;
	sdl_dbg_compute_layout(dbg, &layout);
	lines = layout.disasm_lines;
	if (lines <= 0)
		lines = 1;

	if (dbg->menu_open && sdl_dbg_menu_handle_key(dbg, sym))
		return;

	if (dbg->input_mode != SDL_DBG_INPUT_NONE) {
		if (sym == SDLK_ESCAPE) {
			sdl_dbg_cancel_input(dbg);
			return;
		}
		if (sym == SDLK_RETURN || sym == SDLK_KP_ENTER) {
			sdl_dbg_accept_input(dbg);
			return;
		}
		if (sym == SDLK_BACKSPACE) {
			if (dbg->input_len > 0) {
				dbg->input_len--;
				dbg->input_buf[dbg->input_len] = '\0';
			}
			return;
		}
		return;
	}

	if (dbg->show_breakpoints) {
		if (sym == SDLK_ESCAPE) {
			sdl_dbg_toggle_breakpoints_dialog(dbg);
			return;
		}
		if (sym == SDLK_UP) {
			if (dbg->bp_selected > 0)
				dbg->bp_selected--;
			return;
		}
		if (sym == SDLK_DOWN) {
			if (dbg->bp_selected + 1
			    < (int) dbg->breakpoints->len)
				dbg->bp_selected++;
			return;
		}
		if (sym == SDLK_a) {
			char prefill[SDL_DEBUGGER_LINE_BUFSZ];
			dword addr = dbg->disasm_cursor;
			int addr_width = dbg->mem_logical ? 4 : 6;
			snprintf(prefill, sizeof(prefill),
			         "%s x %0*X",
			         dbg->mem_logical ? "logical" : "physical",
			         addr_width, addr);
			dbg->input_bp_index = -1;
			sdl_dbg_start_input(dbg, SDL_DBG_INPUT_BP_ADD, prefill);
			return;
		}
		if (sym == SDLK_e) {
			if (dbg->bp_selected < (int) dbg->breakpoints->len) {
				TilemSdlDebugBreakpoint *bp =
					&g_array_index(dbg->breakpoints,
					               TilemSdlDebugBreakpoint,
					               dbg->bp_selected);
				char prefill[SDL_DEBUGGER_LINE_BUFSZ];
				sdl_dbg_format_breakpoint(bp, prefill,
				                          sizeof(prefill));
				dbg->input_bp_index = dbg->bp_selected;
				sdl_dbg_start_input(dbg,
				                    SDL_DBG_INPUT_BP_EDIT,
				                    prefill);
			}
			return;
		}
		if (sym == SDLK_d || sym == SDLK_DELETE) {
			if (dbg->bp_selected < (int) dbg->breakpoints->len) {
				TilemSdlDebugBreakpoint bp =
					g_array_index(dbg->breakpoints,
					              TilemSdlDebugBreakpoint,
					              dbg->bp_selected);
				sdl_dbg_unset_bp(dbg, &bp);
				g_array_remove_index(dbg->breakpoints,
				                     dbg->bp_selected);
				if (dbg->bp_selected >= (int) dbg->breakpoints->len)
					dbg->bp_selected =
						(int) dbg->breakpoints->len - 1;
				if (dbg->bp_selected < 0)
					dbg->bp_selected = 0;
			}
			return;
		}
		if (sym == SDLK_t || sym == SDLK_SPACE) {
			if (dbg->bp_selected < (int) dbg->breakpoints->len) {
				TilemSdlDebugBreakpoint *bp =
					&g_array_index(dbg->breakpoints,
					               TilemSdlDebugBreakpoint,
					               dbg->bp_selected);
				if (bp->disabled) {
					bp->disabled = 0;
					sdl_dbg_set_bp(dbg, bp);
				}
				else {
					bp->disabled = 1;
					sdl_dbg_unset_bp(dbg, bp);
				}
			}
			return;
		}
		return;
	}

	if (!sdl_dbg_is_paused(dbg)) {
		if (sym == SDLK_ESCAPE) {
			tilem_calc_emulator_pause(dbg->emu);
			return;
		}
		if ((mods & KMOD_CTRL) && sym == SDLK_k) {
			if (dbg->keypad.visible)
				sdl_dbg_keypad_hide(dbg);
			else
				sdl_dbg_keypad_show(dbg);
			return;
		}
		return;
	}

	if ((mods & KMOD_CTRL) && sym == SDLK_b) {
		sdl_dbg_toggle_breakpoints_dialog(dbg);
		return;
	}
	if ((mods & KMOD_CTRL) && sym == SDLK_l) {
		sdl_dbg_start_goto(dbg);
		return;
	}
	if ((mods & KMOD_CTRL) && sym == SDLK_m) {
		sdl_dbg_set_mem_mode(dbg, !dbg->mem_logical);
		return;
	}
	if ((mods & KMOD_CTRL) && sym == SDLK_k) {
		if (dbg->keypad.visible)
			sdl_dbg_keypad_hide(dbg);
		else
			sdl_dbg_keypad_show(dbg);
		return;
	}
	if ((mods & KMOD_ALT) && sym == SDLK_HOME) {
		sdl_dbg_go_to_pc(dbg);
		return;
	}
	if ((mods & KMOD_ALT) && sym == SDLK_PAGEUP) {
		sdl_dbg_go_to_stack_pos(dbg, dbg->stack_index - 1);
		return;
	}
	if ((mods & KMOD_ALT) && sym == SDLK_PAGEDOWN) {
		sdl_dbg_go_to_stack_pos(dbg, dbg->stack_index + 1);
		return;
	}

	switch (sym) {
	case SDLK_ESCAPE:
		tilem_calc_emulator_pause(dbg->emu);
		return;
	case SDLK_F5:
		tilem_calc_emulator_run(dbg->emu);
		return;
	case SDLK_F7:
		sdl_dbg_step(dbg);
		return;
	case SDLK_F8:
		sdl_dbg_step_over(dbg);
		return;
	case SDLK_F9:
		sdl_dbg_finish(dbg);
		return;
	case SDLK_F2:
		sdl_dbg_toggle_breakpoint(dbg, dbg->disasm_cursor);
		return;
	case SDLK_HOME:
		sdl_dbg_go_to_pc(dbg);
		return;
	case SDLK_PAGEUP:
		sdl_dbg_scroll_lines(dbg, -lines, lines);
		return;
	case SDLK_PAGEDOWN:
		sdl_dbg_scroll_lines(dbg, lines, lines);
		return;
	case SDLK_UP:
		if (event->keysym.mod & KMOD_CTRL) {
			sdl_dbg_adjust_mem(dbg, -1);
			return;
		}
		break;
	case SDLK_DOWN:
		if (event->keysym.mod & KMOD_CTRL) {
			sdl_dbg_adjust_mem(dbg, 1);
			return;
		}
		break;
	default:
		break;
	}

	if (!dbg->emu || !dbg->emu->calc)
		return;

	if (sym == SDLK_UP || sym == SDLK_DOWN) {
		tilem_calc_emulator_lock(dbg->emu);
		if (sym == SDLK_UP) {
			dbg->disasm_cursor = sdl_dbg_disasm_prev_addr(
				dbg->emu->calc, dbg->dasm,
				dbg->disasm_cursor, dbg->mem_logical);
		}
		else {
			dbg->disasm_cursor = sdl_dbg_disasm_next_addr(
				dbg->emu->calc, dbg->dasm,
				dbg->disasm_cursor, dbg->mem_logical);
		}
		idx = sdl_dbg_find_visible_line(dbg->emu->calc, dbg->dasm,
		                                dbg->disasm_base,
		                                lines, dbg->disasm_cursor,
		                                dbg->mem_logical);
		if (idx < 0)
			dbg->disasm_base = dbg->disasm_cursor;
		tilem_calc_emulator_unlock(dbg->emu);
		dbg->disasm_follow_pc = FALSE;
	}
}

static int sdl_dbg_line_from_y(TilemSdlDebugger *dbg, SDL_Rect rect, int y)
{
	int rel = y - rect.y;
	int line;

	if (rel < 0)
		return -1;

	line = rel / dbg->line_height;
	if (line < 0)
		return -1;

	return line;
}

static gboolean sdl_dbg_handle_menu_mouse(TilemSdlDebugger *dbg,
                                          const SDL_Event *event)
{
	TilemSdlDebuggerLayout layout;
	TilemSdlMenuBar bar;
	const TilemSdlDbgMenuItem *items;
	size_t n_items = 0;
	int menu_x;
	int menu_y;
	int menu_w;
	int menu_h;
	int idx;
	int x;
	int y;

	if (!dbg || !event)
		return FALSE;
	if (dbg->input_mode != SDL_DBG_INPUT_NONE || dbg->show_breakpoints)
		return FALSE;

	if (event->type == SDL_MOUSEMOTION) {
		x = event->motion.x;
		y = event->motion.y;
	}
	else if (event->type == SDL_MOUSEBUTTONDOWN) {
		if (event->button.button != SDL_BUTTON_LEFT)
			return FALSE;
		x = event->button.x;
		y = event->button.y;
	}
	else {
		return FALSE;
	}

	sdl_dbg_compute_layout(dbg, &layout);

	if (event->type == SDL_MOUSEBUTTONDOWN) {
		bar = sdl_dbg_menu_hit_test_bar(dbg, &layout, x, y);
		if (bar != SDL_DBG_MENU_BAR_NONE) {
			if (dbg->menu_open && dbg->menu_active == bar)
				sdl_dbg_menu_close(dbg);
			else
				sdl_dbg_menu_open(dbg, bar);
			return TRUE;
		}

		if (!dbg->menu_open)
			return FALSE;

		items = sdl_dbg_menu_items(dbg->menu_active, &n_items);
		if (!items || n_items == 0) {
			sdl_dbg_menu_close(dbg);
			return TRUE;
		}

		sdl_dbg_menu_panel_bounds(dbg, &layout, dbg->menu_active,
		                          &menu_x, &menu_y, &menu_w, &menu_h);
		idx = sdl_dbg_menu_hit_test_items(dbg, items, n_items,
		                                  menu_x, menu_y,
		                                  menu_w, menu_h,
		                                  x, y);
		if (idx >= 0) {
			dbg->menu_selected = idx;
			if (sdl_dbg_menu_action_enabled(dbg,
			                                items[idx].action)) {
				sdl_dbg_menu_close(dbg);
				sdl_dbg_handle_menu_action(
					dbg, items[idx].action);
			}
			return TRUE;
		}

		sdl_dbg_menu_close(dbg);
		return TRUE;
	}

	if (!dbg->menu_open)
		return FALSE;

	bar = sdl_dbg_menu_hit_test_bar(dbg, &layout, x, y);
	if (bar != SDL_DBG_MENU_BAR_NONE && bar != dbg->menu_active) {
		sdl_dbg_menu_open(dbg, bar);
		return TRUE;
	}

	items = sdl_dbg_menu_items(dbg->menu_active, &n_items);
	if (!items || n_items == 0)
		return TRUE;

	sdl_dbg_menu_panel_bounds(dbg, &layout, dbg->menu_active,
	                          &menu_x, &menu_y, &menu_w, &menu_h);
	idx = sdl_dbg_menu_hit_test_items(dbg, items, n_items,
	                                  menu_x, menu_y,
	                                  menu_w, menu_h,
	                                  x, y);
	dbg->menu_selected = idx;
	return TRUE;
}

static gboolean sdl_dbg_handle_mouse(TilemSdlDebugger *dbg,
                                     const SDL_MouseButtonEvent *event)
{
	TilemSdlDebuggerLayout layout;
	int line;
	dword addr;
	TilemSdlDbgToolbarButton buttons[8];
	int count;
	int i;

	if (dbg->show_breakpoints || dbg->input_mode != SDL_DBG_INPUT_NONE
	    || dbg->menu_open)
		return TRUE;

	if (!dbg->emu || !dbg->emu->calc)
		return TRUE;

	sdl_dbg_compute_layout(dbg, &layout);
	if (event->button == SDL_BUTTON_LEFT
	    && layout.toolbar_rect.h > 0
	    && event->x >= layout.toolbar_rect.x
	    && event->x < layout.toolbar_rect.x + layout.toolbar_rect.w
	    && event->y >= layout.toolbar_rect.y
	    && event->y < layout.toolbar_rect.y + layout.toolbar_rect.h) {
		count = sdl_dbg_toolbar_layout(dbg, &layout, buttons,
		                               (int) G_N_ELEMENTS(buttons));
		for (i = 0; i < count; i++) {
			SDL_Rect rect = buttons[i].rect;
			if (event->x >= rect.x && event->x < rect.x + rect.w
			    && event->y >= rect.y && event->y < rect.y + rect.h) {
				if (sdl_dbg_menu_action_enabled(
					    dbg, buttons[i].action))
					sdl_dbg_handle_menu_action(
						dbg, buttons[i].action);
				return TRUE;
			}
		}
	}

	if (!sdl_dbg_is_paused(dbg))
		return TRUE;

	if (event->x < layout.disasm_rect.x
	    || event->x >= layout.disasm_rect.x + layout.disasm_rect.w
	    || event->y < layout.disasm_rect.y
	    || event->y >= layout.disasm_rect.y + layout.disasm_rect.h)
		return FALSE;

	line = sdl_dbg_line_from_y(dbg, layout.disasm_rect, event->y);
	if (line < 0 || line >= layout.disasm_lines)
		return FALSE;

	tilem_calc_emulator_lock(dbg->emu);
	addr = sdl_dbg_address_at_line(dbg->emu->calc, dbg->dasm,
	                               dbg->disasm_base, line,
	                               dbg->mem_logical);
	tilem_calc_emulator_unlock(dbg->emu);

	dbg->disasm_cursor = addr;
	dbg->disasm_follow_pc = FALSE;

	if (event->button == SDL_BUTTON_RIGHT)
		sdl_dbg_toggle_breakpoint(dbg, addr);
	return TRUE;
}

static void sdl_dbg_handle_wheel(TilemSdlDebugger *dbg,
                                 const SDL_MouseWheelEvent *event)
{
	TilemSdlDebuggerLayout layout;
	int lines;

	if (dbg->show_breakpoints || dbg->input_mode != SDL_DBG_INPUT_NONE
	    || dbg->menu_open)
		return;

	if (!dbg->emu || !dbg->emu->calc)
		return;
	if (!sdl_dbg_is_paused(dbg))
		return;

	sdl_dbg_compute_layout(dbg, &layout);
	lines = layout.disasm_lines;
	if (lines <= 0)
		lines = 1;

	if (event->y > 0)
		sdl_dbg_scroll_lines(dbg, -1, lines);
	else if (event->y < 0)
		sdl_dbg_scroll_lines(dbg, 1, lines);
}

gboolean tilem_sdl_debugger_handle_event(TilemSdlDebugger *dbg,
                                         const SDL_Event *event)
{
	if (!dbg || !dbg->visible || !dbg->window || !event)
		return FALSE;

	if (sdl_dbg_keypad_handle_event(dbg, event))
		return TRUE;

	if (!sdl_dbg_event_matches(dbg, event))
		return FALSE;

	switch (event->type) {
	case SDL_WINDOWEVENT:
		if (event->window.event == SDL_WINDOWEVENT_CLOSE)
			tilem_sdl_debugger_hide(dbg);
		else if (event->window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
			dbg->window_width = event->window.data1;
			dbg->window_height = event->window.data2;
		}
		return TRUE;
	case SDL_KEYDOWN:
		sdl_dbg_handle_key(dbg, &event->key);
		return TRUE;
	case SDL_TEXTINPUT:
		if (dbg->input_mode != SDL_DBG_INPUT_NONE) {
			size_t add_len = strlen(event->text.text);
			if (dbg->input_len + (int) add_len
			    < (int) sizeof(dbg->input_buf) - 1) {
				memcpy(dbg->input_buf + dbg->input_len,
				       event->text.text, add_len);
				dbg->input_len += (int) add_len;
				dbg->input_buf[dbg->input_len] = '\0';
			}
		}
		return TRUE;
	case SDL_MOUSEMOTION:
		sdl_dbg_handle_menu_mouse(dbg, event);
		return TRUE;
	case SDL_MOUSEBUTTONDOWN:
		if (sdl_dbg_handle_menu_mouse(dbg, event))
			return TRUE;
		sdl_dbg_handle_mouse(dbg, &event->button);
		return TRUE;
	case SDL_MOUSEWHEEL:
		sdl_dbg_handle_wheel(dbg, &event->wheel);
		return TRUE;
	default:
		return TRUE;
	}
}

static void sdl_dbg_render_panel(SDL_Renderer *renderer, SDL_Rect rect)
{
	SDL_Color border = { 80, 80, 80, 255 };
	SDL_Color bg = { 30, 30, 30, 255 };

	SDL_SetRenderDrawColor(renderer, bg.r, bg.g, bg.b, bg.a);
	SDL_RenderFillRect(renderer, &rect);
	SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b,
	                       border.a);
	SDL_RenderDrawRect(renderer, &rect);
}

static void sdl_dbg_render_toolbar(TilemSdlDebugger *dbg,
                                   const TilemSdlDebuggerLayout *layout)
{
	TilemSdlDbgToolbarButton buttons[8];
	int count;
	int i;
	SDL_Color border = { 80, 80, 80, 255 };
	SDL_Color text = { 220, 220, 220, 255 };

	if (!dbg || layout->toolbar_rect.h <= 0)
		return;

	count = sdl_dbg_toolbar_layout(dbg, layout, buttons,
	                               (int) G_N_ELEMENTS(buttons));
	for (i = 0; i < count; i++) {
		gboolean enabled = sdl_dbg_menu_action_enabled(
			dbg, buttons[i].action);
		SDL_Rect rect = buttons[i].rect;
		SDL_Rect dst;
		Uint8 alpha = enabled ? 255 : 120;

		SDL_SetRenderDrawColor(dbg->renderer, border.r, border.g,
		                       border.b, border.a);
		SDL_RenderDrawRect(dbg->renderer, &rect);

		if (buttons[i].icon && buttons[i].icon->texture) {
			dst.x = rect.x + (rect.w - buttons[i].icon->width) / 2;
			dst.y = rect.y + (rect.h - buttons[i].icon->height) / 2;
			dst.w = buttons[i].icon->width;
			dst.h = buttons[i].icon->height;
			SDL_SetTextureAlphaMod(buttons[i].icon->texture, alpha);
			SDL_RenderCopy(dbg->renderer,
			               buttons[i].icon->texture,
			               NULL, &dst);
			SDL_SetTextureAlphaMod(buttons[i].icon->texture, 255);
		}
		else if (buttons[i].label) {
			SDL_Color color = text;
			color.a = alpha;
			sdl_dbg_draw_text(dbg,
			                  rect.x + SDL_DEBUGGER_MENU_PADDING,
			                  rect.y + (rect.h - dbg->line_height) / 2,
			                  buttons[i].label, color);
		}
	}
}

static void sdl_dbg_draw_check(SDL_Renderer *renderer, int x, int y,
                               int size, SDL_Color color)
{
	if (size <= 2)
		return;

	SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
	SDL_RenderDrawLine(renderer, x, y + size / 2,
	                   x + size / 3, y + size);
	SDL_RenderDrawLine(renderer, x + size / 3, y + size,
	                   x + size, y);
}

static void sdl_dbg_render_menubar(TilemSdlDebugger *dbg,
                                   const TilemSdlDebuggerLayout *layout)
{
	SDL_Color text = { 230, 230, 230, 255 };
	SDL_Color highlight = { 60, 110, 170, 255 };
	SDL_Color bg = { 40, 40, 40, 255 };
	SDL_Color border = { 90, 90, 90, 255 };
	int text_h;
	int i;

	if (!dbg || !layout)
		return;

	SDL_SetRenderDrawColor(dbg->renderer, bg.r, bg.g, bg.b, bg.a);
	SDL_RenderFillRect(dbg->renderer, &layout->menu_rect);
	SDL_SetRenderDrawColor(dbg->renderer, border.r, border.g, border.b,
	                       border.a);
	SDL_RenderDrawLine(dbg->renderer,
	                   layout->menu_rect.x,
	                   layout->menu_rect.y + layout->menu_rect.h - 1,
	                   layout->menu_rect.x + layout->menu_rect.w,
	                   layout->menu_rect.y + layout->menu_rect.h - 1);

	sdl_dbg_menu_bar_layout(dbg, layout);
	text_h = dbg->font ? TTF_FontHeight(dbg->font) : dbg->line_height;
	for (i = 0; i < SDL_DBG_MENU_BAR_COUNT; i++) {
		SDL_Rect rect = dbg->menu_bar_items[i];
		if (dbg->menu_open && dbg->menu_active == i) {
			SDL_SetRenderDrawColor(dbg->renderer, highlight.r,
			                       highlight.g, highlight.b,
			                       highlight.a);
			SDL_RenderFillRect(dbg->renderer, &rect);
		}
		sdl_dbg_draw_text(dbg,
		                  rect.x + SDL_DEBUGGER_MENU_PADDING,
		                  rect.y + (rect.h - text_h) / 2,
		                  sdl_dbg_menu_bar_labels[i],
		                  text);
	}
}

static void sdl_dbg_render_menu_panel(TilemSdlDebugger *dbg,
                                      const TilemSdlDbgMenuItem *items,
                                      size_t n_items,
                                      int menu_x, int menu_y,
                                      int menu_w, int menu_h,
                                      int selected)
{
	SDL_Color text_color = { 230, 230, 230, 255 };
	SDL_Color disabled_color = { 140, 140, 140, 255 };
	SDL_Color highlight = { 60, 110, 170, 255 };
	SDL_Color bg = { 40, 40, 40, 240 };
	SDL_Color border = { 90, 90, 90, 255 };
	SDL_Rect menu_rect;
	SDL_Rect item_rect;
	int item_h;
	int icon_col_w;
	int icon_area_w;
	int text_h;
	size_t i;

	if (!dbg || !items || n_items == 0)
		return;

	item_h = sdl_dbg_menu_item_height(dbg, items, n_items);
	icon_col_w = sdl_dbg_menu_icon_column_width(dbg, items, n_items);
	icon_area_w = (icon_col_w > 0) ? (icon_col_w - SDL_DEBUGGER_MENU_ICON_GAP) : 0;
	text_h = dbg->font ? TTF_FontHeight(dbg->font) : dbg->line_height;

	menu_rect.x = menu_x;
	menu_rect.y = menu_y;
	menu_rect.w = menu_w;
	menu_rect.h = menu_h;

	SDL_SetRenderDrawColor(dbg->renderer, bg.r, bg.g, bg.b, bg.a);
	SDL_RenderFillRect(dbg->renderer, &menu_rect);
	SDL_SetRenderDrawColor(dbg->renderer, border.r, border.g,
	                       border.b, border.a);
	SDL_RenderDrawRect(dbg->renderer, &menu_rect);

	for (i = 0; i < n_items; i++) {
		item_rect.x = menu_x;
		item_rect.y = menu_y + SDL_DEBUGGER_MENU_SPACING
			+ (int) i * item_h;
		item_rect.w = menu_w;
		item_rect.h = item_h;

		if (items[i].separator) {
			SDL_SetRenderDrawColor(dbg->renderer, border.r,
			                       border.g, border.b, border.a);
			SDL_RenderDrawLine(dbg->renderer,
			                   item_rect.x + SDL_DEBUGGER_MENU_PADDING,
			                   item_rect.y + item_rect.h / 2,
			                   item_rect.x + item_rect.w
			                       - SDL_DEBUGGER_MENU_PADDING,
			                   item_rect.y + item_rect.h / 2);
			continue;
		}

		if ((int) i == selected) {
			SDL_SetRenderDrawColor(dbg->renderer, highlight.r,
			                       highlight.g, highlight.b,
			                       highlight.a);
			SDL_RenderFillRect(dbg->renderer, &item_rect);
		}

		if (icon_area_w > 0) {
			const TilemSdlIcon *icon =
				sdl_dbg_menu_item_icon(dbg, items[i].action);
			gboolean enabled = sdl_dbg_menu_action_enabled(
				dbg, items[i].action);
			SDL_Color item_color = enabled ? text_color
			                              : disabled_color;
			if (icon && icon->texture) {
				SDL_Rect dst;

				dst.x = item_rect.x + SDL_DEBUGGER_MENU_PADDING
				        + (icon_area_w - icon->width) / 2;
				dst.y = item_rect.y + (item_rect.h - icon->height) / 2;
				dst.w = icon->width;
				dst.h = icon->height;
				SDL_SetTextureAlphaMod(icon->texture,
				                       enabled ? 255 : 120);
				SDL_RenderCopy(dbg->renderer, icon->texture,
				               NULL, &dst);
				SDL_SetTextureAlphaMod(icon->texture, 255);
			}
			else if (items[i].flags & (SDL_DBG_MENU_FLAG_TOGGLE
			                           | SDL_DBG_MENU_FLAG_RADIO)) {
				if (sdl_dbg_menu_action_checked(dbg,
				                                &items[i])) {
					int size = MIN(icon_area_w,
					               item_rect.h
					                   - SDL_DEBUGGER_MENU_PADDING * 2);
					int cx = item_rect.x
					         + SDL_DEBUGGER_MENU_PADDING
					         + (icon_area_w - size) / 2;
					int cy = item_rect.y
					         + (item_rect.h - size) / 2;
					if (items[i].flags & SDL_DBG_MENU_FLAG_RADIO) {
						SDL_Rect dot = { cx + size / 4,
						                 cy + size / 4,
						                 size / 2,
						                 size / 2 };
						SDL_SetRenderDrawColor(
							dbg->renderer,
							item_color.r,
							item_color.g,
							item_color.b,
							item_color.a);
						SDL_RenderFillRect(dbg->renderer, &dot);
					}
					else {
						sdl_dbg_draw_check(
							dbg->renderer,
							cx, cy, size, item_color);
					}
				}
			}
		}

		SDL_Color color = sdl_dbg_menu_action_enabled(dbg,
		                                              items[i].action)
			? text_color : disabled_color;
		sdl_dbg_draw_text(dbg,
		                  item_rect.x + SDL_DEBUGGER_MENU_PADDING
		                      + icon_col_w,
		                  item_rect.y + (item_rect.h - text_h) / 2,
		                  items[i].label,
		                  color);
	}
}

static void sdl_dbg_render_menu(TilemSdlDebugger *dbg,
                                const TilemSdlDebuggerLayout *layout)
{
	const TilemSdlDbgMenuItem *items;
	size_t n_items;
	int menu_x;
	int menu_y;
	int menu_w;
	int menu_h;

	if (!dbg || !dbg->menu_open || dbg->menu_active == SDL_DBG_MENU_BAR_NONE)
		return;

	items = sdl_dbg_menu_items(dbg->menu_active, &n_items);
	if (!items || n_items == 0)
		return;

	sdl_dbg_menu_panel_bounds(dbg, layout, dbg->menu_active,
	                          &menu_x, &menu_y, &menu_w, &menu_h);
	sdl_dbg_render_menu_panel(dbg, items, n_items,
	                          menu_x, menu_y, menu_w, menu_h,
	                          dbg->menu_selected);
}

void tilem_sdl_debugger_render(TilemSdlDebugger *dbg)
{
	TilemSdlDebuggerLayout layout;
	SDL_Color text = { 220, 220, 220, 255 };
	SDL_Color muted = { 160, 160, 160, 255 };
	SDL_Color highlight = { 60, 110, 170, 255 };
	SDL_Color pc_color = { 255, 210, 120, 255 };
	SDL_Color bp_color = { 255, 120, 120, 255 };
	SDL_Color panel_text = text;
	SDL_Color panel_muted = muted;
	SDL_Color panel_pc = pc_color;
	SDL_Color panel_bp = bp_color;
	TilemCalc *calc;
	TilemZ80Regs regs;
	int i;
	dword addr;
	dword pc;
	dword pc_phys;
	byte f;
	char line[SDL_DEBUGGER_LINE_BUFSZ];
	char disasm_lines[SDL_DEBUGGER_MAX_LINES][SDL_DEBUGGER_LINE_BUFSZ];
	dword disasm_addrs[SDL_DEBUGGER_MAX_LINES];
	char mem_lines[SDL_DEBUGGER_MAX_LINES][SDL_DEBUGGER_LINE_BUFSZ];
	char stack_lines[SDL_DEBUGGER_MAX_LINES][SDL_DEBUGGER_LINE_BUFSZ];
	int disasm_count = 0;
	int mem_count = 0;
	int stack_count = 0;
	int disasm_addr_width = 4;
	int mem_addr_width = 4;

	if (!dbg || !dbg->visible || !dbg->window || !dbg->renderer)
		return;
	if (!dbg->emu || !dbg->emu->calc)
		return;

	calc = dbg->emu->calc;

	sdl_dbg_compute_layout(dbg, &layout);

	if (!dbg->emu->paused) {
		panel_text.r = panel_text.g = panel_text.b = 150;
		panel_muted.r = panel_muted.g = panel_muted.b = 110;
		panel_pc = panel_text;
		panel_bp = panel_text;
	}

	tilem_calc_emulator_lock(dbg->emu);
	disasm_addr_width = dbg->mem_logical ? 4
		: ((calc->hw.romsize + calc->hw.ramsize) > 0xffff ? 6 : 4);
	mem_addr_width = dbg->mem_logical ? 4 : disasm_addr_width;
	regs = calc->z80.r;
	pc = regs.pc.w.l;
	f = regs.af.b.l;
	pc_phys = (*calc->hw.mem_ltop)(calc, pc);

	if (dbg->disasm_follow_pc) {
		if (dbg->mem_logical) {
			dbg->disasm_base = pc;
			dbg->disasm_cursor = pc;
		}
		else {
			dbg->disasm_base = pc_phys;
			dbg->disasm_cursor = pc_phys;
		}
	}
	if (dbg->mem_follow_pc)
		dbg->mem_addr = (dbg->mem_logical ? pc : pc_phys)
		                & ~(SDL_DEBUGGER_MEM_COLS - 1);

	addr = dbg->disasm_base;
	for (i = 0; i < layout.disasm_lines; i++) {
		dword next;
		char textbuf[SDL_DEBUGGER_LINE_BUFSZ];
		gboolean show_markers = !sdl_dbg_has_disasm_icons(dbg);
		char cursor = (show_markers && addr == dbg->disasm_cursor)
		              ? '>' : ' ';
		char bp = (show_markers && sdl_dbg_find_exec_breakpoint(
		               dbg, addr, dbg->mem_logical) >= 0)
		          ? '*' : ' ';

		tilem_disasm_disassemble(dbg->dasm, calc,
		                         dbg->mem_logical ? 0 : 1,
		                         addr, &next,
		                         textbuf, sizeof(textbuf));
		if (show_markers) {
			snprintf(disasm_lines[i], sizeof(disasm_lines[i]),
			         "%c%c%0*X %s",
			         cursor, bp, disasm_addr_width,
			         dbg->mem_logical ? (addr & 0xffff) : addr,
			         textbuf);
		}
		else {
			snprintf(disasm_lines[i], sizeof(disasm_lines[i]),
			         "%0*X %s",
			         disasm_addr_width,
			         dbg->mem_logical ? (addr & 0xffff) : addr,
			         textbuf);
		}
		disasm_addrs[i] = addr;
		disasm_count++;

		if (next <= addr)
			next = addr + 1;
		addr = next;
	}

	for (i = 0; i < layout.mem_rows; i++) {
		int col;
		char *p = mem_lines[i];
		dword row_addr = dbg->mem_addr
			+ (dword) i * SDL_DEBUGGER_MEM_COLS;
		dword disp_addr = dbg->mem_logical
			? (row_addr & 0xffff)
			: row_addr;
		p += snprintf(p, SDL_DEBUGGER_LINE_BUFSZ,
		              "%0*X:", mem_addr_width, disp_addr);
		for (col = 0; col < SDL_DEBUGGER_MEM_COLS; col++) {
			byte v = dbg->mem_logical
				? sdl_dbg_read_mem_byte(calc,
				                        row_addr + col)
				: sdl_dbg_read_mem_byte_physical(
					calc, row_addr + col);
			p += snprintf(p,
			              SDL_DEBUGGER_LINE_BUFSZ - (p - mem_lines[i]),
			              " %02X", v);
		}
		p += snprintf(p, SDL_DEBUGGER_LINE_BUFSZ - (p - mem_lines[i]), "  ");
		for (col = 0; col < SDL_DEBUGGER_MEM_COLS; col++) {
			byte v = dbg->mem_logical
				? sdl_dbg_read_mem_byte(calc,
				                        row_addr + col)
				: sdl_dbg_read_mem_byte_physical(
					calc, row_addr + col);
			*p++ = (v >= 32 && v < 127) ? (char) v : '.';
		}
		*p = '\0';
		mem_count++;
	}

	for (i = 0; i < layout.stack_rows; i++) {
		dword sp = regs.sp.w.l;
		dword entry_addr = (sp + (i * 2)) & 0xffff;
		dword value = sdl_dbg_read_mem_word(calc, entry_addr);
		snprintf(stack_lines[i], sizeof(stack_lines[i]),
		         "+%02X %04X: %04X",
		         (unsigned) (i * 2), entry_addr, value);
		stack_count++;
	}

	tilem_calc_emulator_unlock(dbg->emu);

	SDL_SetRenderDrawColor(dbg->renderer, 20, 20, 20, 255);
	SDL_RenderClear(dbg->renderer);

	sdl_dbg_render_menubar(dbg, &layout);
	if (layout.toolbar_rect.h > 0)
		sdl_dbg_render_toolbar(dbg, &layout);

	sdl_dbg_render_panel(dbg->renderer, layout.disasm_rect);
	sdl_dbg_render_panel(dbg->renderer, layout.regs_rect);
	sdl_dbg_render_panel(dbg->renderer, layout.stack_rect);
	sdl_dbg_render_panel(dbg->renderer, layout.mem_rect);

	for (i = 0; i < disasm_count; i++) {
		SDL_Color color = panel_text;
		const TilemSdlIcon *icon = NULL;
		int y = layout.disasm_rect.y + i * dbg->line_height;
		gboolean is_pc = (disasm_addrs[i]
		                  == (dbg->mem_logical ? pc : pc_phys));
		gboolean is_bp = (sdl_dbg_find_exec_breakpoint(
		                      dbg, disasm_addrs[i],
		                      dbg->mem_logical) >= 0);

		if (is_pc)
			color = panel_pc;
		if (is_bp)
			color = panel_bp;
		if (disasm_addrs[i] == dbg->disasm_cursor) {
			SDL_Rect hl = layout.disasm_rect;
			hl.y = y;
			hl.h = dbg->line_height;
			SDL_SetRenderDrawColor(dbg->renderer, highlight.r,
			                       highlight.g, highlight.b,
			                       highlight.a);
			SDL_RenderFillRect(dbg->renderer, &hl);
		}
		icon = sdl_dbg_pick_disasm_icon(dbg, is_pc, is_bp);
		if (icon && icon->texture) {
			SDL_Rect dst;

			dst.x = layout.disasm_rect.x + SDL_DEBUGGER_MARGIN;
			dst.y = y + (dbg->line_height - icon->height) / 2;
			dst.w = icon->width;
			dst.h = icon->height;
			SDL_RenderCopy(dbg->renderer, icon->texture, NULL,
			               &dst);
		}

		sdl_dbg_draw_text(dbg,
		                  layout.disasm_rect.x + SDL_DEBUGGER_MARGIN
		                      + layout.disasm_icon_w,
		                  y, disasm_lines[i], color);
	}

	if (layout.regs_rect.h > 0) {
		char flags[16];
		int reg_lines = layout.regs_rect.h / dbg->line_height;
		int line_idx = 0;

		snprintf(flags, sizeof(flags), "Flags: %c%c%c%c%c%c%c%c",
		         (f & 0x80) ? 'S' : '.',
		         (f & 0x40) ? 'Z' : '.',
		         (f & 0x20) ? 'Y' : '.',
		         (f & 0x10) ? 'H' : '.',
		         (f & 0x08) ? 'X' : '.',
		         (f & 0x04) ? 'P' : '.',
		         (f & 0x02) ? 'N' : '.',
		         (f & 0x01) ? 'C' : '.');

		if (line_idx < reg_lines) {
			snprintf(line, sizeof(line), "AF:%04X  BC:%04X",
			         regs.af.w.l, regs.bc.w.l);
			sdl_dbg_draw_text(dbg,
			                  layout.regs_rect.x + SDL_DEBUGGER_MARGIN,
			                  layout.regs_rect.y
			                      + dbg->line_height * line_idx,
			                  line, panel_text);
			line_idx++;
		}
		if (line_idx < reg_lines) {
			snprintf(line, sizeof(line), "DE:%04X  HL:%04X",
			         regs.de.w.l, regs.hl.w.l);
			sdl_dbg_draw_text(dbg,
			                  layout.regs_rect.x + SDL_DEBUGGER_MARGIN,
			                  layout.regs_rect.y
			                      + dbg->line_height * line_idx,
			                  line, panel_text);
			line_idx++;
		}
		if (line_idx < reg_lines) {
			snprintf(line, sizeof(line), "IX:%04X  IY:%04X",
			         regs.ix.w.l, regs.iy.w.l);
			sdl_dbg_draw_text(dbg,
			                  layout.regs_rect.x + SDL_DEBUGGER_MARGIN,
			                  layout.regs_rect.y
			                      + dbg->line_height * line_idx,
			                  line, panel_text);
			line_idx++;
		}
		if (line_idx < reg_lines) {
			snprintf(line, sizeof(line), "SP:%04X  PC:%04X",
			         regs.sp.w.l, regs.pc.w.l);
			sdl_dbg_draw_text(dbg,
			                  layout.regs_rect.x + SDL_DEBUGGER_MARGIN,
			                  layout.regs_rect.y
			                      + dbg->line_height * line_idx,
			                  line, panel_text);
			line_idx++;
		}
		if (line_idx < reg_lines) {
			snprintf(line, sizeof(line), "AF':%04X  BC':%04X",
			         regs.af2.w.l, regs.bc2.w.l);
			sdl_dbg_draw_text(dbg,
			                  layout.regs_rect.x + SDL_DEBUGGER_MARGIN,
			                  layout.regs_rect.y
			                      + dbg->line_height * line_idx,
			                  line, panel_text);
			line_idx++;
		}
		if (line_idx < reg_lines) {
			snprintf(line, sizeof(line), "DE':%04X  HL':%04X",
			         regs.de2.w.l, regs.hl2.w.l);
			sdl_dbg_draw_text(dbg,
			                  layout.regs_rect.x + SDL_DEBUGGER_MARGIN,
			                  layout.regs_rect.y
			                      + dbg->line_height * line_idx,
			                  line, panel_text);
			line_idx++;
		}
		if (line_idx < reg_lines) {
			snprintf(line, sizeof(line), "IM:%d  I:%02X",
			         regs.im, regs.ir.b.h);
			sdl_dbg_draw_text(dbg,
			                  layout.regs_rect.x + SDL_DEBUGGER_MARGIN,
			                  layout.regs_rect.y
			                      + dbg->line_height * line_idx,
			                  line, panel_text);
			line_idx++;
		}
		if (line_idx < reg_lines) {
			snprintf(line, sizeof(line), "IFF1:%d IFF2:%d",
			         regs.iff1, regs.iff2);
			sdl_dbg_draw_text(dbg,
			                  layout.regs_rect.x + SDL_DEBUGGER_MARGIN,
			                  layout.regs_rect.y
			                      + dbg->line_height * line_idx,
			                  line, panel_text);
			line_idx++;
		}
		if (line_idx < reg_lines) {
			sdl_dbg_draw_text(dbg,
			                  layout.regs_rect.x + SDL_DEBUGGER_MARGIN,
			                  layout.regs_rect.y
			                      + dbg->line_height * line_idx,
			                  flags, panel_muted);
		}
	}

	for (i = 0; i < stack_count; i++) {
		int y = layout.stack_rect.y + i * dbg->line_height;
		if (dbg->stack_index == i) {
			SDL_Rect hl = layout.stack_rect;
			hl.y = y;
			hl.h = dbg->line_height;
			SDL_SetRenderDrawColor(dbg->renderer, highlight.r,
			                       highlight.g, highlight.b,
			                       highlight.a);
			SDL_RenderFillRect(dbg->renderer, &hl);
		}
		sdl_dbg_draw_text(dbg, layout.stack_rect.x + SDL_DEBUGGER_MARGIN,
		                  y, stack_lines[i], panel_text);
	}

	for (i = 0; i < mem_count; i++) {
		int y = layout.mem_rect.y + i * dbg->line_height;
		sdl_dbg_draw_text(dbg, layout.mem_rect.x + SDL_DEBUGGER_MARGIN,
		                  y, mem_lines[i], panel_text);
	}

	sdl_dbg_render_breakpoints(dbg);
	sdl_dbg_render_menu(dbg, &layout);

	SDL_RenderPresent(dbg->renderer);

	sdl_dbg_keypad_render(dbg);
}
