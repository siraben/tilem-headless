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

#ifndef TILEM_SDL_SCREENSHOT_H
#define TILEM_SDL_SCREENSHOT_H

#include <glib.h>
#include <tilem.h>

typedef struct _TilemCalcEmulator TilemCalcEmulator;

gboolean tilem_sdl_save_screenshot(TilemCalcEmulator *emu,
                                   gboolean smooth_scale,
                                   const dword *palette,
                                   int width, int height,
                                   const char *filename,
                                   const char *format,
                                   GError **err);

#endif
