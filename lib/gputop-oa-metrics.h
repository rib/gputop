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

#include <i915_drm.h>

#include "dev/gen_device_info.h"
#include "util/hash_table.h"
#include "util/list.h"

#ifdef __cplusplus
extern "C" {
#endif

struct gputop_devtopology {
    uint32_t max_slices;
    uint32_t max_subslices;
    uint32_t max_eus_per_subslice;
    uint32_t n_threads_per_eu;

    /* Max values should be enough for a while. */
    uint8_t slices_mask[4];
    uint8_t subslices_mask[16];
    uint8_t eus_mask[256];

    uint32_t engines[5];
};

struct gputop_devinfo {
    char devname[20];
    char prettyname[100];

    uint32_t devid;
    uint32_t gen;
    uint32_t revision;
    uint64_t timestamp_frequency;
    uint64_t gt_min_freq;
    uint64_t gt_max_freq;

    /* Always false for gputop, we don't have the additional snapshots of
     * register values, only the OA reports.
     */
    bool query_mode;

    bool has_dynamic_configs;

    struct gputop_devtopology topology;

    /* The following fields are prepared for equations from the XML files.
     * Their values are build up from the topology fields.
     */
    uint64_t n_eus;
    uint64_t n_eu_slices;
    uint64_t n_eu_sub_slices;
    uint64_t subslice_mask;
    uint64_t slice_mask;
    uint64_t eu_threads_count;
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

typedef enum {
    /* size */
    GPUTOP_PERFQUERY_COUNTER_UNITS_BYTES,

    /* frequency */
    GPUTOP_PERFQUERY_COUNTER_UNITS_HZ,

    /* time */
    GPUTOP_PERFQUERY_COUNTER_UNITS_NS,
    GPUTOP_PERFQUERY_COUNTER_UNITS_US,

    /**/
    GPUTOP_PERFQUERY_COUNTER_UNITS_PIXELS,
    GPUTOP_PERFQUERY_COUNTER_UNITS_TEXELS,
    GPUTOP_PERFQUERY_COUNTER_UNITS_THREADS,
    GPUTOP_PERFQUERY_COUNTER_UNITS_PERCENT,

    /* events */
    GPUTOP_PERFQUERY_COUNTER_UNITS_MESSAGES,
    GPUTOP_PERFQUERY_COUNTER_UNITS_NUMBER,
    GPUTOP_PERFQUERY_COUNTER_UNITS_CYCLES,
    GPUTOP_PERFQUERY_COUNTER_UNITS_EVENTS,
    GPUTOP_PERFQUERY_COUNTER_UNITS_UTILIZATION,

    /**/
    GPUTOP_PERFQUERY_COUNTER_UNITS_EU_SENDS_TO_L3_CACHE_LINES,
    GPUTOP_PERFQUERY_COUNTER_UNITS_EU_ATOMIC_REQUESTS_TO_L3_CACHE_LINES,
    GPUTOP_PERFQUERY_COUNTER_UNITS_EU_REQUESTS_TO_L3_CACHE_LINES,
    GPUTOP_PERFQUERY_COUNTER_UNITS_EU_BYTES_PER_L3_CACHE_LINE,

    GPUTOP_PERFQUERY_COUNTER_UNITS_MAX
} gputop_counter_units_t;

#define OAREPORT_REASON_MASK           0x3f
#define OAREPORT_REASON_SHIFT          19
#define OAREPORT_REASON_TIMER          (1<<0)
#define OAREPORT_REASON_CTX_SWITCH     (1<<3)

struct gputop_metric_set;
struct gputop_metric_set_counter {
    const struct gputop_metric_set *metric_set;
    const char *name;
    const char *symbol_name;
    const char *desc;
    gputop_counter_type_t type;
    gputop_counter_data_type_t data_type;
    gputop_counter_units_t units;
    union {
        uint64_t (*max_uint64)(const struct gputop_devinfo *devinfo,
                               const struct gputop_metric_set *metric_set,
                               uint64_t *deltas);
        double (*max_float)(const struct gputop_devinfo *devinfo,
                            const struct gputop_metric_set *metric_set,
                            uint64_t *deltas);
    };

    union {
        uint64_t (*oa_counter_read_uint64)(const struct gputop_devinfo *devinfo,
                                           const struct gputop_metric_set *metric_set,
                                           uint64_t *deltas);
        double (*oa_counter_read_float)(const struct gputop_devinfo *devinfo,
                                        const struct gputop_metric_set *metric_set,
                                        uint64_t *deltas);
    };

    struct list_head link; /* list from gputop_counter_group.counters */
};

struct gputop_register_prog {
    uint32_t reg;
    uint32_t val;
};

struct gputop_metric_set {
    const char *name;
    const char *symbol_name;
    const char *hw_config_guid;
    struct gputop_metric_set_counter *counters;
    int n_counters;

    uint64_t perf_oa_metrics_set;
    int perf_oa_format;
    int perf_raw_size;

    /* For indexing into accumulator->deltas[] ... */
    int gpu_time_offset;
    int gpu_clock_offset;
    int a_offset;
    int b_offset;
    int c_offset;

    struct gputop_register_prog *b_counter_regs;
    uint32_t n_b_counter_regs;

    struct gputop_register_prog *mux_regs;
    uint32_t n_mux_regs;

    struct gputop_register_prog *flex_regs;
    uint32_t n_flex_regs;

    struct list_head link;
};

struct gputop_counter_group {
    const char *name;

    struct list_head counters;
    struct list_head groups;

    struct list_head link;  /* list from gputop_counter_group.groups */
};

struct gputop_gen {
    const char *name;

    struct gputop_counter_group *root_group;

    struct list_head metric_sets;
    struct hash_table *metric_sets_map;
};

/* Free with ralloc_free() */
struct gputop_gen *gputop_gen_for_devinfo(const struct gen_device_info *devinfo);

struct gputop_gen *gputop_gen_new(void);

void gputop_gen_add_counter(struct gputop_gen *gen,
                            struct gputop_metric_set_counter *counter,
                            const char *group);

void gputop_gen_add_metric_set(struct gputop_gen *gen,
                               struct gputop_metric_set *metric_set);

#ifdef __cplusplus
}
#endif
