/*
 * TilEm II - SDL skin loader
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
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <glib.h>
#include <glib/gstdio.h>

#include "sdlskin.h"

#define ENDIANNESS_FLAG 0xfeedbabe

static gboolean sdl_skin_read_exact(FILE *fp, void *buf, size_t len,
                                    GError **err)
{
	if (fread(buf, len, 1, fp) == 1)
		return TRUE;

	g_set_error(err, G_FILE_ERROR, G_FILE_ERROR_FAILED,
	            "Unexpected end of skin file");
	return FALSE;
}

static gboolean sdl_skin_skip(FILE *fp, size_t len, GError **err)
{
	if (len == 0)
		return TRUE;
	if (fseek(fp, (long) len, SEEK_CUR) == 0)
		return TRUE;

	g_set_error(err, G_FILE_ERROR, G_FILE_ERROR_FAILED,
	            "Unable to skip skin data");
	return FALSE;
}

static gboolean sdl_skin_read_header(TilemSdlSkin *skin, FILE *fp,
                                     long *out_offset, GError **err)
{
	char sig[17];
	uint32_t endian;
	uint32_t jpeg_offset;
	uint32_t length;
	uint32_t colortype;
	gboolean swap;
	int i;

	memset(sig, 0, sizeof(sig));
	if (!sdl_skin_read_exact(fp, sig, 16, err))
		return FALSE;
	if (strncmp(sig, "TilEm v2.00", 16)
	    && strncmp(sig, "TiEmu v2.00", 16)) {
		g_set_error(err, G_FILE_ERROR, G_FILE_ERROR_FAILED,
		            "Bad skin format");
		return FALSE;
	}

	if (!sdl_skin_read_exact(fp, &endian, sizeof(endian), err))
		return FALSE;
	if (!sdl_skin_read_exact(fp, &jpeg_offset, sizeof(jpeg_offset), err))
		return FALSE;

	swap = (endian != ENDIANNESS_FLAG);
	if (swap)
		jpeg_offset = GUINT32_SWAP_LE_BE(jpeg_offset);
	(void) jpeg_offset;

	if (!sdl_skin_read_exact(fp, &length, sizeof(length), err))
		return FALSE;
	if (swap)
		length = GUINT32_SWAP_LE_BE(length);
	if (!sdl_skin_skip(fp, length, err))
		return FALSE;

	if (!sdl_skin_read_exact(fp, &length, sizeof(length), err))
		return FALSE;
	if (swap)
		length = GUINT32_SWAP_LE_BE(length);
	if (!sdl_skin_skip(fp, length, err))
		return FALSE;

	if (!sdl_skin_read_exact(fp, &colortype, sizeof(colortype), err))
		return FALSE;
	if (swap)
		colortype = GUINT32_SWAP_LE_BE(colortype);
	(void) colortype;

	if (!sdl_skin_read_exact(fp, &skin->lcd_white,
	                         sizeof(skin->lcd_white), err))
		return FALSE;
	if (!sdl_skin_read_exact(fp, &skin->lcd_black,
	                         sizeof(skin->lcd_black), err))
		return FALSE;

	if (swap) {
		skin->lcd_black = GUINT32_SWAP_LE_BE(skin->lcd_black);
		skin->lcd_white = GUINT32_SWAP_LE_BE(skin->lcd_white);
	}

	if (!sdl_skin_skip(fp, 8, err))
		return FALSE;

	if (!sdl_skin_read_exact(fp, &skin->lcd_pos.left,
	                         sizeof(skin->lcd_pos.left), err))
		return FALSE;
	if (!sdl_skin_read_exact(fp, &skin->lcd_pos.top,
	                         sizeof(skin->lcd_pos.top), err))
		return FALSE;
	if (!sdl_skin_read_exact(fp, &skin->lcd_pos.right,
	                         sizeof(skin->lcd_pos.right), err))
		return FALSE;
	if (!sdl_skin_read_exact(fp, &skin->lcd_pos.bottom,
	                         sizeof(skin->lcd_pos.bottom), err))
		return FALSE;

	if (!sdl_skin_read_exact(fp, &length, sizeof(length), err))
		return FALSE;
	if (swap)
		length = GUINT32_SWAP_LE_BE(length);
	if (length > SKIN_KEYS) {
		g_set_error(err, G_FILE_ERROR, G_FILE_ERROR_FAILED,
		            "Skin contains too many key regions");
		return FALSE;
	}

	for (i = 0; i < (int) length; i++) {
		if (!sdl_skin_read_exact(fp, &skin->keys_pos[i].left,
		                         sizeof(skin->keys_pos[i].left),
		                         err))
			return FALSE;
		if (!sdl_skin_read_exact(fp, &skin->keys_pos[i].top,
		                         sizeof(skin->keys_pos[i].top),
		                         err))
			return FALSE;
		if (!sdl_skin_read_exact(fp, &skin->keys_pos[i].right,
		                         sizeof(skin->keys_pos[i].right),
		                         err))
			return FALSE;
		if (!sdl_skin_read_exact(fp, &skin->keys_pos[i].bottom,
		                         sizeof(skin->keys_pos[i].bottom),
		                         err))
			return FALSE;
	}

	if (swap) {
		skin->lcd_pos.left = GUINT32_SWAP_LE_BE(skin->lcd_pos.left);
		skin->lcd_pos.top = GUINT32_SWAP_LE_BE(skin->lcd_pos.top);
		skin->lcd_pos.right = GUINT32_SWAP_LE_BE(skin->lcd_pos.right);
		skin->lcd_pos.bottom = GUINT32_SWAP_LE_BE(skin->lcd_pos.bottom);
		for (i = 0; i < (int) length; i++) {
			skin->keys_pos[i].left =
				GUINT32_SWAP_LE_BE(skin->keys_pos[i].left);
			skin->keys_pos[i].top =
				GUINT32_SWAP_LE_BE(skin->keys_pos[i].top);
			skin->keys_pos[i].right =
				GUINT32_SWAP_LE_BE(skin->keys_pos[i].right);
			skin->keys_pos[i].bottom =
				GUINT32_SWAP_LE_BE(skin->keys_pos[i].bottom);
		}
	}

	if (out_offset)
		*out_offset = ftell(fp);
	return TRUE;
}

static gboolean sdl_skin_read_image(TilemSdlSkin *skin, FILE *fp,
                                    long offset, GError **err)
{
	long end;
	size_t size;
	guchar *buf = NULL;
	SDL_RWops *rw = NULL;
	SDL_Surface *surface = NULL;

	if (offset < 0) {
		g_set_error(err, G_FILE_ERROR, G_FILE_ERROR_FAILED,
		            "Unable to read skin image");
		return FALSE;
	}

	if (fseek(fp, 0, SEEK_END) != 0) {
		g_set_error(err, G_FILE_ERROR,
		            g_file_error_from_errno(errno),
		            "Unable to read skin image");
		return FALSE;
	}

	end = ftell(fp);
	if (end < 0 || end < offset) {
		g_set_error(err, G_FILE_ERROR, G_FILE_ERROR_FAILED,
		            "Unable to read skin image");
		return FALSE;
	}

	size = (size_t) (end - offset);
	if (fseek(fp, offset, SEEK_SET) != 0) {
		g_set_error(err, G_FILE_ERROR,
		            g_file_error_from_errno(errno),
		            "Unable to read skin image");
		return FALSE;
	}

	buf = g_malloc(size);
	if (fread(buf, 1, size, fp) != size) {
		g_set_error(err, G_FILE_ERROR, G_FILE_ERROR_FAILED,
		            "Unable to read skin image");
		g_free(buf);
		return FALSE;
	}

	rw = SDL_RWFromConstMem(buf, (int) size);
	if (rw)
		surface = IMG_Load_RW(rw, 1);

	g_free(buf);

	if (!surface) {
		g_set_error(err, G_FILE_ERROR, G_FILE_ERROR_FAILED,
		            "Unable to decode skin image: %s",
		            IMG_GetError());
		return FALSE;
	}

	skin->surface = surface;
	skin->width = surface->w;
	skin->height = surface->h;
	return TRUE;
}

TilemSdlSkin *tilem_sdl_skin_load(const char *filename, GError **err)
{
	TilemSdlSkin *skin;
	FILE *fp;
	long offset;

	if (!filename)
		return NULL;

	fp = g_fopen(filename, "rb");
	if (!fp) {
		g_set_error(err, G_FILE_ERROR,
		            g_file_error_from_errno(errno),
		            "Unable to open %s",
		            filename);
		return NULL;
	}

	skin = g_new0(TilemSdlSkin, 1);
	if (!sdl_skin_read_header(skin, fp, &offset, err)
	    || !sdl_skin_read_image(skin, fp, offset, err)) {
		fclose(fp);
		tilem_sdl_skin_free(skin);
		return NULL;
	}

	fclose(fp);
	skin->sx = 1.0;
	skin->sy = 1.0;
	return skin;
}

void tilem_sdl_skin_free(TilemSdlSkin *skin)
{
	if (!skin)
		return;
	if (skin->surface)
		SDL_FreeSurface(skin->surface);
	g_free(skin);
}
