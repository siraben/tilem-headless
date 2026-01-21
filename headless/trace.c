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
#define TRACE_HEADER_LEN 20
#define TRACE_RECORD_INSTR_LEN (1 + 4 + 4 + 4 + (15 * 2) + 5)
#define TRACE_RECORD_MEM_WRITE_LEN (1 + 4 + 1)
#define TRACE_RECORD_KEY_EVENT_LEN (1 + 1 + 1 + 4 + 2)
#define TRACE_RECORD_MAX_LEN TRACE_RECORD_INSTR_LEN

static guint8 *trace_build_header(TilemTraceWriter *tw,
                                  TilemCalc *calc,
                                  guint32 init_size,
                                  gsize *out_len);

static size_t trace_record_len(guint8 type)
{
	switch (type) {
	case TRACE_RECORD_INSTR:
		return TRACE_RECORD_INSTR_LEN;
	case TRACE_RECORD_MEM_WRITE:
		return TRACE_RECORD_MEM_WRITE_LEN;
	case TRACE_RECORD_KEY_EVENT:
		return TRACE_RECORD_KEY_EVENT_LEN;
	default:
		return 0;
	}
}

static void trace_ring_read(const TilemTraceWriter *tw,
                            gsize pos,
                            guint8 *dst,
                            gsize len)
{
	gsize first;

	if (!len)
		return;
	first = MIN(len, tw->ring_size - pos);
	memcpy(dst, tw->ring + pos, first);
	if (len > first)
		memcpy(dst + first, tw->ring, len - first);
}

static void trace_snapshot_apply_mem_write(TilemTraceWriter *tw,
                                           guint32 addr,
                                           guint8 value)
{
	gsize idx;

	if (!tw->header)
		return;
	if (addr < tw->range_start || addr > tw->range_end)
		return;

	idx = TRACE_HEADER_LEN + (gsize) (addr - tw->range_start);
	if (idx < tw->header_len)
		tw->header[idx] = value;
}

static void trace_ring_resync(TilemTraceWriter *tw)
{
	guint32 init_size;
	gsize header_len = 0;

	if (!tw->calc)
		goto reset;

	init_size = (tw->range_end - tw->range_start) + 1;
	g_free(tw->header);
	tw->header = trace_build_header(tw, tw->calc, init_size, &header_len);
	tw->header_len = header_len;

reset:
	tw->ring_start = 0;
	tw->ring_pos = 0;
	tw->ring_used = 0;
	tw->ring_wrapped = FALSE;
}

static gboolean trace_write_ring(TilemTraceWriter *tw,
                                 const void *data,
                                 size_t len)
{
	const guint8 *src = data;
	guint8 rec_buf[TRACE_RECORD_MAX_LEN];

	if (!tw->enabled || !tw->ring || !tw->ring_size)
		return FALSE;

	if (len >= tw->ring_size) {
		src += (len - tw->ring_size);
		len = tw->ring_size;
	}

	while (tw->ring_used + len > tw->ring_size) {
		guint8 rec_type;
		size_t rec_len;

		if (!tw->ring_used)
			break;
		rec_type = tw->ring[tw->ring_start];
		rec_len = trace_record_len(rec_type);
		if (!rec_len || rec_len > tw->ring_used
		    || rec_len > sizeof(rec_buf)) {
			trace_ring_resync(tw);
			break;
		}

		trace_ring_read(tw, tw->ring_start, rec_buf, rec_len);
		if (rec_type == TRACE_RECORD_MEM_WRITE
		    && rec_len == TRACE_RECORD_MEM_WRITE_LEN) {
			guint32 addr = (guint32) rec_buf[1]
			               | ((guint32) rec_buf[2] << 8)
			               | ((guint32) rec_buf[3] << 16)
			               | ((guint32) rec_buf[4] << 24);
			trace_snapshot_apply_mem_write(tw, addr, rec_buf[5]);
		}

		tw->ring_start = (tw->ring_start + rec_len) % tw->ring_size;
		tw->ring_used -= rec_len;
	}

	if (len) {
		gsize first = MIN((gsize) len, tw->ring_size - tw->ring_pos);

		memcpy(tw->ring + tw->ring_pos, src, first);
		if (len > first)
			memcpy(tw->ring, src + first, len - first);

		tw->ring_pos = (tw->ring_pos + len) % tw->ring_size;
		tw->ring_used += len;
		tw->ring_wrapped = (tw->ring_used == tw->ring_size);
	}
	return TRUE;
}

static gboolean trace_write(TilemTraceWriter *tw, const void *data, size_t len)
{
	if (!tw->enabled)
		return FALSE;
	if (tw->ring_mode)
		return trace_write_ring(tw, data, len);
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

static void trace_put_u16(guint8 *buf, size_t *off, guint16 value)
{
	buf[(*off)++] = (guint8) (value & 0xff);
	buf[(*off)++] = (guint8) ((value >> 8) & 0xff);
}

static void trace_put_u32(guint8 *buf, size_t *off, guint32 value)
{
	buf[(*off)++] = (guint8) (value & 0xff);
	buf[(*off)++] = (guint8) ((value >> 8) & 0xff);
	buf[(*off)++] = (guint8) ((value >> 16) & 0xff);
	buf[(*off)++] = (guint8) ((value >> 24) & 0xff);
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

static guint8 *trace_build_header(TilemTraceWriter *tw,
                                  TilemCalc *calc,
                                  guint32 init_size,
                                  gsize *out_len)
{
	guint16 version = TRACE_VERSION;
	guint16 flags = TRACE_FLAGS_INSTR
	                | TRACE_FLAGS_MEM_WRITE
	                | TRACE_FLAGS_KEY_EVENT;
	gsize total = TRACE_HEADER_LEN + init_size;
	guint8 *buf = g_malloc(total);
	size_t off = 0;
	guint32 addr;
	guint32 idx = TRACE_HEADER_LEN;

	memcpy(buf + off, TRACE_HEADER_MAGIC, 4);
	off += 4;
	trace_put_u16(buf, &off, version);
	trace_put_u16(buf, &off, flags);
	trace_put_u32(buf, &off, tw->range_start);
	trace_put_u32(buf, &off, tw->range_end);
	trace_put_u32(buf, &off, init_size);

	for (addr = tw->range_start; addr <= tw->range_end; addr++) {
		dword phys = (*calc->hw.mem_ltop)(calc, addr);
		buf[idx++] = calc->mem[phys];
	}

	*out_len = total;
	return buf;
}

static gboolean trace_flush_ring(TilemTraceWriter *tw, GError **err)
{
	FILE *fp;
	gsize first_len;
	gsize used;

	if (!tw->path || !tw->header)
		return TRUE;

	fp = fopen(tw->path, "wb");
	if (!fp) {
		g_set_error(err, G_FILE_ERROR, G_FILE_ERROR_FAILED,
		            "Unable to open trace file %s: %s",
		            tw->path, g_strerror(errno));
		return FALSE;
	}

	if (fwrite(tw->header, 1, tw->header_len, fp) != tw->header_len) {
		fclose(fp);
		g_set_error(err, G_FILE_ERROR, G_FILE_ERROR_FAILED,
		            "Failed to write trace header to %s", tw->path);
		return FALSE;
	}

	if (!tw->ring || !tw->ring_size) {
		fclose(fp);
		return TRUE;
	}
	used = tw->ring_used;
	if (!used) {
		fclose(fp);
		return TRUE;
	}

	first_len = MIN(used, tw->ring_size - tw->ring_start);
	if (fwrite(tw->ring + tw->ring_start, 1, first_len, fp) != first_len) {
		fclose(fp);
		g_set_error(err, G_FILE_ERROR, G_FILE_ERROR_FAILED,
		            "Failed to write trace backtrace to %s", tw->path);
		return FALSE;
	}
	used -= first_len;
	if (used
	    && fwrite(tw->ring, 1, used, fp) != used) {
		fclose(fp);
		g_set_error(err, G_FILE_ERROR, G_FILE_ERROR_FAILED,
		            "Failed to write trace backtrace to %s", tw->path);
		return FALSE;
	}

	fclose(fp);
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
	guint32 init_size;
	guint32 range_start = 0;
	guint32 range_end = 0;
	gsize header_len = 0;

	if (!parse_trace_range(range_spec, &range_start, &range_end, err))
		return FALSE;

	memset(tw, 0, sizeof(*tw));
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
	tw->header = trace_build_header(tw, calc, init_size, &header_len);
	tw->header_len = header_len;

	tw->fp = fopen(path, "wb");
	if (!tw->fp) {
		g_set_error(err, G_FILE_ERROR, G_FILE_ERROR_FAILED,
		            "Unable to open trace file %s: %s",
		            path, g_strerror(errno));
		return FALSE;
	}

	setvbuf(tw->fp, NULL, _IOFBF, 1024 * 1024);
	if (fwrite(tw->header, 1, tw->header_len, tw->fp) != tw->header_len) {
		g_set_error(err, G_FILE_ERROR, G_FILE_ERROR_FAILED,
		            "Failed to write trace header to %s", path);
		return FALSE;
	}

	return TRUE;
}

gboolean tilem_trace_writer_init_backtrace(TilemTraceWriter *tw,
                                           TilemCalc *calc,
                                           const char *path,
                                           const char *range_spec,
                                           guint64 limit_bytes,
                                           GError **err)
{
	guint32 init_size;
	guint32 range_start = 0;
	guint32 range_end = 0;
	gsize header_len = 0;

	if (!parse_trace_range(range_spec, &range_start, &range_end, err))
		return FALSE;

	memset(tw, 0, sizeof(*tw));
	tw->calc = calc;
	tw->range_start = range_start;
	tw->range_end = range_end;
	tw->limit_bytes = limit_bytes;
	tw->ring_mode = TRUE;
	tw->ring_size = (gsize) limit_bytes;
	tw->ring = g_malloc0(tw->ring_size);
	tw->ring_start = 0;
	tw->ring_used = 0;
	tw->path = g_strdup(path);
	tw->enabled = TRUE;
	tw->trace.ctx = tw;
	tw->trace.instr = trace_writer_instr;
	tw->trace.mem_write = trace_writer_mem_write;
	tw->trace.key_event = trace_writer_key_event;
	calc->trace = &tw->trace;

	init_size = (range_end - range_start) + 1;
	tw->header = trace_build_header(tw, calc, init_size, &header_len);
	tw->header_len = header_len;
	return TRUE;
}

void tilem_trace_writer_close(TilemTraceWriter *tw)
{
	GError *err = NULL;

	if (tw->calc && tw->calc->trace == &tw->trace)
		tw->calc->trace = NULL;
	if (tw->ring_mode) {
		if (!trace_flush_ring(tw, &err)) {
			g_printerr("Trace backtrace flush failed: %s\n",
			           err ? err->message : "unknown error");
			g_clear_error(&err);
		}
	}
	if (tw->fp)
		fclose(tw->fp);
	g_free(tw->ring);
	g_free(tw->header);
	g_free(tw->path);
	tw->fp = NULL;
	tw->ring = NULL;
	tw->header = NULL;
	tw->path = NULL;
	tw->enabled = FALSE;
	tw->calc = NULL;
}
