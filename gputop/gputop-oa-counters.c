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

#include <string.h>

#include "gputop-oa-counters.h"

#ifdef EMSCRIPTEN
#include "gputop-web-lib.h"
#define dbg gputop_web_console_log
#else
#include "gputop-log.h"
#endif

struct gputop_devinfo gputop_devinfo;

static uint64_t
timebase_scale(uint64_t u32_time)
{
    return (u32_time * 1000000000) / gputop_devinfo.timestamp_frequency;
}

void
gputop_u32_clock_init(struct gputop_u32_clock *clock, uint32_t u32_start)
{
    clock->timestamp = clock->start = timebase_scale(u32_start);
    clock->last_u32 = u32_start;
    clock->initialized = true;
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

    clock->timestamp += timebase_scale(delta);
    clock->last_u32 = u32_timestamp;
}

static void
gputop_oa_accumulate_uint32(const uint32_t *report0,
                            const uint32_t *report1,
                            uint64_t *deltas)
{
   *deltas += (uint32_t)(*report1 - *report0);
}

static void
gputop_oa_accumulate_uint40(int a_index,
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
gputop_oa_accumulate_reports(struct gputop_oa_accumulator *accumulator,
                             const uint8_t *report0,
                             const uint8_t *report1,
                             bool per_ctx_mode)
{
    struct gputop_metric_set *metric_set = accumulator->metric_set;
    uint64_t *deltas = accumulator->deltas;
    const uint32_t *start = (const uint32_t *)report0;
    const uint32_t *end = (const uint32_t *)report1;
    uint32_t start_reason = ((start[0] >> OAREPORT_REASON_SHIFT) &
                             OAREPORT_REASON_MASK);
    uint32_t end_reason = ((start[0] >> OAREPORT_REASON_SHIFT) &
                           OAREPORT_REASON_MASK);
    bool ret = true;
    int idx = 0;
    int i;

    assert(report0 != report1);

    if (!accumulator->clock.initialized) {
        gputop_u32_clock_init(&accumulator->clock, start[1]);
        accumulator->last_ctx_id = start[2];
    }


    if (start[2] == 0x1fffff) {
        dbg("switch away reason = 0x%x\n", start_reason);
    }
    if (end[2] == 0x1fffff) {
        dbg("switch away reason = 0x%x\n", end_reason);
    }

    switch (metric_set->perf_oa_format) {
    case I915_OA_FORMAT_A32u40_A4u32_B8_C8:

        if (((start_reason | end_reason) & (OAREPORT_REASON_CTX_SWITCH |
                                            OAREPORT_REASON_TIMER)) == 0)
            dbg("accumulator: Unknown OA sample reason: start = %"
                PRIu32" end = %"PRIu32"\n",
                start_reason, end_reason);

        /* While in per-ctx-mode we aim to detect and flag context switches to
         * and from the specific context being filtered for so the caller can
         * optionally update the UI at these key points and reset accumulation.
         *
         * More specificically:
         * - we flag the accumulation with _CTX_SW_TO_SEEN only if
         *   if the first report (since the last clear) is a switch-to report.
         * - we flag the accumulation with _CTX_SW_FROM_SEEN while the last
         *   report (since the last clear) is a switch-from report.
         * - in the case where the start report is a switch-away and the end
         *   is a switch-to then we skip over accumulation since the deltas
         *   relate to the work of other contexts.
         */
        if (per_ctx_mode) {
            /* the switch-from state may be transient if the caller doesn't
             * decide to clear the accumulator after seeing the switch-from */
            accumulator->flags &= ~GPUTOP_ACCUMULATOR_CTX_SW_FROM_SEEN;


            /* XXX: we don't trust that the first report we see after a context
             * switch will be tagged by the HW as a context-switch since we often
             * see that the first sample is in fact a timer sample. We keep track
             * of the last ctx_id we've see so we can spot the switches
             * ourselves.
             */
            if (accumulator->first_timestamp == 0 &&
                accumulator->last_ctx_id != start[2])
            {
                if (start[2] == 0x1ffff) { /* this invalid ctx-id marks a switch-from */
                    ret = false;
                    goto exit;
                } else
                    accumulator->flags |= GPUTOP_ACCUMULATOR_CTX_SW_TO_SEEN;
            }

            if (start[2] != end[2]) {
                if (end[2] == 0x1ffff) { /* this invalid ctx-id marks a switch-from */
                    accumulator->flags |= GPUTOP_ACCUMULATOR_CTX_SW_FROM_SEEN;
                } else {
                    if (start[2] != 0x1fffff)
                        dbg("accumulator: spurious per-ctx switch-to not preceded by switch-from");

                    ret = false;
                    goto exit;
                }
            }
        }

        gputop_oa_accumulate_uint32(start + 1, end + 1, deltas + idx++); /* timestamp */
        gputop_oa_accumulate_uint32(start + 3, end + 3, deltas + idx++); /* clock */

        /* 32x 40bit A counters... */
        for (i = 0; i < 32; i++)
            gputop_oa_accumulate_uint40(i, start, end, deltas + idx++);

        /* 4x 32bit A counters... */
        for (i = 0; i < 4; i++)
            gputop_oa_accumulate_uint32(start + 36 + i, end + 36 + i,
                                        deltas + idx++);

        /* 8x 32bit B counters + 8x 32bit C counters... */
        for (i = 0; i < 16; i++)
            gputop_oa_accumulate_uint32(start + 48 + i, end + 48 + i,
                                        deltas + idx++);
        break;

    case I915_OA_FORMAT_A45_B8_C8:

        /* technically a timestamp of zero is valid, but much more likely it
         * indicates a problem... */
        if (start[1] == 0 || end[1] == 0)
            dbg("i915_oa: spurious report with timestamp of zero\n");

        gputop_oa_accumulate_uint32(start + 1, end + 1, deltas); /* timestamp */

        for (i = 0; i < 61; i++)
            gputop_oa_accumulate_uint32(start + 3 + i, end + 3 + i,
                                        deltas + 1 + i);
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

exit:
    accumulator->last_ctx_id = end[2];
    return ret;
}

void
gputop_oa_accumulator_clear(struct gputop_oa_accumulator *accumulator)
{
    memset(accumulator->deltas, 0, sizeof(accumulator->deltas));
    accumulator->first_timestamp = 0;
    accumulator->last_timestamp = 0;
    accumulator->flags = 0;
}

void
gputop_oa_accumulator_init(struct gputop_oa_accumulator *accumulator,
                           struct gputop_metric_set *metric_set)
{
    assert(accumulator);
    assert(metric_set);
    assert(metric_set->perf_oa_format);

    memset(accumulator, 0, sizeof(*accumulator));
    accumulator->metric_set = metric_set;
}
