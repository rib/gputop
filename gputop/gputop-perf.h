/*
 * GPU Top
 *
 * Copyright (C) 2015 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef _GPUTOP_PERF_H_
#define _GPUTOP_PERF_H_

#include <stdbool.h>

#ifndef EMSCRIPTEN
#include <uv.h>
#include <time.h>
#endif

#include "i915_oa_drm.h"

#include "gputop-list.h"
#include "gputop-hash-table.h"

typedef enum {
    GPUTOP_PERFQUERY_COUNTER_DATA_UINT64,
    GPUTOP_PERFQUERY_COUNTER_DATA_UINT32,
    GPUTOP_PERFQUERY_COUNTER_DATA_DOUBLE,
    GPUTOP_PERFQUERY_COUNTER_DATA_FLOAT,
    GPUTOP_PERFQUERY_COUNTER_DATA_BOOL32,
} gputop_counter_data_type_t;

typedef enum {
    GPUTOP_PERFQUERY_COUNTER_RAW,
    GPUTOP_PERFQUERY_COUNTER_DURATION_RAW,
    GPUTOP_PERFQUERY_COUNTER_DURATION_NORM,
    GPUTOP_PERFQUERY_COUNTER_EVENT,
    GPUTOP_PERFQUERY_COUNTER_THROUGHPUT,
    GPUTOP_PERFQUERY_COUNTER_TIMESTAMP,
} gputop_counter_type_t;


#define OAREPORT_REASON_MASK           0x3f
#define OAREPORT_REASON_SHIFT          19
#define OAREPORT_REASON_CTX_SWITCH     (1<<3)

struct gputop_devinfo {
    uint32_t devid;
    uint32_t gen;
    uint64_t n_eus;
    uint64_t n_eu_slices;
    uint64_t n_eu_sub_slices;
    uint64_t eu_threads_count;
    uint64_t subslice_mask;
    uint64_t slice_mask;
};

struct gputop_perf_query;
uint64_t get_time(void);
struct gputop_perf_query_counter
{
   const char *name;
   const char *desc;
   gputop_counter_type_t type;
   gputop_counter_data_type_t data_type;
   uint64_t (*max)(struct gputop_devinfo *devinfo,
		   const struct gputop_perf_query *query,
		   uint64_t *accumulator);

   union {
      uint64_t (*oa_counter_read_uint64)(struct gputop_devinfo *devinfo,
                                         const struct gputop_perf_query *query,
                                         uint64_t *accumulator);
      float (*oa_counter_read_float)(struct gputop_devinfo *devinfo,
                                     const struct gputop_perf_query *query,
                                     uint64_t *accumulator);
   };
};

#define MAX_RAW_OA_COUNTERS 62

struct gputop_perf_query
{
    const char *name;
    const char *symbol_name;
    const char *guid;
    struct gputop_perf_query_counter *counters;
    int n_counters;
    bool per_ctx_mode;

    int perf_oa_metrics_set;
    int perf_oa_format;
    int perf_raw_size;

    /* For indexing into the accumulator[] ... */
    int gpu_time_offset;
    int gpu_clock_offset;
    int a_offset;
    int b_offset;
    int c_offset;

    uint64_t accumulator[MAX_RAW_OA_COUNTERS];

    gputop_list_t link;
};

struct ctx_handle {
    gputop_list_t link;

    uint32_t id;
    int fd;
};

#ifndef EMSCRIPTEN
/*
 * This structure tracks the offsets to the perf record headers in our
 * perf circular buffer so that we can enable perf's flight recorder
 * mode whereby perf may overwrite old samples and records with new
 * ones but in doing so it trashes the perf headers that we need to
 * determine the location of sequential records.
 *
 * The alternative to this would be to copy all data out of the perf
 * circular buffer into another buffer, so it can't be overwritten by
 * perf but that would demand further memory bandwidth that we want to
 * avoid wasting.
 *
 * With this approach we don't *actually* enable perf's flight
 * recorder mode, because as we update our record of header offsets we
 * can update the perf tail to say that these records have been read
 * so we can have our flight recorder mode without perf knowing. The
 * advantage of this is that if for some corner case userspace gets
 * blocked from updating its record of header offsets for a long time
 * and the kernel comes to wrap around then we will get notified of
 * that condition by the kernel. In that case we end up loosing recent
 * samples instead of old ones, but something has to have gone quite
 * badly wrong in that case anyway so it's hopefully better to report
 * errors reliably instead.
 *
 * To simplify this, there is a limit on the number of headers we
 * track, based on the expected number of samples that could fit
 * in a full perf circular buffer.
 *
 * The header offsets are stored in a circular buffer of packed uint32
 * values.
 *
 * Due to the fixed size of this buffer, but the possibility of many
 * small perf records, (much smaller than our expected samples),
 * there's a small chance that we fill this buffer and still loose
 * track of some older headers. We can report this case as an error
 * though, and this is a more graceful failure than having to scrap
 * the entire perf buffer.
 *
 *
 * For each update of this buffer we:
 *
 * 1) Check what new records have been added:
 *
 *    * if buf->last_perf_head uninitialized, set it to the perf tail
 *    * foreach new record from buf->last_perf_head to the current perf head:
 *        - check there's room for a new header offset, but if not:
 *            - report an error
 *            - move the tail forward (loosing a record)
 *        - add a header offset to buf->offsets[buf->head]
 *        - buf->head = (buf->head + 1) % buf->len;
 *        - recognise when the perf head wraps and mark the buffer 'full'
 *
 * 2) Optionally parse any of the new records (i.e. before we update
 *    tail)
 *
 *    Typically we aren't processing the records while tracing, but
 *    beware that if anything does need passing on the fly then it
 *    needs to be done before we update the tail pointer below.
 *
 * 3) If buf 'full'; check how much of perf's tail has been eaten:
 *
 *    * move buf->tail forward to the next offset that is ahead of
 *      perf's (head + header->size)
 *        XXX: we can assert() that we don't overtake buf->head. That
 *        shouldn't be possible if we aren't enabling perf's
 *        overwriting/flight recorder mode.
 *	  XXX: Note: we do this after checking for new records so we
 *	  don't have to worry about the corner case of eating more
 *	  than we previously knew about.
 *
 * 4) Set perf's tail to perf's head (i.e. consume everything so that
 *    perf won't block when wrapping around and overwriting old
 *    samples.)
 *
 * XXX: Note: if tracing and using this structure to track headers
 * then when you want to process all the collected data, it's
 * necessary to stop/disable the event before consuming data from the
 * perf circular buffer due to how we manage the tail pointer (there's
 * nothing stopping perf from overwriting all the current data)
 */
struct gputop_perf_header_buf
{
    uint32_t *offsets;
    uint32_t len;
    uint32_t head;
    uint32_t tail;
    uint32_t last_perf_head;
    bool full; /* Set when we first wrap. */
};

enum gputop_perf_stream_type {
    GPUTOP_STREAM_PERF,
    GPUTOP_STREAM_I915_PERF,
};

struct gputop_perf_stream
{
    int ref_count;

    struct gputop_perf_query *query;
    bool overwrite;

    enum gputop_perf_stream_type type;

    union {
	/* i915 perf event */
	struct {
	    int buf_sizes;
	    uint8_t *bufs[2];
	    int buf_idx;
	    uint8_t *last;
	} oa;
	/* linux perf event */
	struct {
	    /* The mmaped circular buffer for collecting samples from perf */
	    struct perf_event_mmap_page *mmap_page;
	    uint8_t *buffer;
	    size_t buffer_size;

	    struct gputop_perf_header_buf header_buf;
	} perf;
    };

    int fd;
    uv_poll_t fd_poll;
    uv_timer_t fd_timer;
    void (*ready_cb)(struct gputop_perf_stream *);

// fields used for fake data:
    uint64_t start_time;  // stream opening time
    uint32_t gen_so_far; // amount of reports generated since stream opening
    uint32_t prev_clocks; // the previous value of clock ticks
    uint32_t period; // the period in nanoseconds calculated from exponent
    uint32_t prev_timestamp; // the previous timestamp value

    /* XXX: reserved for whoever opens the stream */
    struct {
	uint32_t id;
	gputop_list_t link;
	void *data;
	void (*destroy_cb)(struct gputop_perf_stream *stream);
	bool flushing;
    } user;
};
#endif

bool gputop_add_ctx_handle(int ctx_fd, uint32_t ctx_id);
bool gputop_remove_ctx_handle(uint32_t ctx_id);

extern struct gputop_devinfo gputop_devinfo;
extern struct gputop_perf_query *gputop_current_perf_query;
extern struct gputop_perf_stream *gputop_current_perf_stream;

bool gputop_enumerate_queries_via_sysfs(void);
bool gputop_perf_initialize(void);
void gputop_perf_free(void);

bool gputop_i915_perf_oa_overview_open(struct gputop_perf_query *query,
                                       bool enable_per_ctx);
void gputop_i915_perf_oa_overview_close(void);

int fake_read(struct gputop_perf_stream *stream, uint8_t *buf, int buf_length);

void gputop_perf_accumulator_clear(struct gputop_perf_stream *stream);
void gputop_perf_accumulate(struct gputop_perf_stream *stream,
			    const uint8_t *report0,
			    const uint8_t *report1);

void gputop_perf_read_samples(struct gputop_perf_stream *stream);
void gputop_i915_perf_print_records(struct gputop_perf_stream *stream,
				    uint8_t *buf,
				    int len);

bool gputop_i915_perf_oa_trace_open(struct gputop_perf_query *query,
                                    bool enable_per_ctx);
void gputop_i915_perf_oa_trace_close(void);

extern struct gputop_hash_table *queries;
extern struct array *perf_oa_supported_query_guids;
extern int gputop_perf_trace_buffer_size;
extern uint8_t *gputop_perf_trace_buffer;
extern bool gputop_perf_trace_empty;
extern bool gputop_perf_trace_full;
extern uint8_t *gputop_perf_trace_head;
extern int gputop_perf_n_samples;
extern bool gputop_fake_mode;

uint64_t read_report_timestamp(const uint32_t *report);
uint64_t read_uint64_oa_counter(const struct gputop_perf_query *query,
				const struct gputop_perf_query_counter *counter,
				uint64_t *accumulator);
uint32_t read_uint32_oa_counter(const struct gputop_perf_query *query,
				const struct gputop_perf_query_counter *counter,
				uint64_t *accumulator);
bool read_bool_oa_counter(const struct gputop_perf_query *query,
			  const struct gputop_perf_query_counter *counter,
			  uint64_t *accumulator);
double read_double_oa_counter(const struct gputop_perf_query *query,
			      const struct gputop_perf_query_counter *counter,
			      uint64_t *accumulator);
float read_float_oa_counter(const struct gputop_perf_query *query,
			    const struct gputop_perf_query_counter *counter,
			    uint64_t *accumulator);

#ifndef EMSCRIPTEN
struct gputop_perf_stream *
gputop_open_i915_perf_oa_query(struct gputop_perf_query *query,
			       int period_exponent,
			       size_t perf_buffer_size,
			       void (*ready_cb)(struct gputop_perf_stream *),
			       bool overwrite,
			       char **error);
struct gputop_perf_stream *
gputop_perf_open_trace(int pid,
		       int cpu,
		       const char *system,
		       const char *event,
		       size_t trace_struct_size,
		       size_t perf_buffer_size,
		       void (*ready_cb)(uv_poll_t *poll, int status, int events),
		       bool overwrite,
		       char **error);

struct gputop_perf_stream *
gputop_perf_open_generic_counter(int pid,
				 int cpu,
				 uint64_t type,
				 uint64_t config,
				 size_t perf_buffer_size,
				 void (*ready_cb)(uv_poll_t *poll, int status, int events),
				 bool overwrite,
				 char **error);

bool gputop_stream_data_pending(struct gputop_perf_stream *stream);

void gputop_perf_update_header_offsets(struct gputop_perf_stream *stream);

void gputop_perf_stream_ref(struct gputop_perf_stream *stream);
void gputop_perf_stream_unref(struct gputop_perf_stream *stream);
#endif

#endif /* _GPUTOP_PERF_H_ */
