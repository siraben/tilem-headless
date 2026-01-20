/*
 * TilEm II - headless macro script support
 *
 * Copyright (c) 2024
 *
 * This program is free software: you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, either version 3 of the
 * License, or (at your option) any later version.
 */

#ifndef TILEM_HEADLESS_SCRIPT_H
#define TILEM_HEADLESS_SCRIPT_H

#include <glib.h>

typedef struct {
	void *ctx;
	void (*press_key)(void *ctx, int key);
	void (*release_key)(void *ctx, int key);
	void (*advance_time)(void *ctx, double seconds);
} TilemHeadlessOps;

typedef struct {
	double key_hold;
	double key_delay;
} TilemHeadlessScriptSettings;

#define TILEM_HEADLESS_DEFAULT_KEY_HOLD 0.05
#define TILEM_HEADLESS_DEFAULT_KEY_DELAY 0.05

gboolean tilem_headless_script_run(const char *path,
                                   const TilemHeadlessOps *ops,
                                   TilemHeadlessScriptSettings *settings,
                                   GError **err);

#endif
