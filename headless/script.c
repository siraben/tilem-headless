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

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include <glib.h>
#include <tilem.h>
#include <scancodes.h>

#include "script.h"

typedef struct {
	const TilemHeadlessOps *ops;
	TilemHeadlessScriptSettings settings;
} TilemScriptState;

typedef struct {
	const char *name;
	int key;
} KeyMap;

static const KeyMap key_map[] = {
	{ "DOWN", TILEM_KEY_DOWN },
	{ "LEFT", TILEM_KEY_LEFT },
	{ "RIGHT", TILEM_KEY_RIGHT },
	{ "UP", TILEM_KEY_UP },
	{ "ENTER", TILEM_KEY_ENTER },
	{ "RETURN", TILEM_KEY_ENTER },
	{ "ADD", TILEM_KEY_ADD },
	{ "PLUS", TILEM_KEY_ADD },
	{ "SUB", TILEM_KEY_SUB },
	{ "MINUS", TILEM_KEY_SUB },
	{ "MUL", TILEM_KEY_MUL },
	{ "TIMES", TILEM_KEY_MUL },
	{ "DIV", TILEM_KEY_DIV },
	{ "POWER", TILEM_KEY_POWER },
	{ "CLEAR", TILEM_KEY_CLEAR },
	{ "CHS", TILEM_KEY_CHS },
	{ "DECPNT", TILEM_KEY_DECPNT },
	{ "DECPT", TILEM_KEY_DECPNT },
	{ "LPAREN", TILEM_KEY_LPAREN },
	{ "RPAREN", TILEM_KEY_RPAREN },
	{ "COMMA", TILEM_KEY_COMMA },
	{ "SIN", TILEM_KEY_SIN },
	{ "COS", TILEM_KEY_COS },
	{ "TAN", TILEM_KEY_TAN },
	{ "LOG", TILEM_KEY_LOG },
	{ "LN", TILEM_KEY_LN },
	{ "SQUARE", TILEM_KEY_SQUARE },
	{ "RECIP", TILEM_KEY_RECIP },
	{ "MATH", TILEM_KEY_MATH },
	{ "PRGM", TILEM_KEY_PRGM },
	{ "VARS", TILEM_KEY_VARS },
	{ "APPS", TILEM_KEY_MATRIX },
	{ "MATRIX", TILEM_KEY_MATRIX },
	{ "GRAPHVAR", TILEM_KEY_GRAPHVAR },
	{ "ON", TILEM_KEY_ON },
	{ "STORE", TILEM_KEY_STORE },
	{ "ALPHA", TILEM_KEY_ALPHA },
	{ "GRAPH", TILEM_KEY_GRAPH },
	{ "TRACE", TILEM_KEY_TRACE },
	{ "ZOOM", TILEM_KEY_ZOOM },
	{ "WINDOW", TILEM_KEY_WINDOW },
	{ "YEQU", TILEM_KEY_YEQU },
	{ "Y=", TILEM_KEY_YEQU },
	{ "2ND", TILEM_KEY_2ND },
	{ "SECOND", TILEM_KEY_2ND },
	{ "MODE", TILEM_KEY_MODE },
	{ "DEL", TILEM_KEY_DEL },
	{ "DELETE", TILEM_KEY_DEL },
	{ "STAT", TILEM_KEY_STAT },
};

static gboolean parse_seconds(const char *token, double *out)
{
	char *end = NULL;
	double value;

	if (!token || !*token)
		return FALSE;

	value = g_ascii_strtod(token, &end);
	if (end == token)
		return FALSE;

	if (*end == '\0' || !strcmp(end, "s")) {
		*out = value;
		return TRUE;
	}
	if (!strcmp(end, "ms")) {
		*out = value / 1000.0;
		return TRUE;
	}

	return FALSE;
}

static int keycode_from_name(const char *name)
{
	int i;

	if (!name || !*name)
		return 0;

	if (strlen(name) == 1 && g_ascii_isdigit(name[0])) {
		switch (name[0]) {
		case '0': return TILEM_KEY_0;
		case '1': return TILEM_KEY_1;
		case '2': return TILEM_KEY_2;
		case '3': return TILEM_KEY_3;
		case '4': return TILEM_KEY_4;
		case '5': return TILEM_KEY_5;
		case '6': return TILEM_KEY_6;
		case '7': return TILEM_KEY_7;
		case '8': return TILEM_KEY_8;
		case '9': return TILEM_KEY_9;
		default: break;
		}
	}

	for (i = 0; i < (int) G_N_ELEMENTS(key_map); i++) {
		if (!g_ascii_strcasecmp(name, key_map[i].name))
			return key_map[i].key;
	}

	return 0;
}

static gboolean alpha_key_for_letter(char c, int *key)
{
	switch (g_ascii_toupper(c)) {
	case 'A': *key = TILEM_KEY_MATH; return TRUE;
	case 'B': *key = TILEM_KEY_MATRIX; return TRUE;
	case 'C': *key = TILEM_KEY_PRGM; return TRUE;
	case 'D': *key = TILEM_KEY_VARS; return TRUE;
	case 'E': *key = TILEM_KEY_POWER; return TRUE;
	case 'F': *key = TILEM_KEY_RECIP; return TRUE;
	case 'G': *key = TILEM_KEY_SIN; return TRUE;
	case 'H': *key = TILEM_KEY_COS; return TRUE;
	case 'I': *key = TILEM_KEY_TAN; return TRUE;
	case 'J': *key = TILEM_KEY_SQUARE; return TRUE;
	case 'K': *key = TILEM_KEY_COMMA; return TRUE;
	case 'L': *key = TILEM_KEY_LPAREN; return TRUE;
	case 'M': *key = TILEM_KEY_RPAREN; return TRUE;
	case 'N': *key = TILEM_KEY_DIV; return TRUE;
	case 'O': *key = TILEM_KEY_LOG; return TRUE;
	case 'P': *key = TILEM_KEY_7; return TRUE;
	case 'Q': *key = TILEM_KEY_8; return TRUE;
	case 'R': *key = TILEM_KEY_9; return TRUE;
	case 'S': *key = TILEM_KEY_MUL; return TRUE;
	case 'T': *key = TILEM_KEY_LN; return TRUE;
	case 'U': *key = TILEM_KEY_4; return TRUE;
	case 'V': *key = TILEM_KEY_5; return TRUE;
	case 'W': *key = TILEM_KEY_6; return TRUE;
	case 'X': *key = TILEM_KEY_SUB; return TRUE;
	case 'Y': *key = TILEM_KEY_1; return TRUE;
	case 'Z': *key = TILEM_KEY_2; return TRUE;
	default:
		break;
	}

	return FALSE;
}

static void tap_key(TilemScriptState *state, int key, double hold)
{
	state->ops->press_key(state->ops->ctx, key);
	if (hold > 0.0)
		state->ops->advance_time(state->ops->ctx, hold);
	state->ops->release_key(state->ops->ctx, key);
}

static gboolean type_char(TilemScriptState *state, char c, GError **err)
{
	int key = 0;

	if (g_ascii_isalpha(c)) {
		if (!alpha_key_for_letter(c, &key)) {
			g_set_error(err, G_FILE_ERROR, G_FILE_ERROR_INVAL,
			            "Unsupported letter '%c'", c);
			return FALSE;
		}
		tap_key(state, TILEM_KEY_ALPHA, state->settings.key_hold);
		tap_key(state, key, state->settings.key_hold);
		return TRUE;
	}

	if (g_ascii_isdigit(c)) {
		key = keycode_from_name((char[2]){c, '\0'});
		tap_key(state, key, state->settings.key_hold);
		return TRUE;
	}

	switch (c) {
	case ' ':
		tap_key(state, TILEM_KEY_ALPHA, state->settings.key_hold);
		tap_key(state, TILEM_KEY_0, state->settings.key_hold);
		return TRUE;
	case '.':
		tap_key(state, TILEM_KEY_DECPNT, state->settings.key_hold);
		return TRUE;
	case ',':
		tap_key(state, TILEM_KEY_COMMA, state->settings.key_hold);
		return TRUE;
	case '(':
		tap_key(state, TILEM_KEY_LPAREN, state->settings.key_hold);
		return TRUE;
	case ')':
		tap_key(state, TILEM_KEY_RPAREN, state->settings.key_hold);
		return TRUE;
	case '+':
		tap_key(state, TILEM_KEY_ADD, state->settings.key_hold);
		return TRUE;
	case '-':
		tap_key(state, TILEM_KEY_SUB, state->settings.key_hold);
		return TRUE;
	case '*':
		tap_key(state, TILEM_KEY_MUL, state->settings.key_hold);
		return TRUE;
	case '/':
		tap_key(state, TILEM_KEY_DIV, state->settings.key_hold);
		return TRUE;
	case '^':
		tap_key(state, TILEM_KEY_POWER, state->settings.key_hold);
		return TRUE;
	case '\n':
		tap_key(state, TILEM_KEY_ENTER, state->settings.key_hold);
		return TRUE;
	default:
		break;
	}

	g_set_error(err, G_FILE_ERROR, G_FILE_ERROR_INVAL,
	            "Unsupported character '%c'", c);
	return FALSE;
}

static gboolean run_type(TilemScriptState *state, const char *text, GError **err)
{
	int i;
	int len;

	if (!text || !*text)
		return TRUE;

	len = (int) strlen(text);
	for (i = 0; i < len; i++) {
		if (!type_char(state, text[i], err))
			return FALSE;
		if (state->settings.key_delay > 0.0 && i + 1 < len)
			state->ops->advance_time(state->ops->ctx,
			                         state->settings.key_delay);
	}

	return TRUE;
}

static gboolean parse_set(TilemScriptState *state,
                          char **tokens,
                          int line_no,
                          GError **err)
{
	double value = 0.0;

	if (!tokens[1] || !tokens[2]) {
		g_set_error(err, G_FILE_ERROR, G_FILE_ERROR_INVAL,
		            "Line %d: set requires a key and a value", line_no);
		return FALSE;
	}

	if (!parse_seconds(tokens[2], &value) || value < 0.0) {
		g_set_error(err, G_FILE_ERROR, G_FILE_ERROR_INVAL,
		            "Line %d: invalid time value '%s'", line_no, tokens[2]);
		return FALSE;
	}

	if (!g_ascii_strcasecmp(tokens[1], "key_delay")) {
		state->settings.key_delay = value;
		return TRUE;
	}
	if (!g_ascii_strcasecmp(tokens[1], "key_hold")) {
		state->settings.key_hold = value;
		return TRUE;
	}

	g_set_error(err, G_FILE_ERROR, G_FILE_ERROR_INVAL,
	            "Line %d: unknown setting '%s'", line_no, tokens[1]);
	return FALSE;
}

static gboolean run_key_command(TilemScriptState *state,
                                const char *name,
                                const char *hold_token,
                                int line_no,
                                GError **err)
{
	double hold = state->settings.key_hold;
	int key;

	if (!name) {
		g_set_error(err, G_FILE_ERROR, G_FILE_ERROR_INVAL,
		            "Line %d: key requires a name", line_no);
		return FALSE;
	}

	key = keycode_from_name(name);
	if (!key) {
		g_set_error(err, G_FILE_ERROR, G_FILE_ERROR_INVAL,
		            "Line %d: unknown key '%s'", line_no, name);
		return FALSE;
	}

	if (hold_token) {
		if (!parse_seconds(hold_token, &hold) || hold < 0.0) {
			g_set_error(err, G_FILE_ERROR, G_FILE_ERROR_INVAL,
			            "Line %d: invalid hold time '%s'",
			            line_no, hold_token);
			return FALSE;
		}
	}

	tap_key(state, key, hold);
	return TRUE;
}

static gboolean run_line(TilemScriptState *state,
                         const char *line,
                         int line_no,
                         GError **err)
{
	char *trimmed;
	char **tokens;
	gboolean ok = TRUE;

	trimmed = g_strdup(line);
	g_strstrip(trimmed);

	if (!*trimmed || trimmed[0] == '#'
	    || (trimmed[0] == '/' && trimmed[1] == '/')) {
		g_free(trimmed);
		return TRUE;
	}

	tokens = g_strsplit_set(trimmed, " \t", -1);
	if (!tokens[0]) {
		g_strfreev(tokens);
		g_free(trimmed);
		return TRUE;
	}

	if (!g_ascii_strcasecmp(tokens[0], "wait")
	    || !g_ascii_strcasecmp(tokens[0], "sleep")
	    || !g_ascii_strcasecmp(tokens[0], "pause")) {
		double seconds = 0.0;

		if (!tokens[1] || !parse_seconds(tokens[1], &seconds)
		    || seconds < 0.0) {
			g_set_error(err, G_FILE_ERROR, G_FILE_ERROR_INVAL,
			            "Line %d: invalid wait value", line_no);
			ok = FALSE;
		}
		else {
			state->ops->advance_time(state->ops->ctx, seconds);
		}
	}
	else if (!g_ascii_strcasecmp(tokens[0], "set")) {
		ok = parse_set(state, tokens, line_no, err);
	}
	else if (!g_ascii_strcasecmp(tokens[0], "key")) {
		const char *hold_token = NULL;

		if (tokens[2]) {
			if (!g_ascii_strcasecmp(tokens[2], "hold"))
				hold_token = tokens[3];
			else
				hold_token = tokens[2];
		}

		ok = run_key_command(state, tokens[1], hold_token, line_no, err);
	}
	else if (!g_ascii_strcasecmp(tokens[0], "press")) {
		int key = keycode_from_name(tokens[1]);

		if (!key) {
			g_set_error(err, G_FILE_ERROR, G_FILE_ERROR_INVAL,
			            "Line %d: unknown key '%s'", line_no,
			            tokens[1] ? tokens[1] : "");
			ok = FALSE;
		}
		else {
			state->ops->press_key(state->ops->ctx, key);
		}
	}
	else if (!g_ascii_strcasecmp(tokens[0], "release")) {
		int key = keycode_from_name(tokens[1]);

		if (!key) {
			g_set_error(err, G_FILE_ERROR, G_FILE_ERROR_INVAL,
			            "Line %d: unknown key '%s'", line_no,
			            tokens[1] ? tokens[1] : "");
			ok = FALSE;
		}
		else {
			state->ops->release_key(state->ops->ctx, key);
		}
	}
	else if (!g_ascii_strcasecmp(tokens[0], "type")) {
		const char *text = trimmed + 4;
		char *payload;

		while (*text && g_ascii_isspace(*text))
			text++;

		if (*text == '"') {
			const char *end = strrchr(text + 1, '"');
			if (!end || end == text + 1) {
				g_set_error(err, G_FILE_ERROR, G_FILE_ERROR_INVAL,
				            "Line %d: unterminated string", line_no);
				ok = FALSE;
			}
			else {
				payload = g_strndup(text + 1, end - text - 1);
				ok = run_type(state, payload, err);
				g_free(payload);
			}
		}
		else {
			ok = run_type(state, text, err);
		}
	}
	else {
		g_set_error(err, G_FILE_ERROR, G_FILE_ERROR_INVAL,
		            "Line %d: unknown command '%s'", line_no, tokens[0]);
		ok = FALSE;
	}

	g_strfreev(tokens);
	g_free(trimmed);
	return ok;
}

gboolean tilem_headless_script_run(const char *path,
                                   const TilemHeadlessOps *ops,
                                   TilemHeadlessScriptSettings *settings,
                                   GError **err)
{
	char *contents = NULL;
	gsize length = 0;
	gboolean ok = TRUE;
	TilemScriptState state;
	int line_no = 1;
	char *line_start;
	char *cursor;

	g_return_val_if_fail(path != NULL, FALSE);
	g_return_val_if_fail(ops != NULL, FALSE);
	g_return_val_if_fail(ops->press_key != NULL, FALSE);
	g_return_val_if_fail(ops->release_key != NULL, FALSE);
	g_return_val_if_fail(ops->advance_time != NULL, FALSE);
	g_return_val_if_fail(err == NULL || *err == NULL, FALSE);

	if (!g_file_get_contents(path, &contents, &length, err))
		return FALSE;

	state.ops = ops;
	if (settings) {
		state.settings = *settings;
	}
	else {
		state.settings.key_hold = TILEM_HEADLESS_DEFAULT_KEY_HOLD;
		state.settings.key_delay = TILEM_HEADLESS_DEFAULT_KEY_DELAY;
	}

	line_start = contents;
	cursor = contents;
	while (ok && *cursor) {
		if (*cursor == '\n') {
			*cursor = '\0';
			ok = run_line(&state, line_start, line_no, err);
			line_no++;
			line_start = cursor + 1;
		}
		cursor++;
	}

	if (ok && line_start && *line_start)
		ok = run_line(&state, line_start, line_no, err);

	if (settings)
		*settings = state.settings;

	g_free(contents);
	return ok;
}
