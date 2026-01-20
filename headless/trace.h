/*
 * TilEm II - headless trace writer
 */

#ifndef TILEM_TRACE_H
#define TILEM_TRACE_H

#include <stdio.h>

#include <glib.h>
#include <tilem.h>

typedef struct {
	TilemTrace trace;
	TilemCalc *calc;
	FILE *fp;
	guint64 bytes_written;
	guint64 limit_bytes;
	guint32 range_start;
	guint32 range_end;
	gboolean enabled;
	gboolean warned;
} TilemTraceWriter;

gboolean tilem_trace_writer_init(TilemTraceWriter *tw,
                                 TilemCalc *calc,
                                 const char *path,
                                 const char *range_spec,
                                 guint64 limit_bytes,
                                 GError **err);

void tilem_trace_writer_close(TilemTraceWriter *tw);

#endif
