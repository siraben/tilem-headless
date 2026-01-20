/*
 * TilEm II
 *
 * Copyright (c) 2010-2011 Thibault Duponchelle
 * Copyright (c) 2010-2012 Benjamin Moody
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
#include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <gtk/gtk.h>
#include <glib.h>
#include <ticalcs.h>
#include <tilem.h>

#include "gui.h"
#include "files.h"
#include "icons.h"
#include "msgbox.h"
#include "../headless/script.h"
#include "trace.h"

#define TRACE_DEFAULT_LIMIT_BYTES (500ULL * 1024ULL * 1024ULL * 1024ULL)

/* CMD LINE OPTIONS */
static gchar* cl_romfile = NULL;
static gchar* cl_skinfile = NULL;
static gchar* cl_model = NULL;
static gchar* cl_statefile = NULL;
static gchar** cl_files_to_load = NULL;
static gboolean cl_skinless_flag = FALSE;
static gboolean cl_reset_flag = FALSE;
static gchar* cl_getvar = NULL;
static gchar* cl_macro_to_run = NULL;
static gboolean cl_debug_flag = FALSE;
static gboolean cl_normalspeed_flag = FALSE;
static gboolean cl_fullspeed_flag = FALSE;
static gboolean cl_headless_flag = FALSE;
static gchar* cl_headless_screenshot = NULL;
static gchar* cl_headless_record = NULL;
static gdouble cl_headless_delay = 0.0;
static gchar* cl_headless_macro = NULL;
static gchar* cl_headless_trace = NULL;
static gchar* cl_headless_trace_range = NULL;
static gint64 cl_headless_trace_limit = 0;


static GOptionEntry entries[] =
{
	{ "rom", 'r', 0, G_OPTION_ARG_FILENAME, &cl_romfile, "The rom file to run", "FILE" },
	{ "skin", 'k', 0, G_OPTION_ARG_FILENAME, &cl_skinfile, "The skin file to use", "FILE" },
	{ "model", 'm', 0, G_OPTION_ARG_STRING, &cl_model, "The model to use", "NAME" },
	{ "state-file", 's', 0, G_OPTION_ARG_FILENAME, &cl_statefile, "The state-file to use", "FILE" },
	{ "without-skin", 'l', 0, G_OPTION_ARG_NONE, &cl_skinless_flag, "Start in skinless mode", NULL },
	{ "reset", 0, 0, G_OPTION_ARG_NONE, &cl_reset_flag, "Reset the calc at startup", NULL },
	{ "get-var", 0, 0, G_OPTION_ARG_STRING, &cl_getvar, "Get a var at startup", "FILE" },
	{ "play-macro", 'p', 0, G_OPTION_ARG_FILENAME, &cl_macro_to_run, "Run this macro at startup", "FILE" },
	{ "debug", 'd', 0, G_OPTION_ARG_NONE, &cl_debug_flag, "Launch debugger", NULL },
	{ "normal-speed", 0, 0, G_OPTION_ARG_NONE, &cl_normalspeed_flag, "Run at normal speed", NULL },
	{ "full-speed", 0, 0, G_OPTION_ARG_NONE, &cl_fullspeed_flag, "Run at maximum speed", NULL },
	{ "headless", 0, 0, G_OPTION_ARG_NONE, &cl_headless_flag, "Run without the GUI", NULL },
	{ "headless-delay", 0, 0, G_OPTION_ARG_DOUBLE, &cl_headless_delay, "Seconds to wait before capture/exit in headless mode", "SECONDS" },
	{ "headless-screenshot", 0, 0, G_OPTION_ARG_FILENAME, &cl_headless_screenshot, "Save a screenshot to FILE in headless mode", "FILE" },
	{ "headless-record", 0, 0, G_OPTION_ARG_FILENAME, &cl_headless_record, "Save a GIF recording to FILE in headless mode", "FILE" },
	{ "trace", 0, 0, G_OPTION_ARG_FILENAME, &cl_headless_trace, "Write binary instruction trace to FILE (headless mode)", "FILE" },
	{ "trace-range", 0, 0, G_OPTION_ARG_STRING, &cl_headless_trace_range, "Trace logical address range (e.g. ram, all, 0x8000-0xBFFF)", "RANGE" },
	{ "trace-limit", 0, 0, G_OPTION_ARG_INT64, &cl_headless_trace_limit, "Maximum trace size in bytes (default 500GB)", "BYTES" },
	{ "macro", 0, 0, G_OPTION_ARG_FILENAME, &cl_headless_macro, "Execute macro script FILE (headless mode)", "FILE" },
	{ G_OPTION_REMAINING, 0, 0, G_OPTION_ARG_FILENAME_ARRAY, &cl_files_to_load, NULL, "FILE" },
	{ 0, 0, 0, 0, 0, 0, 0 }
};


/* #########  MAIN  ######### */

/* Order of preference for automatic model selection. */
static const char model_search_order[] =
	{ TILEM_CALC_TI81,
	  TILEM_CALC_TI73,
	  TILEM_CALC_TI82,
	  TILEM_CALC_TI83,
	  TILEM_CALC_TI76,
	  TILEM_CALC_TI84P_SE,
	  TILEM_CALC_TI84P,
	  TILEM_CALC_TI83P_SE,
	  TILEM_CALC_TI83P,
	  TILEM_CALC_TI84P_NSPIRE,
	  TILEM_CALC_TI85,
	  TILEM_CALC_TI86, 0 };

/* Check if given calc model should be used for these file types. */
static gboolean check_file_types(int calc_model,
                                 const int *file_models,
                                 int nfiles)
{
	/* Only choose a calc model if it supports all of the given
	   file types, and at least one of the files is of the calc's
	   "preferred" type.  This means if we have a mixture of 82Ps
	   and 83Ps, we can use either a TI-83 or TI-76.fr ROM image,
	   but not a TI-83 Plus. */

	gboolean preferred = FALSE;
	int i;

	calc_model = model_to_base_model(calc_model);

	for (i = 0; i < nfiles; i++) {
		if (file_models[i] == calc_model)
			preferred = TRUE;
		else if (!model_supports_file(calc_model, file_models[i]))
			return FALSE;
	}

	return preferred;
}

static void load_initial_rom(TilemCalcEmulator *emu,
                             const char *cmdline_rom_name,
                             const char *cmdline_state_name,
                             char **cmdline_files,
                             int model,
                             gboolean allow_prompt)
{
	GError *err = NULL;
	char *modelname;
	int nfiles, *file_models, i;

	/* If a ROM file is specified on the command line, use that
	   (and no other) */

	if (cmdline_rom_name) {
		if (tilem_calc_emulator_load_state(emu, cmdline_rom_name,
		                                   cmdline_state_name,
		                                   model, &err))
			return;
		else if (!err)
			exit(0);
		else {
			g_printerr("%s\n", err->message);
			exit(1);
		}
	}

	/* Choose model by file names */

	if (!model && cmdline_files) {
		nfiles = g_strv_length(cmdline_files);
		file_models = g_new(int, nfiles);

		/* determine model for each filename */
		for (i = 0; i < nfiles; i++)
			file_models[i] = file_to_model(cmdline_files[i]);

		/* iterate over all known models... */
		for (i = 0; model_search_order[i]; i++) {
			model = model_search_order[i];

			/* check if this model supports the named files */
			if (!check_file_types(model, file_models, nfiles))
				continue;

			/* try to load model, but no error message if
			   no ROM is present in config */
			if (tilem_calc_emulator_load_state(emu, NULL, NULL,
			                                   model, &err)) {
				g_free(file_models);
				return;
			}
			else if (!err)
				exit(0);
			else if (!g_error_matches(err, TILEM_EMULATOR_ERROR,
			                          TILEM_EMULATOR_ERROR_NO_ROM)) {
				if (allow_prompt) {
					messagebox01(NULL, GTK_MESSAGE_ERROR,
					             "Unable to load calculator state",
					             "%s", err->message);
				}
				else {
					g_printerr("Unable to load calculator state: %s\n",
					           err->message);
				}
			}
			g_clear_error(&err);
		}

		g_free(file_models);
		model = 0;
	}

	/* If no model specified on command line (either explicitly or
	   implicitly), then choose the most recently used model */

	if (!model && !cmdline_files) {
		tilem_config_get("recent", "last_model/s", &modelname, NULL);
		if (modelname)
			model = name_to_model(modelname);
	}

	/* Try to load the most recently used ROM for chosen model */

	if (model) {
		if (tilem_calc_emulator_load_state(emu, NULL, NULL,
		                                   model, &err))
			return;
		else if (!err)
			exit(0);
		else {
			if (allow_prompt) {
				messagebox01(NULL, GTK_MESSAGE_ERROR,
				             "Unable to load calculator state",
				             "%s", err->message);
			}
			else {
				g_printerr("Unable to load calculator state: %s\n",
				           err->message);
			}
			g_clear_error(&err);
		}
	}

	/* Prompt user for a ROM file */

	if (!allow_prompt) {
		g_printerr("No ROM file specified; use --rom or --model.\n");
		exit(1);
	}

	while (!emu->calc) {
		if (!tilem_calc_emulator_prompt_open_rom(emu))
			exit(0);
	}
}

typedef struct {
	TilemCalcEmulator *emu;
} GuiHeadlessContext;

static void gui_headless_press_key(void *data, int key)
{
	GuiHeadlessContext *ctx = data;
	tilem_calc_emulator_press_key(ctx->emu, key);
}

static void gui_headless_release_key(void *data, int key)
{
	GuiHeadlessContext *ctx = data;
	tilem_calc_emulator_release_key(ctx->emu, key);
}

static void gui_headless_advance_time(void *data, double seconds)
{
	GuiHeadlessContext *ctx = data;
	(void) ctx;
	if (seconds > 0.0)
		g_usleep((gulong)(seconds * G_USEC_PER_SEC));
}

static gboolean gui_headless_screenshot(void *data, const char *path,
                                        GError **err)
{
	GuiHeadlessContext *ctx = data;
	TilemAnimation *shot;
	char *format;
	gboolean ok;

	if (!path || !*path)
		return FALSE;

	shot = tilem_calc_emulator_get_screenshot(ctx->emu, ctx->emu->grayscale);
	if (!shot) {
		if (err)
			g_set_error(err, G_FILE_ERROR, G_FILE_ERROR_FAILED,
			            "Unable to capture screenshot");
		return FALSE;
	}

	format = g_ascii_strdown(strrchr(path, '.') ? strrchr(path, '.') + 1 : "png",
	                         -1);
	ok = tilem_animation_save(shot, path, format, NULL, NULL, err);
	g_free(format);
	g_object_unref(shot);
	return ok;
}

static gboolean gui_headless_memdump(void *data, const char *path,
                                     const char *region, GError **err)
{
	GuiHeadlessContext *ctx = data;
	const byte *base = NULL;
	gsize size = 0;
	gsize total;
	byte *copy = NULL;
	gboolean ok;

	if (!path || !*path)
		return FALSE;

	tilem_calc_emulator_lock(ctx->emu);
	total = ctx->emu->calc->hw.romsize + ctx->emu->calc->hw.ramsize
	        + ctx->emu->calc->hw.lcdmemsize;

	if (!region || !g_ascii_strcasecmp(region, "mem")
	    || !g_ascii_strcasecmp(region, "all")) {
		base = ctx->emu->calc->mem;
		size = total;
	}
	else if (!g_ascii_strcasecmp(region, "rom")) {
		base = ctx->emu->calc->mem;
		size = ctx->emu->calc->hw.romsize;
	}
	else if (!g_ascii_strcasecmp(region, "ram")) {
		base = ctx->emu->calc->ram;
		size = ctx->emu->calc->hw.ramsize;
	}
	else if (!g_ascii_strcasecmp(region, "lcd")) {
		base = ctx->emu->calc->lcdmem;
		size = ctx->emu->calc->hw.lcdmemsize;
	}
	else {
		tilem_calc_emulator_unlock(ctx->emu);
		if (err)
			g_set_error(err, G_FILE_ERROR, G_FILE_ERROR_INVAL,
			            "Unknown memdump region '%s'", region);
		return FALSE;
	}

	copy = g_memdup2(base, size);
	tilem_calc_emulator_unlock(ctx->emu);

	ok = g_file_set_contents(path, (const char *) copy, size, err);
	g_free(copy);
	return ok;
}

int main(int argc, char **argv)
{
	TilemCalcEmulator* emu;
	char *menurc_path;
	GOptionContext *context;
	GError *error = NULL;
	int model = 0;
	gboolean use_headless = FALSE;
	char *format = NULL;
	TilemAnimation *anim = NULL;
	int i;
	GError *script_err = NULL;
	GuiHeadlessContext headless_ctx;
	TilemHeadlessOps headless_ops;
	TilemHeadlessScriptSettings script_settings;
	TilemTraceWriter trace_writer;
	gboolean trace_enabled = FALSE;
	GError *trace_err = NULL;

	g_thread_init(NULL);
	for (i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--headless")
		    || g_str_has_prefix(argv[i], "--headless=")) {
			use_headless = TRUE;
			break;
		}
	}

	set_program_path(argv[0]);
	g_set_application_name("TilEm");

	if (!use_headless)
		gtk_init(&argc, &argv);

	if (!use_headless) {
		menurc_path = get_shared_file_path("menurc", NULL);
		if (menurc_path)
			gtk_accel_map_load(menurc_path);
		g_free(menurc_path);

		init_custom_icons();
		gtk_window_set_default_icon_name("tilem");
	}

	emu = tilem_calc_emulator_new();

	context = g_option_context_new(NULL);
	g_option_context_add_main_entries(context, entries, NULL);
	if (!use_headless)
		g_option_context_add_group(context, gtk_get_option_group(FALSE));
	if (!g_option_context_parse(context, &argc, &argv, &error))
	{
		g_printerr("%s: %s\n", g_get_prgname(), error->message);
		exit (1);
	}

	if (cl_model) {
		model = name_to_model(cl_model);
		if (!model) {
			g_printerr("%s: unknown model %s\n",
			           g_get_prgname(), cl_model);
			return 1;
		}
	}

	if (cl_headless_flag && cl_debug_flag) {
		g_printerr("Headless mode does not support the debugger.\n");
		return 1;
	}
	if (cl_headless_macro && !cl_headless_flag) {
		g_printerr("--macro requires --headless.\n");
		return 1;
	}
	if (cl_headless_trace && !cl_headless_flag) {
		g_printerr("--trace requires --headless.\n");
		return 1;
	}

	load_initial_rom(emu, cl_romfile, cl_statefile, cl_files_to_load, model,
	                 !cl_headless_flag);

	if (!cl_headless_flag)
		emu->ewin = tilem_emulator_window_new(emu);

	if (!cl_headless_flag) {
		if (cl_skinless_flag)
			tilem_emulator_window_set_skin_disabled(emu->ewin, TRUE);
		else if (cl_skinfile) {
			tilem_emulator_window_set_skin(emu->ewin, cl_skinfile);
			tilem_emulator_window_set_skin_disabled(emu->ewin, FALSE);
		}

		gtk_widget_show(emu->ewin->window);
	}

	ticables_library_init();
	tifiles_library_init();
	ticalcs_library_init();

	if (cl_reset_flag)
		tilem_calc_emulator_reset(emu);

	if (cl_fullspeed_flag)
		tilem_calc_emulator_set_limit_speed(emu, FALSE);
	else if (cl_normalspeed_flag || cl_headless_flag)
		tilem_calc_emulator_set_limit_speed(emu, TRUE);

	if (!cl_headless_flag) {
		if (cl_files_to_load)
			load_files_cmdline(emu->ewin, cl_files_to_load);
		if (cl_macro_to_run)
			tilem_macro_load(emu, cl_macro_to_run);
		if (cl_getvar)
			tilem_link_receive_matching(emu, cl_getvar, ".");
	}

	if (cl_debug_flag)
		launch_debugger(emu->ewin);
	else
		tilem_calc_emulator_run(emu);

	if (cl_headless_flag) {
		if (cl_headless_record) {
			format = g_ascii_strdown(strrchr(cl_headless_record, '.')
			                         ? strrchr(cl_headless_record, '.') + 1
			                         : "gif",
			                         -1);
			if (strcmp(format, "gif") != 0) {
				g_printerr("Headless recording requires a .gif output.\n");
				g_free(format);
				tilem_calc_emulator_free(emu);
				return 1;
			}
			g_free(format);
			tilem_calc_emulator_begin_animation(emu, emu->grayscale);
		}

		headless_ctx.emu = emu;
		headless_ops.ctx = &headless_ctx;
		headless_ops.press_key = gui_headless_press_key;
		headless_ops.release_key = gui_headless_release_key;
		headless_ops.advance_time = gui_headless_advance_time;
		headless_ops.screenshot = gui_headless_screenshot;
		headless_ops.memdump = gui_headless_memdump;
		script_settings.key_hold = TILEM_HEADLESS_DEFAULT_KEY_HOLD;
		script_settings.key_delay = TILEM_HEADLESS_DEFAULT_KEY_DELAY;

		if (cl_headless_trace) {
			guint64 limit = (cl_headless_trace_limit > 0)
				? (guint64) cl_headless_trace_limit
				: TRACE_DEFAULT_LIMIT_BYTES;

			tilem_calc_emulator_lock(emu);
			if (!tilem_trace_writer_init(&trace_writer, emu->calc,
			                             cl_headless_trace,
			                             cl_headless_trace_range,
			                             limit, &trace_err)) {
				tilem_calc_emulator_unlock(emu);
				g_printerr("Trace error: %s\n", trace_err->message);
				g_clear_error(&trace_err);
				tilem_calc_emulator_pause(emu);
				tilem_calc_emulator_free(emu);
				ticables_library_exit();
				tifiles_library_exit();
				ticalcs_library_exit();
				return 1;
			}
			tilem_calc_emulator_unlock(emu);
			trace_enabled = TRUE;
		}

		if (cl_headless_macro) {
			if (!tilem_headless_script_run(cl_headless_macro,
			                               &headless_ops,
			                               &script_settings,
			                               &script_err)) {
				g_printerr("Macro error: %s\n", script_err->message);
				g_clear_error(&script_err);
				if (trace_enabled) {
					tilem_calc_emulator_lock(emu);
					tilem_trace_writer_close(&trace_writer);
					tilem_calc_emulator_unlock(emu);
				}
				tilem_calc_emulator_pause(emu);
				tilem_calc_emulator_free(emu);
				ticables_library_exit();
				tifiles_library_exit();
				ticalcs_library_exit();
				return 1;
			}
		}
		else if (cl_headless_delay > 0.0) {
			g_usleep((gulong)(cl_headless_delay * G_USEC_PER_SEC));
		}

		if (cl_headless_screenshot) {
			anim = tilem_calc_emulator_get_screenshot(emu, emu->grayscale);
			if (anim) {
				format = g_ascii_strdown(strrchr(cl_headless_screenshot, '.')
				                         ? strrchr(cl_headless_screenshot, '.') + 1
				                         : "png",
				                         -1);
				if (!tilem_animation_save(anim, cl_headless_screenshot, format,
				                          NULL, NULL, &error)) {
					g_printerr("Failed to save screenshot: %s\n", error->message);
					g_clear_error(&error);
				}
				g_free(format);
				g_object_unref(anim);
			}
		}

		if (cl_headless_record) {
			anim = tilem_calc_emulator_end_animation(emu);
			if (anim) {
				if (!tilem_animation_save(anim, cl_headless_record, "gif",
				                          NULL, NULL, &error)) {
					g_printerr("Failed to save recording: %s\n", error->message);
					g_clear_error(&error);
				}
				g_object_unref(anim);
			}
		}

		if (trace_enabled) {
			tilem_calc_emulator_lock(emu);
			tilem_trace_writer_close(&trace_writer);
			tilem_calc_emulator_unlock(emu);
		}

		tilem_calc_emulator_pause(emu);
		tilem_calc_emulator_free(emu);
		ticables_library_exit();
		tifiles_library_exit();
		ticalcs_library_exit();
		return 0;
	}

	g_signal_connect(emu->ewin->window, "destroy",
	                 G_CALLBACK(gtk_main_quit), NULL);

	gtk_main();

	tilem_calc_emulator_pause(emu);

	tilem_emulator_window_free(emu->ewin);
	tilem_calc_emulator_free(emu);

	menurc_path = get_config_file_path("menurc", NULL);
	gtk_accel_map_save(menurc_path);
	g_free(menurc_path);

	ticables_library_exit();
	tifiles_library_exit();
	ticalcs_library_exit();

	return 0;
}
