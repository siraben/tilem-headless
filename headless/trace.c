/*
 * TilEm II - headless trace writer
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <errno.h>
#include <string.h>

#include "trace.h"

#define TRACE_HEADER_MAGIC "TLMT"
#define TRACE_DEFAULT_RANGE_START 0x8000
#define TRACE_DEFAULT_RANGE_END 0xffff
#define TRACE_VERSION 2
#define TRACE_FLAGS_INSTR 0x1
#define TRACE_FLAGS_MEM_WRITE 0x2
#define TRACE_FLAGS_KEY_EVENT 0x4
#define TRACE_RECORD_INSTR 0x01
#define TRACE_RECORD_MEM_WRITE 0x02
#define TRACE_RECORD_KEY_EVENT 0x03

static gboolean trace_write(TilemTraceWriter *tw, const void *data, size_t len)
{
	if (!tw->enabled)
		return FALSE;
	if (tw->limit_bytes && tw->bytes_written + len > tw->limit_bytes) {
		if (!tw->warned) {
			g_printerr("Trace limit reached; disabling trace.\n");
			tw->warned = TRUE;
		}
		tw->enabled = FALSE;
		return FALSE;
	}
	if (fwrite(data, 1, len, tw->fp) != len) {
		if (!tw->warned) {
			g_printerr("Trace write failed; disabling trace.\n");
			tw->warned = TRUE;
		}
		tw->enabled = FALSE;
		return FALSE;
	}
	tw->bytes_written += len;
	return TRUE;
}

static gboolean trace_write_u16(TilemTraceWriter *tw, guint16 value)
{
	guint8 buf[2];

	buf[0] = (guint8) (value & 0xff);
	buf[1] = (guint8) ((value >> 8) & 0xff);
	return trace_write(tw, buf, sizeof(buf));
}

static gboolean trace_write_u32(TilemTraceWriter *tw, guint32 value)
{
	guint8 buf[4];

	buf[0] = (guint8) (value & 0xff);
	buf[1] = (guint8) ((value >> 8) & 0xff);
	buf[2] = (guint8) ((value >> 16) & 0xff);
	buf[3] = (guint8) ((value >> 24) & 0xff);
	return trace_write(tw, buf, sizeof(buf));
}

static gboolean parse_trace_range(const char *spec,
                                  guint32 *start,
                                  guint32 *end,
                                  GError **err)
{
	char *dash;
	char *endptr = NULL;
	long long start_val;
	long long end_val;

	if (!spec || !*spec || !g_ascii_strcasecmp(spec, "ram")) {
		*start = TRACE_DEFAULT_RANGE_START;
		*end = TRACE_DEFAULT_RANGE_END;
		return TRUE;
	}
	if (!g_ascii_strcasecmp(spec, "all")) {
		*start = 0x0000;
		*end = 0xffff;
		return TRUE;
	}

	dash = strchr(spec, '-');
	if (!dash) {
		g_set_error(err, G_FILE_ERROR, G_FILE_ERROR_INVAL,
		            "Invalid trace range '%s'", spec);
		return FALSE;
	}

	start_val = g_ascii_strtoll(spec, &endptr, 0);
	if (endptr != dash) {
		g_set_error(err, G_FILE_ERROR, G_FILE_ERROR_INVAL,
		            "Invalid trace range '%s'", spec);
		return FALSE;
	}

	end_val = g_ascii_strtoll(dash + 1, &endptr, 0);
	if (!endptr || *endptr != '\0') {
		g_set_error(err, G_FILE_ERROR, G_FILE_ERROR_INVAL,
		            "Invalid trace range '%s'", spec);
		return FALSE;
	}

	if (start_val < 0 || end_val < 0
	    || start_val > 0xffff || end_val > 0xffff
	    || start_val > end_val) {
		g_set_error(err, G_FILE_ERROR, G_FILE_ERROR_INVAL,
		            "Trace range out of bounds '%s'", spec);
		return FALSE;
	}

	*start = (guint32) start_val;
	*end = (guint32) end_val;
	return TRUE;
}

static gboolean trace_write_snapshot(TilemTraceWriter *tw, TilemCalc *calc)
{
	guint8 buf[4096];
	guint32 addr;
	guint32 idx = 0;

	for (addr = tw->range_start; addr <= tw->range_end; addr++) {
		dword phys = (*calc->hw.mem_ltop)(calc, addr);
		buf[idx++] = calc->mem[phys];
		if (idx == sizeof(buf)) {
			if (!trace_write(tw, buf, idx))
				return FALSE;
			idx = 0;
		}
	}

	if (idx)
		return trace_write(tw, buf, idx);

	return TRUE;
}

static void trace_writer_instr(TilemCalc *calc,
                               void *ctx,
                               dword pc,
                               dword opcode)
{
	TilemTraceWriter *tw = ctx;
	guint8 buf[1 + 4 + 4 + 4 + (15 * 2) + 5];
	size_t off = 0;

	if (!tw->enabled)
		return;

	buf[off++] = TRACE_RECORD_INSTR;
	buf[off++] = (guint8) (pc & 0xff);
	buf[off++] = (guint8) ((pc >> 8) & 0xff);
	buf[off++] = (guint8) ((pc >> 16) & 0xff);
	buf[off++] = (guint8) ((pc >> 24) & 0xff);
	buf[off++] = (guint8) (opcode & 0xff);
	buf[off++] = (guint8) ((opcode >> 8) & 0xff);
	buf[off++] = (guint8) ((opcode >> 16) & 0xff);
	buf[off++] = (guint8) ((opcode >> 24) & 0xff);
	buf[off++] = (guint8) (calc->z80.clock & 0xff);
	buf[off++] = (guint8) ((calc->z80.clock >> 8) & 0xff);
	buf[off++] = (guint8) ((calc->z80.clock >> 16) & 0xff);
	buf[off++] = (guint8) ((calc->z80.clock >> 24) & 0xff);

#define TRACE_PUT_U16(val) \
	do { \
		guint16 v_ = (guint16) (val); \
		buf[off++] = (guint8) (v_ & 0xff); \
		buf[off++] = (guint8) ((v_ >> 8) & 0xff); \
	} while (0)

	TRACE_PUT_U16(calc->z80.r.af.w.l);
	TRACE_PUT_U16(calc->z80.r.bc.w.l);
	TRACE_PUT_U16(calc->z80.r.de.w.l);
	TRACE_PUT_U16(calc->z80.r.hl.w.l);
	TRACE_PUT_U16(calc->z80.r.ix.w.l);
	TRACE_PUT_U16(calc->z80.r.iy.w.l);
	TRACE_PUT_U16(calc->z80.r.sp.w.l);
	TRACE_PUT_U16(calc->z80.r.pc.w.l);
	TRACE_PUT_U16(calc->z80.r.ir.w.l);
	TRACE_PUT_U16(calc->z80.r.wz.w.l);
	TRACE_PUT_U16(calc->z80.r.wz2.w.l);
	TRACE_PUT_U16(calc->z80.r.af2.w.l);
	TRACE_PUT_U16(calc->z80.r.bc2.w.l);
	TRACE_PUT_U16(calc->z80.r.de2.w.l);
	TRACE_PUT_U16(calc->z80.r.hl2.w.l);

#undef TRACE_PUT_U16

	buf[off++] = (guint8) (calc->z80.r.iff1 ? 1 : 0);
	buf[off++] = (guint8) (calc->z80.r.iff2 ? 1 : 0);
	buf[off++] = (guint8) calc->z80.r.im;
	buf[off++] = (guint8) calc->z80.r.r7;
	buf[off++] = (guint8) (calc->z80.halted ? 1 : 0);

	trace_write(tw, buf, off);
}

static void trace_writer_mem_write(TilemCalc *calc,
                                   void *ctx,
                                   dword addr,
                                   byte value)
{
	TilemTraceWriter *tw = ctx;
	guint8 buf[1 + 4 + 1];

	(void) calc;
	if (!tw->enabled)
		return;
	if (addr < tw->range_start || addr > tw->range_end)
		return;

	buf[0] = TRACE_RECORD_MEM_WRITE;
	buf[1] = (guint8) (addr & 0xff);
	buf[2] = (guint8) ((addr >> 8) & 0xff);
	buf[3] = (guint8) ((addr >> 16) & 0xff);
	buf[4] = (guint8) ((addr >> 24) & 0xff);
	buf[5] = (guint8) value;

	trace_write(tw, buf, sizeof(buf));
}

static void trace_writer_key_event(TilemCalc *calc,
                                   void *ctx,
                                   int key,
                                   int pressed)
{
	TilemTraceWriter *tw = ctx;
	guint8 buf[1 + 1 + 1 + 4 + 2];
	dword pc;

	if (!tw->enabled)
		return;

	pc = calc->z80.r.pc.w.l;
	buf[0] = TRACE_RECORD_KEY_EVENT;
	buf[1] = pressed ? 1 : 0;
	buf[2] = (guint8) (key & 0xff);
	buf[3] = (guint8) (calc->z80.clock & 0xff);
	buf[4] = (guint8) ((calc->z80.clock >> 8) & 0xff);
	buf[5] = (guint8) ((calc->z80.clock >> 16) & 0xff);
	buf[6] = (guint8) ((calc->z80.clock >> 24) & 0xff);
	buf[7] = (guint8) (pc & 0xff);
	buf[8] = (guint8) ((pc >> 8) & 0xff);

	trace_write(tw, buf, sizeof(buf));
}

gboolean tilem_trace_writer_init(TilemTraceWriter *tw,
                                 TilemCalc *calc,
                                 const char *path,
                                 const char *range_spec,
                                 guint64 limit_bytes,
                                 GError **err)
{
	guint16 version = TRACE_VERSION;
	guint16 flags = TRACE_FLAGS_INSTR
	                | TRACE_FLAGS_MEM_WRITE
	                | TRACE_FLAGS_KEY_EVENT;
	guint32 init_size;
	guint32 range_start = 0;
	guint32 range_end = 0;

	if (!parse_trace_range(range_spec, &range_start, &range_end, err))
		return FALSE;

	memset(tw, 0, sizeof(*tw));
	tw->fp = fopen(path, "wb");
	if (!tw->fp) {
		g_set_error(err, G_FILE_ERROR, G_FILE_ERROR_FAILED,
		            "Unable to open trace file %s: %s",
		            path, g_strerror(errno));
		return FALSE;
	}

	setvbuf(tw->fp, NULL, _IOFBF, 1024 * 1024);
	tw->calc = calc;
	tw->range_start = range_start;
	tw->range_end = range_end;
	tw->limit_bytes = limit_bytes;
	tw->enabled = TRUE;
	tw->trace.ctx = tw;
	tw->trace.instr = trace_writer_instr;
	tw->trace.mem_write = trace_writer_mem_write;
	tw->trace.key_event = trace_writer_key_event;
	calc->trace = &tw->trace;

	init_size = (range_end - range_start) + 1;
	if (!trace_write(tw, TRACE_HEADER_MAGIC, 4))
		return FALSE;
	if (!trace_write_u16(tw, version)
	    || !trace_write_u16(tw, flags)
	    || !trace_write_u32(tw, range_start)
	    || !trace_write_u32(tw, range_end)
	    || !trace_write_u32(tw, init_size)) {
		return FALSE;
	}

	return trace_write_snapshot(tw, calc);
}

void tilem_trace_writer_close(TilemTraceWriter *tw)
{
	if (tw->calc && tw->calc->trace == &tw->trace)
		tw->calc->trace = NULL;
	if (tw->fp)
		fclose(tw->fp);
	tw->fp = NULL;
	tw->enabled = FALSE;
	tw->calc = NULL;
}
