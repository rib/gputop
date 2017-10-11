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

#pragma once

#include <stdbool.h>

#include <uv.h>
#include <time.h>

#include "util/list.h"

#include "gputop-hash-table.h"
#include "gputop-oa-metrics.h"

uint64_t get_time(void);

struct ctx_handle {
    struct list_head link;

    uint32_t id;
    int fd;
};

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
    GPUTOP_STREAM_CPU,
};

struct gputop_perf_stream
{
    int ref_count;

    struct gputop_metric_set *metric_set;
    bool overwrite;

    enum gputop_perf_stream_type type;

    union {
        /* i915 perf event */
        struct {
            int buf_sizes;
            uint8_t *bufs[2];
            uint8_t *last;
            int last_buf_idx;
        } oa;
        /* linux perf event */
        struct {
            /* The mmaped circular buffer for collecting samples from perf */
            struct perf_event_mmap_page *mmap_page;
            uint8_t *buffer;
            size_t buffer_size;

            struct gputop_perf_header_buf header_buf;
        } perf;
        /* /proc/stat */
        struct {
            uv_timer_t sample_timer;

            struct cpu_stat *stats_buf;
            int stats_buf_len; /* N cpu_stat structures (multiple of n_cpus) */
            int stats_buf_pos;
            bool stats_buf_full;
        } cpu;
    };

    int fd;
    uv_poll_t fd_poll;
    uv_timer_t fd_timer;
    void (*ready_cb)(struct gputop_perf_stream *);

    bool live_updates;

    int n_closing_uv_handles;
    void (*on_close_cb)(struct gputop_perf_stream *stream);
    bool pending_close;
    bool closed;
    bool per_ctx_mode;

// fields used for fake data:
    uint64_t start_time;  // stream opening time
    uint32_t gen_so_far; // amount of reports generated since stream opening
    uint32_t prev_clocks; // the previous value of clock ticks
    uint32_t period; // the period in nanoseconds calculated from exponent
    uint32_t prev_timestamp; // the previous timestamp value

    /* XXX: reserved for whoever opens the stream */
    struct {
        uint32_t id;
        struct list_head link;
        void *data;
        void (*destroy_cb)(struct gputop_perf_stream *stream);
        bool flushing;
    } user;
};

/* E.g. for tracing vs rolling view */
struct perf_oa_user {
    void (*sample)(struct gputop_perf_stream *stream,
                   uint8_t *start, uint8_t *end);
};

extern struct perf_oa_user *gputop_perf_current_user;

bool gputop_add_ctx_handle(int ctx_fd, uint32_t ctx_id);
bool gputop_remove_ctx_handle(uint32_t ctx_id);
struct ctx_handle *get_first_available_ctx(char **error);

bool gputop_perf_initialize(void);
void gputop_perf_free(void);

extern struct gputop_hash_table *metrics;
extern struct array *gputop_perf_oa_supported_metric_set_uuids;
extern int gputop_perf_trace_buffer_size;
extern uint8_t *gputop_perf_trace_buffer;
extern bool gputop_perf_trace_empty;
extern bool gputop_perf_trace_full;
extern uint8_t *gputop_perf_trace_head;
extern int gputop_perf_n_samples;
extern bool gputop_fake_mode;

struct gputop_perf_stream *
gputop_open_i915_perf_oa_stream(struct gputop_metric_set *metric_set,
                                int period_exponent,
                                struct ctx_handle *ctx,
                                void (*ready_cb)(struct gputop_perf_stream *),
                                bool overwrite,
                                char **error);
struct gputop_perf_stream *
gputop_perf_open_tracepoint(int pid,
                            int cpu,
                            uint64_t id,
                            size_t trace_struct_size,
                            size_t perf_buffer_size,
                            void (*ready_cb)(struct gputop_perf_stream *),
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

struct gputop_perf_stream *
gputop_perf_open_cpu_stats(bool overwrite, uint64_t sample_period_ms);

bool gputop_stream_data_pending(struct gputop_perf_stream *stream);

void gputop_perf_update_header_offsets(struct gputop_perf_stream *stream);

int gputop_perf_fake_read(struct gputop_perf_stream *stream,
                          uint8_t *buf, int buf_length);

void gputop_perf_read_samples(struct gputop_perf_stream *stream);

void gputop_i915_perf_print_records(struct gputop_perf_stream *stream,
                                    uint8_t *buf,
                                    int len);

void gputop_perf_stream_close(struct gputop_perf_stream *stream,
                              void (*on_close_cb)(struct gputop_perf_stream *stream));
void gputop_perf_stream_ref(struct gputop_perf_stream *stream);
void gputop_perf_stream_unref(struct gputop_perf_stream *stream);
