/*
 * GPU Top
 *
 * Copyright (C) 2017 Intel Corporation
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

#ifndef __GPUTOP_CLIENT_CONTEXT_H__
#define __GPUTOP_CLIENT_CONTEXT_H__

#include <stdbool.h>
#include <stdint.h>

#include "util/hash_table.h"
#include "util/list.h"

#include "gputop-network.h"
#include "gputop-oa-counters.h"
#include "gputop-oa-metrics.h"

#include "gputop.pb-c.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A chunk of data coming from the i915 perf driver (contains a sequence of
 * struct drm_i915_perf_record_header fields).
 */
struct gputop_i915_perf_chunk {
    struct list_head link;

    uint32_t refcount;

    uint32_t length;
    uint8_t data[];
};

struct gputop_accumulated_samples;
struct gputop_process_info;

struct gputop_hw_context {
    char name[300];
    uint32_t hw_id;

    uint32_t n_samples;

    uint64_t time_spent;

    struct list_head link;

    struct gputop_process_info *process;

    struct gputop_accumulated_samples *current_graph_samples;
    struct list_head graphs;
    uint32_t n_graphs;

    /* UI state */
    uint64_t visible_time_spent;
    uint64_t visible_time;

    uint32_t timeline_row;
    bool selected;
};

struct gputop_accumulated_samples {
    struct list_head link;
    struct gputop_hw_context *context;

    /* Correlated in CPU clock if available or OA timestamp scaled into
     * nanoseconds.
     */
    uint64_t timestamp_start;
    uint64_t timestamp_end;

    struct {
        struct gputop_i915_perf_chunk *chunk;
        const struct drm_i915_perf_record_header *header;
    } start_report, end_report;

    struct gputop_cc_oa_accumulator accumulator;
};

struct gputop_cpu_stat {
    struct list_head link;

    Gputop__Message *stat;
};

struct gputop_stream {
    struct list_head link;

    int id;
    float fill;
};

struct gputop_perf_event {
    struct list_head link; /* global list (gputop_client_context.perf_events) */

    char name[128];
    uint32_t event_id;

    struct list_head streams; /* list of gputop_perf_event_stream */
};

struct gputop_perf_event_data {
    struct {
        uint32_t type;
        uint16_t misc;
        uint16_t size;
    } header;
    uint64_t time;
    uint64_t value;
};

struct gputop_perf_event_stream {
    struct gputop_stream base;

    int cpu;
    struct gputop_perf_event *event;

    struct gputop_perf_event_data *data;

    struct list_head link; /* list of streams (gputop_perf_tracepoint.streams) */
};

struct gputop_perf_data_tracepoint {
    struct {
        uint32_t type;
        uint16_t misc;
        uint16_t size;
    } header;
    uint64_t time;
    uint32_t data_size;
    uint8_t  data[];
};

struct gputop_perf_tracepoint {
    struct list_head link; /* global list (gputop_client_context.perf_tracepoints)*/

    struct list_head data; /* list of gputop_perf_tracepoint_data */

    char name[128];
    uint32_t event_id;
    char *format;
    int idx;

    char uuid[20];

    struct {
        bool is_signed;
        int offset;
        int size;
        char name[80];
    } fields[20];
    int n_fields;

    int process_field;
    int hw_id_field;

    struct list_head streams; /* list of gputop_perf_tracepoint_stream */
};

struct gputop_perf_tracepoint_stream {
    struct gputop_stream base;

    int cpu;
    struct gputop_perf_tracepoint *tp;

    struct list_head link; /* list of streams (gputop_perf_tracepoint.streams) */
};

struct gputop_perf_tracepoint_data {
    struct list_head link; /* global list (gputop_client_context.perf_tracepoints_data) */
    struct list_head tp_link; /* per tracepoint list (gputop_perf_tracepoint.data) */

    struct gputop_perf_tracepoint *tp;

    int cpu;
    struct gputop_perf_data_tracepoint data;
};

struct gputop_process_info {
    char cmd[256];
    char cmd_line[1024];
    uint32_t pid;
};

struct gputop_client_context;

typedef void (*gputop_accumulate_cb)(struct gputop_client_context *ctx,
                                     struct gputop_hw_context *context);

struct gputop_client_context {
    gputop_connection_t *connection;

    struct list_head streams;

    bool is_sampling;

    /**/
    Gputop__Message *features;
    Gputop__Message *tracepoint_info;

    struct gputop_gen *gen_metrics;
    struct gputop_devinfo devinfo;

    int selected_uuid;

    /**/
    struct list_head cpu_stats;
    int n_cpu_stats;
    float cpu_stats_visible_timeline_s; /* RW */
    int cpu_stats_sampling_period_ms;
    struct gputop_stream cpu_stats_stream;

    /**/
    struct gputop_i915_perf_configuration i915_perf_config;
    const struct gputop_metric_set *metric_set;
    struct gputop_i915_perf_chunk *last_chunk;
    const struct drm_i915_perf_record_header *last_header;
    struct gputop_stream oa_stream;

    struct list_head free_samples;
    struct list_head i915_perf_chunks;

    uint64_t last_oa_timestamp;

    /**/
    struct gputop_accumulated_samples *current_graph_samples;
    struct list_head graphs;
    int n_graphs;
    float oa_visible_timeline_s; /* RW */
    uint64_t oa_aggregation_period_ns; /* RW (when not sampling) */
    uint64_t oa_sampling_period_ns; /* RW (when not sampling), always <= oa_aggregation_period_ns */

    gputop_accumulate_cb accumulate_cb; /* RW */

    /**/
    struct gputop_accumulated_samples *current_timeline_samples;
    struct list_head timelines;
    int n_timelines;
    uint32_t last_hw_id;

    struct hash_table *hw_contexts_table;
    struct list_head hw_contexts;

    /**/
    struct hash_table *perf_tracepoints_uuid_table;
    struct hash_table *perf_tracepoints_name_table;
    struct hash_table *perf_tracepoints_stream_table;
    struct list_head perf_tracepoints;
    struct list_head perf_tracepoints_data;

    /**/
    struct hash_table *perf_events_stream_table;
    struct list_head perf_events;

    /**/
    struct hash_table *pid_to_process_table;
    struct hash_table *hw_id_to_process_table;

    /**/
    struct {
        int level;
        char *msg;
    } messages[100];
    int start_message;
    int n_messages;

    /**/
    uint32_t stream_id;
};

int gputop_client_pretty_print_value(gputop_counter_units_t unit,
                                     double value, char *buffer, size_t length);

double gputop_client_context_max_value(struct gputop_client_context *ctx,
                                       const struct gputop_metric_set_counter *counter,
                                       uint64_t ns_time);
int gputop_client_context_pretty_print_max(struct gputop_client_context *ctx,
                                           const struct gputop_metric_set_counter *counter,
                                           uint64_t ns_time, char *buffer, size_t length);

void gputop_client_context_init(struct gputop_client_context *ctx);
void gputop_client_context_reset(struct gputop_client_context *ctx,
                                 gputop_connection_t *connection);

void gputop_client_context_handle_data(struct gputop_client_context *ctx,
                                       const void *payload, size_t payload_len);

void gputop_client_context_update_cpu_stream(struct gputop_client_context *ctx,
                                             int sampling_period_ms);

void gputop_client_context_stop_sampling(struct gputop_client_context *ctx);
void gputop_client_context_start_sampling(struct gputop_client_context *ctx);

void gputop_client_context_clear_logs(struct gputop_client_context *ctx);

const struct gputop_metric_set *
gputop_client_context_uuid_to_metric_set(struct gputop_client_context *ctx,
                                         const char *uuid);
const struct gputop_metric_set *
gputop_client_context_symbol_to_metric_set(struct gputop_client_context *ctx,
                                           const char *symbol_name);

struct gputop_perf_tracepoint *
gputop_client_context_add_tracepoint(struct gputop_client_context *ctx,
                                     const char *name);
void gputop_client_context_remove_tracepoint(struct gputop_client_context *ctx,
                                             struct gputop_perf_tracepoint *tp);

void gputop_client_context_print_tracepoint_data(struct gputop_client_context *ctx,
                                                 char *buf, size_t len,
                                                 struct gputop_perf_tracepoint_data *data,
                                                 bool include_name);

double gputop_client_context_read_counter_value(struct gputop_client_context *ctx,
                                                struct gputop_accumulated_samples *sample,
                                                const struct gputop_metric_set_counter *counter);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* __GPUTOP_CLIENT_CONTEXT_H__ */
