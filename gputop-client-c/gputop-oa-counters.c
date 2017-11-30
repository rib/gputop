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

#define _GNU_SOURCE

#ifdef EMSCRIPTEN
#include <emscripten.h>
#else
#define EMSCRIPTEN_KEEPALIVE
#endif

#include <string.h>

#include "gputop-oa-counters.h"

#ifdef GPUTOP_CLIENT
#include "gputop-client-c-runtime.h"
#define dbg gputop_cr_console_log
#else
#include "gputop-log.h"
#endif

void
gputop_u32_clock_init(struct gputop_u32_clock *clock,
                      const struct gputop_devinfo *devinfo,
                      uint32_t u32_start)
{
    clock->timestamp = clock->start = gputop_timebase_scale_ns(devinfo, u32_start);
    clock->last_u32 = u32_start;
    clock->devinfo = devinfo;
}

uint64_t
gputop_u32_clock_get_time(struct gputop_u32_clock *clock)
{
    return clock->timestamp;
}

void
gputop_u32_clock_progress(struct gputop_u32_clock *clock,
                          uint32_t u32_timestamp)
{
    uint32_t delta = u32_timestamp - clock->last_u32;

    clock->timestamp += gputop_timebase_scale_ns(clock->devinfo, delta);
    clock->last_u32 = u32_timestamp;
}

static void
accumulate_uint32(const uint32_t *report0,
                  const uint32_t *report1,
                  uint64_t *deltas)
{
   *deltas += (uint32_t)(*report1 - *report0);
}

static void
accumulate_uint40(int a_index,
                  const uint32_t *report0,
                  const uint32_t *report1,
                  uint64_t *deltas)
{
    const uint8_t *high_bytes0 = (uint8_t *)(report0 + 40);
    const uint8_t *high_bytes1 = (uint8_t *)(report1 + 40);
    uint64_t high0 = (uint64_t)(high_bytes0[a_index]) << 32;
    uint64_t high1 = (uint64_t)(high_bytes1[a_index]) << 32;
    uint64_t value0 = report0[a_index + 4] | high0;
    uint64_t value1 = report1[a_index + 4] | high1;
    uint64_t delta;

    if (value0 > value1)
       delta = (1ULL << 40) + value1 - value0;
    else
       delta = value1 - value0;

    *deltas += delta;
}

bool
gputop_cc_oa_accumulate_reports(struct gputop_cc_oa_accumulator *accumulator,
                                const uint8_t *report0,
                                const uint8_t *report1)
{
    const struct gputop_metric_set *metric_set = accumulator->metric_set;
    uint64_t *deltas = accumulator->deltas;
    const uint32_t *start = (const uint32_t *)report0;
    const uint32_t *end = (const uint32_t *)report1;
    int idx = 0;
    int i;

    assert(report0 != report1);

    /* technically a timestamp of zero is valid, but much more likely it
     * indicates a problem...
     */
    if (start[1] == 0 || end[1] == 0) {
        dbg("i915_oa: spurious report with timestamp of zero\n");
        return false;
    }

    if (!accumulator->clock.devinfo)
        gputop_u32_clock_init(&accumulator->clock, accumulator->devinfo, start[1]);

    switch (metric_set->perf_oa_format) {
    case I915_OA_FORMAT_A32u40_A4u32_B8_C8:

        accumulate_uint32(start + 1, end + 1, deltas + idx++); /* timestamp */
        accumulate_uint32(start + 3, end + 3, deltas + idx++); /* clock */

        /* 32x 40bit A counters... */
        for (i = 0; i < 32; i++)
            accumulate_uint40(i, start, end, deltas + idx++);

        /* 4x 32bit A counters... */
        for (i = 0; i < 4; i++)
            accumulate_uint32(start + 36 + i, end + 36 + i, deltas + idx++);

        /* 8x 32bit B counters + 8x 32bit C counters... */
        for (i = 0; i < 16; i++)
            accumulate_uint32(start + 48 + i, end + 48 + i, deltas + idx++);
        break;

    case I915_OA_FORMAT_A45_B8_C8:

        accumulate_uint32(start + 1, end + 1, deltas); /* timestamp */

        for (i = 0; i < 61; i++)
            accumulate_uint32(start + 3 + i, end + 3 + i, deltas + 1 + i);
        break;
    default:
        assert(0);
    }

    gputop_u32_clock_progress(&accumulator->clock, start[1]);
    if (accumulator->first_timestamp == 0)
        accumulator->first_timestamp =
            gputop_u32_clock_get_time(&accumulator->clock);

    gputop_u32_clock_progress(&accumulator->clock, end[1]);
    accumulator->last_timestamp =
        gputop_u32_clock_get_time(&accumulator->clock);

    return true;
}

void EMSCRIPTEN_KEEPALIVE
gputop_cc_oa_accumulator_clear(struct gputop_cc_oa_accumulator *accumulator)
{
    memset(accumulator->deltas, 0, sizeof(accumulator->deltas));
    accumulator->first_timestamp = 0;
    accumulator->last_timestamp = 0;
    accumulator->flags = 0;
}

void
gputop_cc_oa_accumulator_init(struct gputop_cc_oa_accumulator *accumulator,
                              const struct gputop_devinfo *devinfo,
                              const struct gputop_metric_set *metric_set,
                              bool enable_ctx_switch_events,
                              int aggregation_period)
{
    assert(accumulator);
    assert(metric_set);
    assert(metric_set->perf_oa_format);

    memset(accumulator, 0, sizeof(*accumulator));
    accumulator->devinfo = devinfo;
    accumulator->metric_set = metric_set;
    accumulator->aggregation_period = aggregation_period;
    accumulator->enable_ctx_switch_events = enable_ctx_switch_events;
}
