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

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <glib.h>
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
	GdkPixbuf *pixbuf;
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

	pixbuf = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8, width, height);
	if (!pixbuf) {
		g_set_error(err, G_FILE_ERROR, G_FILE_ERROR_NOMEM,
		            "Unable to allocate screenshot buffer");
		return FALSE;
	}

	pixels = gdk_pixbuf_get_pixels(pixbuf);
	rowstride = gdk_pixbuf_get_rowstride(pixbuf);

	g_mutex_lock(emu->lcd_mutex);
	tilem_draw_lcd_image_rgb(emu->lcd_buffer, pixels,
	                         width, height, rowstride, 3,
	                         (dword *) palette,
	                         smooth_scale
	                         ? TILEM_SCALE_SMOOTH
	                         : TILEM_SCALE_FAST);
	g_mutex_unlock(emu->lcd_mutex);

	ok = gdk_pixbuf_save(pixbuf, filename, format, err, NULL);
	g_object_unref(pixbuf);
	return ok;
}
