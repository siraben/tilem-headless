/*
 * TilEm II - SDL pixbuf helpers
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

#include "sdlpixbuf.h"

SDL_Texture *tilem_sdl_texture_from_pixbuf(SDL_Renderer *renderer,
                                           GdkPixbuf *pixbuf)
{
	SDL_Texture *texture;
	guchar *pixels;
	guchar *rgb;
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

	rgb = g_new(guchar, width * height * 3);
	for (y = 0; y < height; y++) {
		const guchar *src = pixels + y * rowstride;
		guchar *dst = rgb + y * width * 3;
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
