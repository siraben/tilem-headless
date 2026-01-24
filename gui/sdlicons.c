/*
 * TilEm II - SDL icon helpers
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
#include <glib.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <stdio.h>
#include <string.h>

#include "sdlicons.h"
#include "files.h"

static const int sdl_icon_sizes_app[] = { 48, 32, 24, 22, 16 };
static const int sdl_icon_sizes_action[] = { 16, 24 };
static const int sdl_icon_sizes_status[] = { 16 };
static const int sdl_icon_sizes_menu[] = { 16, 24, 22, 32, 48 };

static const char * const menu_icon_open_names[] = {
	"document-open",
	"gtk-open",
	"open"
};
static const char * const menu_icon_save_names[] = {
	"document-save",
	"gtk-save",
	"save"
};
static const char * const menu_icon_save_as_names[] = {
	"document-save-as",
	"gtk-save-as",
	"document-save"
};
static const char * const menu_icon_revert_names[] = {
	"document-revert",
	"edit-undo",
	"document-revert-to-saved"
};
static const char * const menu_icon_clear_names[] = {
	"edit-clear",
	"edit-clear-all",
	"gtk-clear"
};
static const char * const menu_icon_record_names[] = {
	"media-record",
	"gtk-media-record"
};
static const char * const menu_icon_stop_names[] = {
	"media-playback-stop",
	"media-stop",
	"gtk-media-stop"
};
static const char * const menu_icon_play_names[] = {
	"media-playback-start",
	"media-playback-play",
	"media-play",
	"gtk-media-play"
};
static const char * const menu_icon_prefs_names[] = {
	"preferences-system",
	"preferences-desktop",
	"gtk-preferences"
};
static const char * const menu_icon_about_names[] = {
	"help-about",
	"gtk-about"
};
static const char * const menu_icon_quit_names[] = {
	"application-exit",
	"system-log-out",
	"gtk-quit"
};
static const char * const menu_icon_close_names[] = {
	"window-close",
	"gtk-close"
};
static const char * const dbg_icon_run_names[] = {
	"media-playback-start",
	"media-playback-play",
	"media-play",
	"gtk-media-play"
};
static const char * const dbg_icon_pause_names[] = {
	"media-playback-pause",
	"media-pause",
	"gtk-media-pause"
};

static void sdl_icon_add_dir(GPtrArray *dirs, const char *path)
{
	if (!dirs || !path || !*path)
		return;
	if (!g_file_test(path, G_FILE_TEST_IS_DIR))
		return;
	g_ptr_array_add(dirs, g_strdup(path));
}

static GPtrArray *sdl_icon_theme_dirs(void)
{
	GPtrArray *dirs = g_ptr_array_new_with_free_func(g_free);
	const char *user_data;
	const char *xdg;
	char *home_icons;
	char **parts;
	int i;

	home_icons = g_build_filename(g_get_home_dir(), ".icons", NULL);
	sdl_icon_add_dir(dirs, home_icons);
	g_free(home_icons);

	user_data = g_get_user_data_dir();
	if (user_data) {
		char *path = g_build_filename(user_data, "icons", NULL);
		sdl_icon_add_dir(dirs, path);
		g_free(path);
	}

	xdg = g_getenv("XDG_DATA_DIRS");
	if (!xdg || !*xdg)
		xdg = "/usr/local/share:/usr/share";

	parts = g_strsplit(xdg, ":", -1);
	for (i = 0; parts && parts[i]; i++) {
		char *path;

		if (!parts[i][0])
			continue;
		path = g_build_filename(parts[i], "icons", NULL);
		sdl_icon_add_dir(dirs, path);
		g_free(path);
	}
	g_strfreev(parts);

	return dirs;
}

static GPtrArray *sdl_icon_pixmap_dirs(void)
{
	GPtrArray *dirs = g_ptr_array_new_with_free_func(g_free);
	const char *user_data;
	const char *xdg;
	char **parts;
	int i;

	user_data = g_get_user_data_dir();
	if (user_data) {
		char *path = g_build_filename(user_data, "pixmaps", NULL);
		sdl_icon_add_dir(dirs, path);
		g_free(path);
	}

	xdg = g_getenv("XDG_DATA_DIRS");
	if (!xdg || !*xdg)
		xdg = "/usr/local/share:/usr/share";

	parts = g_strsplit(xdg, ":", -1);
	for (i = 0; parts && parts[i]; i++) {
		char *path;

		if (!parts[i][0])
			continue;
		path = g_build_filename(parts[i], "pixmaps", NULL);
		sdl_icon_add_dir(dirs, path);
		g_free(path);
	}
	g_strfreev(parts);

	return dirs;
}

static SDL_Surface *sdl_surface_from_pixbuf(GdkPixbuf *pixbuf)
{
	GdkPixbuf *rgba;
	guchar *pixels;
	int width;
	int height;
	int rowstride;
	SDL_Surface *surface;
	Uint32 rmask;
	Uint32 gmask;
	Uint32 bmask;
	Uint32 amask;
	int y;

	if (!pixbuf)
		return NULL;

	if (gdk_pixbuf_get_has_alpha(pixbuf))
		rgba = g_object_ref(pixbuf);
	else
		rgba = gdk_pixbuf_add_alpha(pixbuf, FALSE, 0, 0, 0);

	if (!rgba)
		return NULL;

	width = gdk_pixbuf_get_width(rgba);
	height = gdk_pixbuf_get_height(rgba);
	rowstride = gdk_pixbuf_get_rowstride(rgba);
	pixels = gdk_pixbuf_get_pixels(rgba);

#if SDL_BYTEORDER == SDL_BIG_ENDIAN
	rmask = 0xff000000;
	gmask = 0x00ff0000;
	bmask = 0x0000ff00;
	amask = 0x000000ff;
#else
	rmask = 0x000000ff;
	gmask = 0x0000ff00;
	bmask = 0x00ff0000;
	amask = 0xff000000;
#endif

	surface = SDL_CreateRGBSurface(0, width, height, 32,
	                               rmask, gmask, bmask, amask);
	if (!surface) {
		g_object_unref(rgba);
		return NULL;
	}

	if (SDL_MUSTLOCK(surface))
		SDL_LockSurface(surface);

	for (y = 0; y < height; y++) {
		memcpy((Uint8 *) surface->pixels + y * surface->pitch,
		       pixels + y * rowstride,
		       (size_t) width * 4);
	}

	if (SDL_MUSTLOCK(surface))
		SDL_UnlockSurface(surface);

	g_object_unref(rgba);
	return surface;
}

static SDL_Surface *sdl_try_icon_file(const char *path)
{
	GdkPixbuf *pixbuf;
	SDL_Surface *surface;

	if (!path)
		return NULL;
	if (!g_file_test(path, G_FILE_TEST_IS_REGULAR))
		return NULL;

	pixbuf = gdk_pixbuf_new_from_file(path, NULL);
	if (!pixbuf)
		return NULL;

	surface = sdl_surface_from_pixbuf(pixbuf);
	g_object_unref(pixbuf);
	return surface;
}

static SDL_Surface *sdl_load_icon_surface(const char *icons_root,
                                          const char *category,
                                          const char *name,
                                          const int *sizes,
                                          int n_sizes)
{
	GdkPixbuf *pixbuf;
	SDL_Surface *surface = NULL;
	char *filename;
	int i;

	if (!icons_root || !category || !name)
		return NULL;

	filename = g_strconcat(name, ".png", NULL);
	for (i = 0; i < n_sizes; i++) {
		char size_dir[16];
		char *path;

		snprintf(size_dir, sizeof(size_dir), "%dx%d",
		         sizes[i], sizes[i]);
		path = g_build_filename(icons_root, "hicolor",
		                        size_dir, category,
		                        filename, NULL);
		if (!g_file_test(path, G_FILE_TEST_IS_REGULAR)) {
			g_free(path);
			continue;
		}

		pixbuf = gdk_pixbuf_new_from_file(path, NULL);
		g_free(path);
		if (!pixbuf)
			continue;

		surface = sdl_surface_from_pixbuf(pixbuf);
		g_object_unref(pixbuf);
		if (surface)
			break;
	}

	g_free(filename);
	return surface;
}

static SDL_Surface *sdl_load_theme_icon_surface(const char *const *names,
                                                int n_names,
                                                const int *sizes,
                                                int n_sizes)
{
	static const char * const themes[] = {
		"hicolor",
		"Adwaita",
		"gnome",
		"Tango",
		"Humanity",
		"HighContrast"
	};
	static const char * const categories[] = {
		"actions",
		"apps",
		"status",
		"places",
		"devices",
		"mimetypes",
		"categories"
	};
	GPtrArray *theme_dirs;
	GPtrArray *pixmap_dirs;
	SDL_Surface *surface = NULL;
	int n, i, t, s, c;

	if (!names || n_names <= 0 || !sizes || n_sizes <= 0)
		return NULL;

	theme_dirs = sdl_icon_theme_dirs();
	pixmap_dirs = sdl_icon_pixmap_dirs();

	for (n = 0; n < n_names && !surface; n++) {
		const char *name = names[n];
		char *png_name;
		char *svg_name;

		if (!name || !*name)
			continue;

		png_name = g_strconcat(name, ".png", NULL);
		svg_name = g_strconcat(name, ".svg", NULL);

		for (i = 0; i < (int) theme_dirs->len && !surface; i++) {
			const char *base = g_ptr_array_index(theme_dirs, i);
			for (t = 0; t < (int) G_N_ELEMENTS(themes) && !surface; t++) {
				for (s = 0; s < n_sizes && !surface; s++) {
					char size_dir[16];

					snprintf(size_dir, sizeof(size_dir),
					         "%dx%d", sizes[s], sizes[s]);
					for (c = 0;
					     c < (int) G_N_ELEMENTS(categories)
					     && !surface;
					     c++) {
						char *path = g_build_filename(
							base, themes[t],
							size_dir,
							categories[c],
							png_name, NULL);
						surface = sdl_try_icon_file(path);
						g_free(path);
						if (surface)
							break;

						path = g_build_filename(
							base, themes[t],
							size_dir,
							categories[c],
							svg_name, NULL);
						surface = sdl_try_icon_file(path);
						g_free(path);
					}
				}
			}
		}

		for (i = 0; i < (int) pixmap_dirs->len && !surface; i++) {
			const char *base = g_ptr_array_index(pixmap_dirs, i);
			char *path = g_build_filename(base, png_name, NULL);
			surface = sdl_try_icon_file(path);
			g_free(path);
			if (surface)
				break;

			path = g_build_filename(base, svg_name, NULL);
			surface = sdl_try_icon_file(path);
			g_free(path);
		}

		g_free(png_name);
		g_free(svg_name);
	}

	g_ptr_array_free(theme_dirs, TRUE);
	g_ptr_array_free(pixmap_dirs, TRUE);
	return surface;
}

static void sdl_load_icon_texture(SDL_Renderer *renderer,
                                  const char *icons_root,
                                  const char *category,
                                  const char *name,
                                  const int *sizes,
                                  int n_sizes,
                                  TilemSdlIcon *out)
{
	SDL_Surface *surface;
	SDL_Texture *texture;

	if (!out)
		return;

	out->texture = NULL;
	out->width = 0;
	out->height = 0;

	if (!renderer)
		return;

	surface = sdl_load_icon_surface(icons_root, category, name,
	                                sizes, n_sizes);
	if (!surface)
		return;

	texture = SDL_CreateTextureFromSurface(renderer, surface);
	if (texture) {
		SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
		out->texture = texture;
		out->width = surface->w;
		out->height = surface->h;
	}
	SDL_FreeSurface(surface);
}

static void sdl_load_theme_icon_texture(SDL_Renderer *renderer,
                                        const char *const *names,
                                        int n_names,
                                        const int *sizes,
                                        int n_sizes,
                                        TilemSdlIcon *out)
{
	SDL_Surface *surface;
	SDL_Texture *texture;

	if (!out)
		return;

	out->texture = NULL;
	out->width = 0;
	out->height = 0;

	if (!renderer)
		return;

	surface = sdl_load_theme_icon_surface(names, n_names,
	                                      sizes, n_sizes);
	if (!surface)
		return;

	texture = SDL_CreateTextureFromSurface(renderer, surface);
	if (texture) {
		SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
		out->texture = texture;
		out->width = surface->w;
		out->height = surface->h;
	}
	SDL_FreeSurface(surface);
}

static void sdl_load_app_fallback_texture(SDL_Renderer *renderer,
                                          const char *icons_root,
                                          const int *sizes,
                                          int n_sizes,
                                          TilemSdlIcon *out)
{
	if (!out || out->texture)
		return;
	sdl_load_icon_texture(renderer, icons_root, "apps", "tilem",
	                      sizes, n_sizes, out);
}

TilemSdlIcons *tilem_sdl_icons_load(SDL_Renderer *renderer)
{
	TilemSdlIcons *icons;
	char *icons_root;

	icons_root = get_shared_dir_path("icons", NULL);
	if (!icons_root)
		return NULL;

	icons = g_new0(TilemSdlIcons, 1);
	icons->app_surface = sdl_load_icon_surface(icons_root, "apps",
	                                           "tilem",
	                                           sdl_icon_sizes_app,
	                                           (int) G_N_ELEMENTS(sdl_icon_sizes_app));

	sdl_load_icon_texture(renderer, icons_root, "status",
	                      "tilem-disasm-pc",
	                      sdl_icon_sizes_status,
	                      (int) G_N_ELEMENTS(sdl_icon_sizes_status),
	                      &icons->disasm_pc);
	sdl_load_icon_texture(renderer, icons_root, "status",
	                      "tilem-disasm-break",
	                      sdl_icon_sizes_status,
	                      (int) G_N_ELEMENTS(sdl_icon_sizes_status),
	                      &icons->disasm_break);
	sdl_load_icon_texture(renderer, icons_root, "status",
	                      "tilem-disasm-break-pc",
	                      sdl_icon_sizes_status,
	                      (int) G_N_ELEMENTS(sdl_icon_sizes_status),
	                      &icons->disasm_break_pc);

	sdl_load_icon_texture(renderer, icons_root, "actions",
	                      "tilem-db-step",
	                      sdl_icon_sizes_action,
	                      (int) G_N_ELEMENTS(sdl_icon_sizes_action),
	                      &icons->db_step);
	sdl_load_icon_texture(renderer, icons_root, "actions",
	                      "tilem-db-step-over",
	                      sdl_icon_sizes_action,
	                      (int) G_N_ELEMENTS(sdl_icon_sizes_action),
	                      &icons->db_step_over);
	sdl_load_icon_texture(renderer, icons_root, "actions",
	                      "tilem-db-finish",
	                      sdl_icon_sizes_action,
	                      (int) G_N_ELEMENTS(sdl_icon_sizes_action),
	                      &icons->db_finish);

	sdl_load_theme_icon_texture(renderer,
	                            dbg_icon_run_names,
	                            (int) G_N_ELEMENTS(dbg_icon_run_names),
	                            sdl_icon_sizes_menu,
	                            (int) G_N_ELEMENTS(sdl_icon_sizes_menu),
	                            &icons->db_run);
	sdl_load_theme_icon_texture(renderer,
	                            dbg_icon_pause_names,
	                            (int) G_N_ELEMENTS(dbg_icon_pause_names),
	                            sdl_icon_sizes_menu,
	                            (int) G_N_ELEMENTS(sdl_icon_sizes_menu),
	                            &icons->db_pause);
	sdl_load_app_fallback_texture(renderer, icons_root,
	                              sdl_icon_sizes_action,
	                              (int) G_N_ELEMENTS(sdl_icon_sizes_action),
	                              &icons->db_run);
	sdl_load_app_fallback_texture(renderer, icons_root,
	                              sdl_icon_sizes_action,
	                              (int) G_N_ELEMENTS(sdl_icon_sizes_action),
	                              &icons->db_pause);

	sdl_load_theme_icon_texture(renderer,
	                            menu_icon_open_names,
	                            (int) G_N_ELEMENTS(menu_icon_open_names),
	                            sdl_icon_sizes_menu,
	                            (int) G_N_ELEMENTS(sdl_icon_sizes_menu),
	                            &icons->menu[TILEM_SDL_MENU_ICON_OPEN]);
	sdl_load_theme_icon_texture(renderer,
	                            menu_icon_save_names,
	                            (int) G_N_ELEMENTS(menu_icon_save_names),
	                            sdl_icon_sizes_menu,
	                            (int) G_N_ELEMENTS(sdl_icon_sizes_menu),
	                            &icons->menu[TILEM_SDL_MENU_ICON_SAVE]);
	sdl_load_theme_icon_texture(renderer,
	                            menu_icon_save_as_names,
	                            (int) G_N_ELEMENTS(menu_icon_save_as_names),
	                            sdl_icon_sizes_menu,
	                            (int) G_N_ELEMENTS(sdl_icon_sizes_menu),
	                            &icons->menu[TILEM_SDL_MENU_ICON_SAVE_AS]);
	sdl_load_theme_icon_texture(renderer,
	                            menu_icon_revert_names,
	                            (int) G_N_ELEMENTS(menu_icon_revert_names),
	                            sdl_icon_sizes_menu,
	                            (int) G_N_ELEMENTS(sdl_icon_sizes_menu),
	                            &icons->menu[TILEM_SDL_MENU_ICON_REVERT]);
	sdl_load_theme_icon_texture(renderer,
	                            menu_icon_clear_names,
	                            (int) G_N_ELEMENTS(menu_icon_clear_names),
	                            sdl_icon_sizes_menu,
	                            (int) G_N_ELEMENTS(sdl_icon_sizes_menu),
	                            &icons->menu[TILEM_SDL_MENU_ICON_CLEAR]);
	sdl_load_theme_icon_texture(renderer,
	                            menu_icon_record_names,
	                            (int) G_N_ELEMENTS(menu_icon_record_names),
	                            sdl_icon_sizes_menu,
	                            (int) G_N_ELEMENTS(sdl_icon_sizes_menu),
	                            &icons->menu[TILEM_SDL_MENU_ICON_RECORD]);
	sdl_load_theme_icon_texture(renderer,
	                            menu_icon_stop_names,
	                            (int) G_N_ELEMENTS(menu_icon_stop_names),
	                            sdl_icon_sizes_menu,
	                            (int) G_N_ELEMENTS(sdl_icon_sizes_menu),
	                            &icons->menu[TILEM_SDL_MENU_ICON_STOP]);
	sdl_load_theme_icon_texture(renderer,
	                            menu_icon_play_names,
	                            (int) G_N_ELEMENTS(menu_icon_play_names),
	                            sdl_icon_sizes_menu,
	                            (int) G_N_ELEMENTS(sdl_icon_sizes_menu),
	                            &icons->menu[TILEM_SDL_MENU_ICON_PLAY]);
	sdl_load_theme_icon_texture(renderer,
	                            menu_icon_prefs_names,
	                            (int) G_N_ELEMENTS(menu_icon_prefs_names),
	                            sdl_icon_sizes_menu,
	                            (int) G_N_ELEMENTS(sdl_icon_sizes_menu),
	                            &icons->menu[TILEM_SDL_MENU_ICON_PREFERENCES]);
	sdl_load_theme_icon_texture(renderer,
	                            menu_icon_about_names,
	                            (int) G_N_ELEMENTS(menu_icon_about_names),
	                            sdl_icon_sizes_menu,
	                            (int) G_N_ELEMENTS(sdl_icon_sizes_menu),
	                            &icons->menu[TILEM_SDL_MENU_ICON_ABOUT]);
	sdl_load_theme_icon_texture(renderer,
	                            menu_icon_quit_names,
	                            (int) G_N_ELEMENTS(menu_icon_quit_names),
	                            sdl_icon_sizes_menu,
	                            (int) G_N_ELEMENTS(sdl_icon_sizes_menu),
	                            &icons->menu[TILEM_SDL_MENU_ICON_QUIT]);
	sdl_load_theme_icon_texture(renderer,
	                            menu_icon_close_names,
	                            (int) G_N_ELEMENTS(menu_icon_close_names),
	                            sdl_icon_sizes_menu,
	                            (int) G_N_ELEMENTS(sdl_icon_sizes_menu),
	                            &icons->menu[TILEM_SDL_MENU_ICON_CLOSE]);
	{
		int i;

		for (i = 0; i < TILEM_SDL_MENU_ICON_COUNT; i++) {
			sdl_load_app_fallback_texture(
				renderer,
				icons_root,
				sdl_icon_sizes_menu,
				(int) G_N_ELEMENTS(sdl_icon_sizes_menu),
				&icons->menu[i]);
		}
	}

	g_free(icons_root);
	return icons;
}

void tilem_sdl_icons_free(TilemSdlIcons *icons)
{
	if (!icons)
		return;

	if (icons->app_surface)
		SDL_FreeSurface(icons->app_surface);

	if (icons->disasm_pc.texture)
		SDL_DestroyTexture(icons->disasm_pc.texture);
	if (icons->disasm_break.texture)
		SDL_DestroyTexture(icons->disasm_break.texture);
	if (icons->disasm_break_pc.texture)
		SDL_DestroyTexture(icons->disasm_break_pc.texture);
	if (icons->db_step.texture)
		SDL_DestroyTexture(icons->db_step.texture);
	if (icons->db_step_over.texture)
		SDL_DestroyTexture(icons->db_step_over.texture);
	if (icons->db_finish.texture)
		SDL_DestroyTexture(icons->db_finish.texture);
	if (icons->db_run.texture)
		SDL_DestroyTexture(icons->db_run.texture);
	if (icons->db_pause.texture)
		SDL_DestroyTexture(icons->db_pause.texture);

	{
		int i;
		for (i = 0; i < TILEM_SDL_MENU_ICON_COUNT; i++) {
			if (icons->menu[i].texture)
				SDL_DestroyTexture(icons->menu[i].texture);
		}
	}

	g_free(icons);
}
