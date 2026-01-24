/*
 * TilEm II - SDL screenshot helper
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
#include <glib.h>
#include <string.h>
#include <ticalcs.h>
#include <tilem.h>

#include "emulator.h"
#include "sdlscreenshot.h"

gboolean tilem_sdl_save_screenshot(TilemCalcEmulator *emu,
                                   gboolean smooth_scale,
                                   const dword *palette,
                                   int width, int height,
                                   const char *filename,
                                   const char *format,
                                   GError **err)
{
	SDL_Surface *surface = NULL;
	guchar *pixels;
	int rowstride;
	gboolean ok;

	if (!emu || !emu->calc || !emu->lcd_buffer)
		return FALSE;
	if (!palette) {
		g_set_error(err, G_FILE_ERROR, G_FILE_ERROR_FAILED,
		            "No LCD palette available for screenshot");
		return FALSE;
	}

	rowstride = width * 3;
	pixels = g_new(guchar, rowstride * height);
	if (!pixels) {
		g_set_error(err, G_FILE_ERROR, G_FILE_ERROR_NOMEM,
		            "Unable to allocate screenshot buffer");
		return FALSE;
	}

	g_mutex_lock(emu->lcd_mutex);
	tilem_draw_lcd_image_rgb(emu->lcd_buffer, pixels,
	                         width, height, rowstride, 3,
	                         (dword *) palette,
	                         smooth_scale
	                         ? TILEM_SCALE_SMOOTH
	                         : TILEM_SCALE_FAST);
	g_mutex_unlock(emu->lcd_mutex);

	surface = SDL_CreateRGBSurfaceFrom(pixels, width, height, 24,
	                                   rowstride,
#if SDL_BYTEORDER == SDL_BIG_ENDIAN
	                                   0x00ff0000,
	                                   0x0000ff00,
	                                   0x000000ff,
	                                   0x00000000
#else
	                                   0x000000ff,
	                                   0x0000ff00,
	                                   0x00ff0000,
	                                   0x00000000
#endif
	                                   );
	if (!surface) {
		g_set_error(err, G_FILE_ERROR, G_FILE_ERROR_FAILED,
		            "Unable to create screenshot surface");
		g_free(pixels);
		return FALSE;
	}

	ok = FALSE;
	if (format && *format) {
		char *lower = g_ascii_strdown(format, -1);
		const char *fmt = lower;
		if (fmt[0] == '.')
			fmt++;
		if (strcmp(fmt, "png") == 0) {
			ok = (IMG_SavePNG(surface, filename) == 0);
			if (!ok && err && *err == NULL) {
				g_set_error(err, G_FILE_ERROR,
				            G_FILE_ERROR_FAILED,
				            "Unable to save PNG: %s",
				            IMG_GetError());
			}
		}
		else if (strcmp(fmt, "bmp") == 0) {
			ok = (SDL_SaveBMP(surface, filename) == 0);
			if (!ok && err && *err == NULL) {
				g_set_error(err, G_FILE_ERROR,
				            G_FILE_ERROR_FAILED,
				            "Unable to save BMP: %s",
				            SDL_GetError());
			}
		}
		else if (strcmp(fmt, "jpg") == 0
		         || strcmp(fmt, "jpeg") == 0) {
			ok = (IMG_SaveJPG(surface, filename, 90) == 0);
			if (!ok && err && *err == NULL) {
				g_set_error(err, G_FILE_ERROR,
				            G_FILE_ERROR_FAILED,
				            "Unable to save JPEG: %s",
				            IMG_GetError());
			}
		}
		else {
			g_set_error(err, G_FILE_ERROR, G_FILE_ERROR_FAILED,
			            "Unsupported screenshot format: %s",
			            format);
		}
		g_free(lower);
	}
	else {
		ok = (IMG_SavePNG(surface, filename) == 0);
		if (!ok && err && *err == NULL) {
			g_set_error(err, G_FILE_ERROR, G_FILE_ERROR_FAILED,
			            "Unable to save PNG: %s",
			            IMG_GetError());
		}
	}

	SDL_FreeSurface(surface);
	g_free(pixels);
	return ok;
}
