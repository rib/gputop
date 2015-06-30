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
#include <libdrm/i915_drm.h>
#include <uv.h>
#else

#define I915_OA_FORMAT_A13_HSW		0
#define I915_OA_FORMAT_A29_HSW		1
#define I915_OA_FORMAT_A13_B8_C8_HSW	2
#define I915_OA_FORMAT_B4_C8_HSW	4
#define I915_OA_FORMAT_A45_B8_C8_HSW	5
#define I915_OA_FORMAT_B4_C8_A16_HSW	6
#define I915_OA_FORMAT_C4_B8_HSW	7

#define I915_OA_FORMAT_A12_BDW		0
#define I915_OA_FORMAT_A12_B8_C8_BDW	2
#define I915_OA_FORMAT_A36_B8_C8_BDW	5
#define I915_OA_FORMAT_C4_B8_BDW	7

#define I915_OA_METRICS_SET_3D			1
#define I915_OA_METRICS_SET_COMPUTE		2
#define I915_OA_METRICS_SET_COMPUTE_EXTENDED	3
#define I915_OA_METRICS_SET_MEMORY_READS	4
#define I915_OA_METRICS_SET_MEMORY_WRITES	5
#define I915_OA_METRICS_SET_SAMPLER_BALANCE	6
#define I915_OA_METRICS_SET_MAX			I915_OA_METRICS_SET_SAMPLER_BALANCE

#endif

#include "gputop-list.h"

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

struct gputop_devinfo {
    uint32_t devid;

    uint64_t n_eus;
    uint64_t n_eu_slices;
    uint64_t n_eu_sub_slices;
    uint64_t n_samplers;
};

struct gputop_perf_query;

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

#ifndef EMSCRIPTEN
struct gputop_perf_stream
{
    char *name;

    int fd;
    uv_poll_t fd_poll;

    /* The mmaped circular buffer for collecting samples from perf */
    struct perf_event_mmap_page *mmap_page;
    uint8_t *buffer;
    size_t buffer_size;

    char *error;

    gputop_list_t link;
};
#endif

struct gputop_perf_query
{
    const char *name;
    struct gputop_perf_query_counter *counters;
    int n_counters;

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

#ifndef EMSCRIPTEN
    struct gputop_perf_stream stream;
#endif
};

extern struct gputop_devinfo gputop_devinfo;
extern struct gputop_perf_query *gputop_current_perf_query;

typedef enum gputop_perf_query_type
{
    GPUTOP_PERF_QUERY_BASIC,
    GPUTOP_PERF_QUERY_3D_BASIC,
    GPUTOP_PERF_QUERY_COMPUTE_BASIC,
    GPUTOP_PERF_QUERY_COMPUTE_EXTENDED,
    GPUTOP_PERF_QUERY_MEMORY_READS,
    GPUTOP_PERF_QUERY_MEMORY_WRITES,
    GPUTOP_PERF_QUERY_SAMPLER_BALANCE,
} gputop_perf_query_type_t;


bool gputop_perf_initialize(void);

bool gputop_perf_overview_open(gputop_perf_query_type_t query_type);
void gputop_perf_overview_close(void);

void gputop_perf_accumulator_clear(struct gputop_perf_query *query);
void gputop_perf_accumulate(struct gputop_perf_query *query,
			    const uint8_t *report0,
			    const uint8_t *report1);

void gputop_perf_read_samples(struct gputop_perf_query *query);

bool gputop_perf_oa_trace_open(gputop_perf_query_type_t query_type);
void gputop_perf_oa_trace_close(void);

#define MAX_PERF_QUERIES 7
extern struct gputop_perf_query perf_queries[MAX_PERF_QUERIES];
extern int gputop_perf_trace_buffer_size;
extern uint8_t *gputop_perf_trace_buffer;
extern bool gputop_perf_trace_empty;
extern bool gputop_perf_trace_full;
extern uint8_t *gputop_perf_trace_head;
extern int gputop_perf_n_samples;

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
bool gputop_perf_open_i915_oa_query(struct gputop_perf_query *query,
				    int period_exponent,
				    size_t perf_buffer_size,
				    void (*ready_cb)(uv_poll_t *poll, int status, int events),
				    void *user_data);
void gputop_perf_close_i915_oa_query(struct gputop_perf_query *query);
#endif

#endif /* _GPUTOP_PERF_H_ */
