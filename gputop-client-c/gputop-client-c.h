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

#ifdef EMSCRIPTEN
#include <emscripten.h>
#else
#define EMSCRIPTEN_KEEPALIVE
#endif

#include <stdint.h>
#include <stdlib.h>
#include <stdarg.h>
#include <inttypes.h>
#include <string.h>
#include <sys/types.h>

#include <uuid/uuid.h>

#include <intel_chipset.h>
#include <i915_oa_drm.h>

#include "gputop-client-c-runtime.h"
#include "gputop-oa-counters.h"

#include "oa-hsw.h"
#include "oa-bdw.h"
#include "oa-chv.h"
#include "oa-skl.h"

#ifdef __cplusplus
extern "C" {
#endif

struct gputop_cc_stream {
    uint64_t aggregation_period;
    bool per_ctx_mode;

    struct gputop_metric_set *oa_metric_set;
    struct gputop_oa_accumulator oa_accumulator;

    /* Aggregation may happen accross multiple perf data messages
     * so we may need to copy the last report so that aggregation
     * can continue with the next message... */
    uint8_t *continuation_report;

    void *js_pimple;
};

int gputop_cc_get_counter_id(const char *guid, const char *counter_symbol_name);

void gputop_cc_handle_perf_message(struct gputop_cc_stream *stream,
                                   uint8_t *data,
                                   int len);
// function that resets the accumulator clock and the continuation_report
void gputop_cc_reset_accumulator(struct gputop_cc_stream *stream);
void gputop_cc_handle_i915_perf_message(struct gputop_cc_stream *stream,
                                        uint8_t *data, int len);

void gputop_cc_reset_system_properties(void);
void gputop_cc_set_system_property(const char *name, double value);
void gputop_cc_update_system_metrics(void);

struct gputop_cc_stream *
gputop_cc_stream_new(const char *guid,
                       bool per_ctx_mode,
                       uint32_t aggregation_period);

void gputop_cc_update_stream_period(struct gputop_cc_stream *stream,
                                    uint32_t aggregation_period);

void gputop_cc_stream_destroy(struct gputop_cc_stream *stream);

#ifdef __cplusplus
}
#endif
