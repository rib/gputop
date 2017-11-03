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
};

enum gputop_accumulator_flags {
    GPUTOP_ACCUMULATOR_CTX_SW_TO_SEEN   = 1,
    GPUTOP_ACCUMULATOR_CTX_SW_FROM_SEEN = 2,
    GPUTOP_ACCUMULATOR_CTX_TIMER_SEEN   = 4,
};

struct gputop_cc_oa_accumulator
{
    const struct gputop_devinfo *devinfo;
    const struct gputop_metric_set *metric_set;

    uint64_t aggregation_period;
    bool enable_ctx_switch_events;

    uint64_t first_timestamp;
    uint64_t last_timestamp;
#define MAX_RAW_OA_COUNTERS 62
    uint64_t deltas[MAX_RAW_OA_COUNTERS];
    enum gputop_accumulator_flags flags;
    struct gputop_u32_clock clock;

    /* Can be used for binding structure into JavaScript, e.g. to
     * associate a corresponding v8::Object... */
    void *js_priv;
};

void gputop_u32_clock_init(struct gputop_u32_clock *clock,
                           const struct gputop_devinfo *devinfo,
                           uint32_t u32_start);
uint64_t gputop_u32_clock_get_time(struct gputop_u32_clock *clock);
void gputop_u32_clock_progress(struct gputop_u32_clock *clock,
                               uint32_t u32_timestamp);

void gputop_cc_oa_accumulator_init(struct gputop_cc_oa_accumulator *accumulator,
                                   const struct gputop_devinfo *devinfo,
                                   const struct gputop_metric_set *metric_set,
                                   bool enable_ctx_switch_events,
                                   int aggregation_period);
void gputop_cc_oa_accumulator_clear(struct gputop_cc_oa_accumulator *accumulator);
bool gputop_cc_oa_accumulate_reports(struct gputop_cc_oa_accumulator *accumulator,
                                     const uint8_t *report0,
                                     const uint8_t *report1);

static inline uint32_t
gputop_cc_oa_accumulator_get_ctx_id(struct gputop_cc_oa_accumulator *accumulator,
                                    const uint8_t *report)
{
    if (accumulator->devinfo->gen < 8)
        return 0;
    return ((uint32_t *) report)[2];
}

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

static const char *
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

#ifdef __cplusplus
}
#endif
