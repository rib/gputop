/*
 * GPU Top
 *
 * Copyright (C) 2015-2016 Intel Corporation
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

#include <stdint.h>
#include <stdbool.h>
#include <assert.h>
#include <inttypes.h>

#include "i915_oa_drm.h"

#include "gputop-list.h"


struct gputop_devinfo {
    uint32_t devid;
    uint32_t gen;
    uint64_t timestamp_frequency;
    uint64_t n_eus;
    uint64_t n_eu_slices;
    uint64_t n_eu_sub_slices;
    uint64_t eu_threads_count;
    uint64_t subslice_mask;
    uint64_t slice_mask;
    uint64_t gt_min_freq;
    uint64_t gt_max_freq;
};

/* NB: the timestamps written by the OA unit are 32 bits counting in ~80
 * nanosecond units (at least on Haswell) so it wraps every ~ 6 minutes, this
 * gputop_u32_clock api accumulates a 64bit monotonic timestamp in nanoseconds
 */
struct gputop_u32_clock {
    bool initialized;
    uint64_t start;
    uint64_t timestamp;
    uint32_t last_u32;
};

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
#define OAREPORT_REASON_TIMER          (1<<0)
#define OAREPORT_REASON_CTX_SWITCH     (1<<3)

struct gputop_perf_query;
struct gputop_perf_query_counter
{
   const char *name;
   const char *symbol_name;
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

enum gputop_accumulator_flags {
    GPUTOP_ACCUMULATOR_CTX_SW_TO_SEEN   = 1,
    GPUTOP_ACCUMULATOR_CTX_SW_FROM_SEEN = 2,
    GPUTOP_ACCUMULATOR_CTX_TIMER_SEEN   = 4,
};

struct gputop_perf_query
{
    const char *name;
    const char *symbol_name;
    const char *guid;
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

    /* TODO: factor out into a separate structure */
    uint64_t accumulator_period_ns;
    uint64_t accumulator_first_timestamp;
    uint64_t accumulator_last_timestamp;
#define MAX_RAW_OA_COUNTERS 62
    uint64_t accumulator[MAX_RAW_OA_COUNTERS];
    enum gputop_accumulator_flags accumulator_flags;
    struct gputop_u32_clock accumulator_clock;
    uint32_t accumulator_last_ctx_id;

    gputop_list_t link;
};

extern struct gputop_devinfo gputop_devinfo;

void gputop_u32_clock_init(struct gputop_u32_clock *clock, uint32_t u32_start);
uint64_t gputop_u32_clock_get_time(struct gputop_u32_clock *clock);
void gputop_u32_clock_progress(struct gputop_u32_clock *clock,
                               uint32_t u32_timestamp);

void gputop_oa_accumulator_clear(struct gputop_perf_query *query);
bool gputop_oa_accumulate_reports(struct gputop_perf_query *query,
                                  const uint8_t *report0,
                                  const uint8_t *report1,
                                  bool per_ctx_mode);
