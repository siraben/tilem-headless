/*
 * TilEm II - headless runner
 *
 * Copyright (c) 2024
 *
 * This program is free software: you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, either version 3 of the
 * License, or (at your option) any later version.
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <glib.h>
#include <glib/gstdio.h>
#include <tilem.h>
#include "animation.h"
#include "script.h"
#include "trace.h"

typedef struct {
	TilemCalc *calc;
	TilemLCDBuffer *lcd;
	TilemAnimation *record;
	gboolean limit_speed;
	TilemTraceWriter *trace;
} HeadlessContext;

#define FRAME_MSEC 30
#define FRAME_USEC (FRAME_MSEC * 1000)
#define TRACE_DEFAULT_LIMIT_BYTES (500ULL * 1024ULL * 1024ULL * 1024ULL)

static gchar* cl_romfile = NULL;
static gchar* cl_statefile = NULL;
static gchar* cl_model = NULL;
static gchar* cl_screenshot = NULL;
static gchar* cl_record = NULL;
static gdouble cl_delay = 0.0;
static gboolean cl_fullspeed_flag = FALSE;
static gboolean cl_normalspeed_flag = FALSE;
static gboolean cl_reset_flag = FALSE;
static gboolean cl_headless_flag = FALSE;
static gchar* cl_macro = NULL;
static gchar* cl_trace = NULL;
static gchar* cl_trace_range = NULL;
static gint64 cl_trace_limit = 0;
static gchar* cl_trace_backtrace = NULL;
static gint64 cl_trace_backtrace_limit = 0;

static GOptionEntry entries[] = {
	{ "rom", 'r', 0, G_OPTION_ARG_FILENAME, &cl_romfile,
	  "The rom file to run", "FILE" },
	{ "model", 'm', 0, G_OPTION_ARG_STRING, &cl_model,
	  "The model to use", "NAME" },
	{ "state-file", 's', 0, G_OPTION_ARG_FILENAME, &cl_statefile,
	  "The state-file to use", "FILE" },
	{ "reset", 0, 0, G_OPTION_ARG_NONE, &cl_reset_flag,
	  "Reset the calc at startup", NULL },
	{ "normal-speed", 0, 0, G_OPTION_ARG_NONE, &cl_normalspeed_flag,
	  "Run at normal speed", NULL },
	{ "full-speed", 0, 0, G_OPTION_ARG_NONE, &cl_fullspeed_flag,
	  "Run at maximum speed", NULL },
	{ "headless", 0, 0, G_OPTION_ARG_NONE, &cl_headless_flag,
	  "Ignored (headless is always enabled here)", NULL },
	{ "headless-delay", 0, 0, G_OPTION_ARG_DOUBLE, &cl_delay,
	  "Seconds to wait before capture/exit", "SECONDS" },
	{ "headless-screenshot", 0, 0, G_OPTION_ARG_FILENAME, &cl_screenshot,
	  "Save a screenshot to FILE", "FILE" },
	{ "headless-record", 0, 0, G_OPTION_ARG_FILENAME, &cl_record,
	  "Save a GIF recording to FILE", "FILE" },
	{ "macro", 0, 0, G_OPTION_ARG_FILENAME, &cl_macro,
	  "Execute macro script FILE", "FILE" },
	{ "trace", 0, 0, G_OPTION_ARG_FILENAME, &cl_trace,
	  "Write binary instruction trace to FILE", "FILE" },
	{ "trace-range", 0, 0, G_OPTION_ARG_STRING, &cl_trace_range,
	  "Trace logical address range (e.g. ram, all, 0x8000-0xBFFF)", "RANGE" },
	{ "trace-limit", 0, 0, G_OPTION_ARG_INT64, &cl_trace_limit,
	  "Maximum trace size in bytes (default 500GB)", "BYTES" },
	{ "trace-backtrace", 0, 0, G_OPTION_ARG_FILENAME, &cl_trace_backtrace,
	  "Write a ring-buffer trace backtrace to FILE at exit", "FILE" },
	{ "trace-backtrace-limit", 0, 0, G_OPTION_ARG_INT64,
	  &cl_trace_backtrace_limit,
	  "Backtrace ring size in bytes (default 1GB)", "BYTES" },
	{ 0, 0, 0, 0, 0, 0, 0 }
};

/* Get model name (abbreviation) for a TilEm model ID. */
static const char *model_to_name(int model)
{
	const TilemHardware **models;
	int nmodels, i;

	tilem_get_supported_hardware(&models, &nmodels);
	for (i = 0; i < nmodels; i++) {
		if (models[i]->model_id == model)
			return models[i]->name;
	}

	return NULL;
}

/* Convert model name to a model ID. */
static int name_to_model(const char *name)
{
	char *s;
	const TilemHardware **models;
	int nmodels, i, j;

	s = g_new(char, strlen(name) + 1);
	for (i = j = 0; name[i]; i++) {
		if (name[i] == '+')
			s[j++] = 'p';
		else if (name[i] != '-')
			s[j++] = g_ascii_tolower(name[i]);
	}
	s[j] = 0;

	tilem_get_supported_hardware(&models, &nmodels);
	for (i = 0; i < nmodels; i++) {
		if (!strcmp(s, models[i]->name)) {
			g_free(s);
			return models[i]->model_id;
		}
	}

	g_free(s);
	return 0;
}

static char *get_sav_name(const char *romname)
{
	char *dname, *bname, *sname, *suff;

	dname = g_path_get_dirname(romname);
	bname = g_path_get_basename(romname);

	if ((suff = strrchr(bname, '.')))
		*suff = 0;
	sname = g_strconcat(dname, G_DIR_SEPARATOR_S, bname, ".sav", NULL);

	g_free(dname);
	g_free(bname);
	return sname;
}

static char *format_from_path(const char *path, const char *fallback)
{
	const char *dot = strrchr(path, '.');

	if (!dot || dot == path || dot[1] == '\0')
		return g_ascii_strdown(fallback, -1);

	return g_ascii_strdown(dot + 1, -1);
}

static gboolean save_animation(TilemAnimation *anim,
                               const char *path,
                               const char *fallback_format)
{
	GError *err = NULL;
	char *format;
	gboolean ok;

	format = format_from_path(path, fallback_format);
	ok = tilem_animation_save(anim, path, format, NULL, NULL, &err);
	if (!ok) {
		g_printerr("Failed to save %s: %s\n", path, err->message);
		g_clear_error(&err);
	}
	g_free(format);
	return ok;
}

static gboolean headless_save_screenshot(void *data, const char *path,
                                         GError **err)
{
	HeadlessContext *ctx = data;
	TilemAnimation *shot;
	gboolean ok;

	if (!path || !*path)
		return FALSE;

	tilem_lcd_get_frame(ctx->calc, ctx->lcd);
	shot = tilem_animation_new(ctx->calc->hw.lcdwidth,
	                           ctx->calc->hw.lcdheight);
	tilem_animation_append_frame(shot, ctx->lcd, 1);
	ok = save_animation(shot, path, "png");
	g_object_unref(shot);
	if (!ok && err && !*err) {
		g_set_error(err, G_FILE_ERROR, G_FILE_ERROR_FAILED,
		            "Failed to save screenshot to %s", path);
	}
	return ok;
}

static gboolean headless_write_memdump(void *data, const char *path,
                                       const char *region,
                                       GError **err)
{
	HeadlessContext *ctx = data;
	const byte *base = NULL;
	gsize size = 0;
	gsize total;

	if (!path || !*path)
		return FALSE;

	total = ctx->calc->hw.romsize + ctx->calc->hw.ramsize
	        + ctx->calc->hw.lcdmemsize;

	if (!region || !g_ascii_strcasecmp(region, "mem")
	    || !g_ascii_strcasecmp(region, "all")) {
		base = ctx->calc->mem;
		size = total;
	}
	else if (!g_ascii_strcasecmp(region, "rom")) {
		base = ctx->calc->mem;
		size = ctx->calc->hw.romsize;
	}
	else if (!g_ascii_strcasecmp(region, "ram")) {
		base = ctx->calc->ram;
		size = ctx->calc->hw.ramsize;
	}
	else if (!g_ascii_strcasecmp(region, "ram-logical")
	         || !g_ascii_strcasecmp(region, "lram")) {
		guint8 *buf = NULL;
		dword addr;
		gboolean ok;

		size = ctx->calc->hw.ramsize;
		buf = g_malloc(size);
		for (addr = 0; addr < size; addr++) {
			dword logical = 0x8000 + addr;
			dword phys = (*ctx->calc->hw.mem_ltop)(ctx->calc, logical);
			buf[addr] = ctx->calc->mem[phys];
		}
		ok = g_file_set_contents(path, (const char *) buf, size, err);
		g_free(buf);
		return ok;
	}
	else if (!g_ascii_strcasecmp(region, "lcd")) {
		base = ctx->calc->lcdmem;
		size = ctx->calc->hw.lcdmemsize;
	}
	else {
		if (err)
			g_set_error(err, G_FILE_ERROR, G_FILE_ERROR_INVAL,
			            "Unknown memdump region '%s'", region);
		return FALSE;
	}

	return g_file_set_contents(path, (const char *) base, size, err);
}

static TilemCalc *load_calc(const char *romname,
                            const char *statefname,
                            int model)
{
	FILE *romfile = NULL;
	FILE *savfile = NULL;
	TilemCalc *calc = NULL;
	char *savname = NULL;
	const char *modelname;

	romfile = g_fopen(romname, "rb");
	if (!romfile) {
		g_printerr("Unable to open %s: %s\n", romname, g_strerror(errno));
		return NULL;
	}

	if (!model)
		model = tilem_guess_rom_type(romfile);

	if (!model) {
		g_printerr("Unable to detect calculator model for %s\n", romname);
		fclose(romfile);
		return NULL;
	}

	if (statefname)
		savname = g_strdup(statefname);
	else
		savname = get_sav_name(romname);

	if (savname)
		savfile = g_fopen(savname, "rb");

	calc = tilem_calc_new(model);
	if (!calc) {
		g_printerr("Unable to allocate calculator model\n");
		if (savfile)
			fclose(savfile);
		fclose(romfile);
		g_free(savname);
		return NULL;
	}

	if (tilem_calc_load_state(calc, romfile, savfile)) {
		g_printerr("The specified ROM or state file is invalid.\n");
		if (savfile)
			fclose(savfile);
		fclose(romfile);
		g_free(savname);
		tilem_calc_free(calc);
		return NULL;
	}

	if (savfile)
		fclose(savfile);
	fclose(romfile);

	if (!savfile && savname) {
		modelname = model_to_name(model);
		if (modelname) {
			FILE *newsav = g_fopen(savname, "wb");
			if (newsav) {
				fprintf(newsav, "MODEL = %s\n", modelname);
				fclose(newsav);
			}
		}
	}

	g_free(savname);
	return calc;
}

static void run_for(TilemCalc *calc,
                    TilemLCDBuffer *lcd,
                    TilemAnimation *record,
                    gdouble seconds,
                    gboolean limit_speed)
{
	gboolean recorded_any = FALSE;
	GTimer *timer;
	gdouble elapsed = 0.0;
	gdouble next_tick = 0.0;

	timer = g_timer_new();

	while (elapsed < seconds) {
		int rem = 0;

		tilem_z80_run_time(calc, FRAME_USEC, &rem);
		if (record) {
			tilem_lcd_get_frame(calc, lcd);
			tilem_animation_append_frame(record, lcd, FRAME_MSEC);
			recorded_any = TRUE;
		}

		elapsed = g_timer_elapsed(timer, NULL);

		if (limit_speed) {
			gdouble sleep_sec;

			next_tick += (gdouble) FRAME_USEC / G_USEC_PER_SEC;
			sleep_sec = next_tick - elapsed;
			if (sleep_sec > 0.0) {
				g_usleep((gulong) (sleep_sec * G_USEC_PER_SEC));
				elapsed = g_timer_elapsed(timer, NULL);
			}
		}
	}

	g_timer_destroy(timer);

	if (record && !recorded_any) {
		tilem_lcd_get_frame(calc, lcd);
		tilem_animation_append_frame(record, lcd, 1);
	}
}

static void headless_press_key(void *data, int key)
{
	HeadlessContext *ctx = data;
	tilem_keypad_press_key(ctx->calc, key);
}

static void headless_release_key(void *data, int key)
{
	HeadlessContext *ctx = data;
	tilem_keypad_release_key(ctx->calc, key);
}

static void headless_advance_time(void *data, double seconds)
{
	HeadlessContext *ctx = data;
	if (seconds > 0.0)
		run_for(ctx->calc, ctx->lcd, ctx->record, seconds, ctx->limit_speed);
}

int main(int argc, char **argv)
{
	GOptionContext *context;
	GError *error = NULL;
	TilemCalc *calc;
	TilemLCDBuffer *lcd;
	TilemAnimation *record_anim = NULL;
	int model = 0;
	gboolean limit_speed = TRUE;
	char *record_format = NULL;
	TilemHeadlessOps ops;
	TilemHeadlessScriptSettings script_settings;
	GError *script_err = NULL;
	GError *trace_err = NULL;
	HeadlessContext ctx;
	TilemTraceWriter trace_writer;

	context = g_option_context_new(NULL);
	g_option_context_add_main_entries(context, entries, NULL);
	if (!g_option_context_parse(context, &argc, &argv, &error)) {
		g_printerr("%s: %s\n", g_get_prgname(), error->message);
		g_error_free(error);
		return 1;
	}

	if (!cl_romfile) {
		g_printerr("No ROM file specified; use --rom.\n");
		return 1;
	}

	if (cl_delay < 0.0) {
		g_printerr("Delay must be non-negative.\n");
		return 1;
	}
	if (cl_trace && cl_trace_backtrace) {
		g_printerr("Use either --trace or --trace-backtrace, not both.\n");
		return 1;
	}

	if (cl_model) {
		model = name_to_model(cl_model);
		if (!model) {
			g_printerr("%s: unknown model %s\n",
			           g_get_prgname(), cl_model);
			return 1;
		}
	}

	if (cl_record) {
		record_format = format_from_path(cl_record, "gif");
		if (strcmp(record_format, "gif") != 0) {
			g_printerr("Recording output must be a .gif file.\n");
			g_free(record_format);
			return 1;
		}
		g_free(record_format);
	}

	calc = load_calc(cl_romfile, cl_statefile, model);
	if (!calc)
		return 1;

	if (cl_reset_flag)
		tilem_calc_reset(calc);

	if (cl_fullspeed_flag)
		limit_speed = FALSE;
	else if (cl_normalspeed_flag)
		limit_speed = TRUE;

	calc->linkport.linkemu = TILEM_LINK_EMULATOR_NONE;

	lcd = tilem_lcd_buffer_new();
	if (cl_record)
		record_anim = tilem_animation_new(calc->hw.lcdwidth,
		                                  calc->hw.lcdheight);

	ctx.calc = calc;
	ctx.lcd = lcd;
	ctx.record = record_anim;
	ctx.limit_speed = limit_speed;
	ctx.trace = NULL;

	ops.ctx = &ctx;
	ops.press_key = headless_press_key;
	ops.release_key = headless_release_key;
	ops.advance_time = headless_advance_time;
	ops.screenshot = headless_save_screenshot;
	ops.memdump = headless_write_memdump;

	script_settings.key_hold = TILEM_HEADLESS_DEFAULT_KEY_HOLD;
	script_settings.key_delay = TILEM_HEADLESS_DEFAULT_KEY_DELAY;

	if (cl_trace) {
		guint64 limit = (cl_trace_limit > 0)
			? (guint64) cl_trace_limit
			: TRACE_DEFAULT_LIMIT_BYTES;

		if (!tilem_trace_writer_init(&trace_writer, calc, cl_trace,
		                             cl_trace_range, limit,
		                             &trace_err)) {
			g_printerr("Trace error: %s\n", trace_err->message);
			g_clear_error(&trace_err);
			tilem_lcd_buffer_free(lcd);
			tilem_calc_free(calc);
			return 1;
		}

		ctx.trace = &trace_writer;
	}
	else if (cl_trace_backtrace) {
		guint64 limit = (cl_trace_backtrace_limit > 0)
			? (guint64) cl_trace_backtrace_limit
			: (1ULL << 30);

		if (!tilem_trace_writer_init_backtrace(&trace_writer, calc,
		                                       cl_trace_backtrace,
		                                       cl_trace_range, limit,
		                                       &trace_err)) {
			g_printerr("Trace error: %s\n", trace_err->message);
			g_clear_error(&trace_err);
			tilem_lcd_buffer_free(lcd);
			tilem_calc_free(calc);
			return 1;
		}

		ctx.trace = &trace_writer;
	}

	if (cl_macro) {
		if (!tilem_headless_script_run(cl_macro, &ops,
		                               &script_settings, &script_err)) {
			g_printerr("Macro error: %s\n", script_err->message);
			g_clear_error(&script_err);
			if (ctx.trace) {
				tilem_trace_writer_close(ctx.trace);
			}
			tilem_lcd_buffer_free(lcd);
			tilem_calc_free(calc);
			return 1;
		}
	}
	else if (cl_delay > 0.0) {
		run_for(calc, lcd, record_anim, cl_delay, limit_speed);
	}

	tilem_lcd_get_frame(calc, lcd);

	if (cl_screenshot) {
		TilemAnimation *shot = tilem_animation_new(calc->hw.lcdwidth,
		                                           calc->hw.lcdheight);
		tilem_animation_append_frame(shot, lcd, 1);
		save_animation(shot, cl_screenshot, "png");
		g_object_unref(shot);
	}

	if (record_anim) {
		save_animation(record_anim, cl_record, "gif");
		g_object_unref(record_anim);
	}

	if (ctx.trace) {
		tilem_trace_writer_close(ctx.trace);
	}

	tilem_lcd_buffer_free(lcd);
	tilem_calc_free(calc);

	return 0;
}
