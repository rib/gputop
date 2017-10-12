/*
 * GPU Top
 *
 * Copyright (C) 2016 Intel Corporation
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

/* APIs available within the client-c code that themselves call back in to the
 * JavaScript runtime - either in a browser or node.js
 */

#pragma once

#include "gputop-client-c.h"
#include "gputop-oa-counters.h"

#ifdef __cplusplus
extern "C" {
#endif

void _gputop_cr_console_log(const char *message);
void gputop_cr_console_log(const char *format, ...);

void _gputop_cr_console_warn(const char *message);
void gputop_cr_console_warn(const char *format, ...);

void _gputop_cr_console_error(const char *message);
void gputop_cr_console_error(const char *format, ...);

void _gputop_cr_console_assert(bool condition, const char *message);
void gputop_cr_console_assert(bool condition, const char *format, ...);

void gputop_cr_index_metric_set(const char *guid, const struct gputop_metric_set *metric_set);
const struct gputop_metric_set *gputop_cr_lookup_metric_set(const char *guid);

enum gputop_cr_accumulator_event {
    ACCUMULATOR_EVENT_PERIOD_ELAPSED    = 1,
    ACCUMULATOR_EVENT_CTX_SWITCH_TO     = 2,
    ACCUMULATOR_EVENT_CTX_SWITCH_AWAY   = 4
};

struct gputop_cc_stream;

/* If start returns false then the update can be skipped */
bool _gputop_cr_accumulator_start_update(struct gputop_cc_stream *stream,
                                         struct gputop_cc_oa_accumulator *accumulator,
                                         uint32_t events,
                                         double start_timestamp, double end_timestamp);
void _gputop_cr_accumulator_append_count(int counter,
                                         double max, double value);
void _gputop_cr_accumulator_end_update(void);

#ifdef __cplusplus
}
#endif
