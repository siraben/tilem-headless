/*
 * TilEm II
 *
 * Copyright (c) 2010-2011 Thibault Duponchelle
 * Copyright (c) 2010-2011 Benjamin Moody
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

#ifndef TILEM_VARENTRY_H
#define TILEM_VARENTRY_H

#include <glib.h>
#include <ticalcs.h>

/* This structure is a wrapper for VarEntry with additional metadata. */
typedef struct {
	int model;

	VarEntry *ve;             /* Original variable info retrieved */
	int slot;                 /* Slot number */

	/* Strings for display (UTF-8) */
	char *name_str;           /* Variable name */
	char *type_str;           /* Variable type */
	char *slot_str;           /* Program slot */
	char *file_ext;           /* Default file extension */
	char *filetype_desc;      /* File format description */

	int size;                 /* Variable size */
	gboolean archived;        /* Is archived */
	gboolean can_group;       /* Can be stored in group file */
} TilemVarEntry;

void tilem_var_entry_free(TilemVarEntry *tve);
TilemVarEntry *tilem_var_entry_copy(const TilemVarEntry *tve);

#endif
