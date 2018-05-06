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

#include "gputop-oa-metrics.h"


#ifdef __cplusplus
extern "C" {
#endif

#define GPUTOP_OA_INVALID_CTX_ID (0xffffffff)

/* NB: the timestamps written by the OA unit are 32 bits counting in ~80
 * nanosecond units (at least on Haswell) so it wraps every ~ 6 minutes, this
 * gputop_u32_clock api accumulates a 64bit monotonic timestamp in nanoseconds
 */
struct gputop_u32_clock {
    const struct gputop_devinfo *devinfo;
    uint64_t start;
    uint64_t timestamp;
    uint32_t last_u32;
    uint64_t clock_count;
};

struct gputop_cc_oa_accumulator
{
    const struct gputop_devinfo *devinfo;
    const struct gputop_metric_set *metric_set;

    uint64_t aggregation_period;

    uint64_t first_timestamp;
    uint64_t last_timestamp;
#define MAX_RAW_OA_COUNTERS 62
    uint64_t deltas[MAX_RAW_OA_COUNTERS];
    struct gputop_u32_clock clock;
};

void gputop_cc_oa_accumulator_init(struct gputop_cc_oa_accumulator *accumulator,
                                   const struct gputop_devinfo *devinfo,
                                   const struct gputop_metric_set *metric_set,
                                   int aggregation_period,
                                   const uint8_t *first_report);
void gputop_cc_oa_accumulator_clear(struct gputop_cc_oa_accumulator *accumulator);
bool gputop_cc_oa_accumulate_reports(struct gputop_cc_oa_accumulator *accumulator,
                                     const uint8_t *report0,
                                     const uint8_t *report1);

static inline uint64_t
gputop_time_scale_timebase(const struct gputop_devinfo *devinfo, uint64_t ns_time)
{
    return (ns_time * devinfo->timestamp_frequency) / 1000000000ULL;
}

static inline uint64_t
gputop_timebase_scale_ns(const struct gputop_devinfo *devinfo, uint64_t u32_time)
{
    return (u32_time * 1000000000ULL) / devinfo->timestamp_frequency;
}

static inline uint64_t
gputop_oa_exponent_to_period_ns(const struct gputop_devinfo *devinfo, uint32_t exponent)
{
    return ((2ULL << exponent) * 1000000000ULL) / devinfo->timestamp_frequency;
}

uint32_t gputop_time_to_oa_exponent(struct gputop_devinfo *devinfo, uint64_t period_ns);

static inline bool
gputop_cc_oa_report_ctx_is_valid(const struct gputop_devinfo *devinfo,
                                 const uint8_t *_report)
{
    const uint32_t *report = (const uint32_t *) _report;

    if (devinfo->gen < 8) {
        return false; /* TODO */
    } else if (devinfo->gen == 8) {
        return report[0] & (1ul << 25);
    } else if (devinfo->gen > 8) {
        return report[0] & (1ul << 16);
    }

    return false;
}

static inline uint32_t
gputop_cc_oa_report_get_ctx_id(const struct gputop_devinfo *devinfo,
                               const uint8_t *report)
{
    if (!gputop_cc_oa_report_ctx_is_valid(devinfo, report))
        return GPUTOP_OA_INVALID_CTX_ID;
    return ((const uint32_t *) report)[2];
}

static inline uint64_t
gputop_cc_oa_report_get_timestamp(const uint8_t *report)
{
    return ((uint32_t *)report)[1];
}

static inline const char *
gputop_cc_oa_report_get_reason(const struct gputop_devinfo *devinfo,
                               const uint8_t *report)
{
    const uint32_t *report32 = (const uint32_t *) report;
    uint32_t reason;

    if (devinfo->gen < 8)
        return "unknown (gen7)";

    reason = ((report32[0] >> 19) & 0x3f);
    if (reason & (1<<0))
        return "timer";
    if (reason & (1<<1))
        return "internal trigger 1";
    if (reason & (1<<2))
        return "internal trigger 2";
    if (reason & (1<<3))
        return "context switch";
    if (reason & (1<<4))
        return "GO 1->0 transition (enter RC6)";
    if (reason & (1<<5))
        return "[un]slice clock ratio change";
    return "unknown";
}

struct gputop_i915_perf_configuration {
    bool oa_reports;
    bool cpu_timestamps;
    bool gpu_timestamps;
};

enum gputop_i915_perf_field {
    GPUTOP_I915_PERF_FIELD_OA_REPORT,
    GPUTOP_I915_PERF_FIELD_CPU_TIMESTAMP,
    GPUTOP_I915_PERF_FIELD_GPU_TIMESTAMP,
};

static inline uint64_t
gputop_i915_perf_record_max_size(const struct gputop_i915_perf_configuration *config)
{
    uint64_t size = sizeof(struct drm_i915_perf_record_header);

    if (config->oa_reports)
        size += 256ULL; /* Default OA report size */
    if (config->gpu_timestamps)
        size += sizeof(uint64_t);
    if (config->cpu_timestamps)
        size += sizeof(uint64_t);

    return size;
}

static inline const void *
gputop_i915_perf_record_field(const struct gputop_i915_perf_configuration *config,
                              const struct drm_i915_perf_record_header *header,
                              enum gputop_i915_perf_field field)
{
    const uint8_t *ptr = (const uint8_t *) (header + 1);

    if (config->gpu_timestamps) {
        if (field == GPUTOP_I915_PERF_FIELD_GPU_TIMESTAMP)
            return ptr;
        ptr += sizeof(uint64_t);
    }

    if (config->cpu_timestamps) {
        if (field == GPUTOP_I915_PERF_FIELD_CPU_TIMESTAMP)
            return ptr;
        ptr += sizeof(uint64_t);
    }

    if (config->oa_reports) {
        if (field == GPUTOP_I915_PERF_FIELD_OA_REPORT)
            return ptr;
    }

    return NULL;
}

static inline uint32_t
gputop_i915_perf_record_timestamp(const struct gputop_i915_perf_configuration *config,
                                  const struct drm_i915_perf_record_header *header)
{
    if (header->type != DRM_I915_PERF_RECORD_SAMPLE)
        return 0;

    return gputop_cc_oa_report_get_timestamp(
        (const uint8_t *)
        gputop_i915_perf_record_field(config, header,
                                      GPUTOP_I915_PERF_FIELD_OA_REPORT));
}

static inline const char *
gputop_i915_perf_record_reason(const struct gputop_i915_perf_configuration *config,
                               const struct gputop_devinfo *devinfo,
                               const struct drm_i915_perf_record_header *header)
{
    switch (header->type) {
    case DRM_I915_PERF_RECORD_SAMPLE:
        return gputop_cc_oa_report_get_reason(devinfo, (const uint8_t *)
                                              gputop_i915_perf_record_field(config, header,
                                                                            GPUTOP_I915_PERF_FIELD_OA_REPORT));
    case DRM_I915_PERF_RECORD_OA_REPORT_LOST:
        return "report lost";
    case DRM_I915_PERF_RECORD_OA_BUFFER_LOST:
        return "buffer lost";
    default:
        return "unknown/error";
    }
}

#ifdef __cplusplus
}
#endif
