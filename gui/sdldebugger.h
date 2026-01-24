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

#ifndef TILEM_SDLDEBUGGER_H
#define TILEM_SDLDEBUGGER_H

#include <SDL.h>
#include <glib.h>

#include "emulator.h"

typedef struct _TilemSdlDebugger TilemSdlDebugger;

TilemSdlDebugger *tilem_sdl_debugger_new(TilemCalcEmulator *emu);
void tilem_sdl_debugger_free(TilemSdlDebugger *dbg);
void tilem_sdl_debugger_show(TilemSdlDebugger *dbg);
void tilem_sdl_debugger_hide(TilemSdlDebugger *dbg);

/* Returns TRUE if debugger is visible. */
gboolean tilem_sdl_debugger_visible(TilemSdlDebugger *dbg);

/* Notify debugger that a new calculator is loaded. */
void tilem_sdl_debugger_calc_changed(TilemSdlDebugger *dbg);

/* Handle an SDL event. Returns TRUE if consumed by debugger. */
gboolean tilem_sdl_debugger_handle_event(TilemSdlDebugger *dbg,
                                         const SDL_Event *event);

/* Render the debugger window if visible. */
void tilem_sdl_debugger_render(TilemSdlDebugger *dbg);

#endif
