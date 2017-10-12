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

#include <stdint.h>
#include <stdlib.h>
#include <stdarg.h>
#include <inttypes.h>
#include <string.h>
#include <sys/types.h>

#include "gputop-oa-counters.h"

#ifdef __cplusplus
extern "C" {
#endif

enum gputop_cc_stream_type {
    STREAM_TYPE_OA,
    STREAM_TYPE_TRACEPOINT,
};

enum gputop_cc_field_type {
    FIELD_TYPE_INT8,
    FIELD_TYPE_UINT8,
    FIELD_TYPE_INT16,
    FIELD_TYPE_UINT16,
    FIELD_TYPE_INT32,
    FIELD_TYPE_UINT32,
    FIELD_TYPE_INT64,
    FIELD_TYPE_UINT64,
};

#define GPUTOP_CC_MAX_FIELDS 20

struct gputop_cc_tracepoint_field {
    char *name;
    enum gputop_cc_field_type type;
    int offset;
};

struct gputop_cc_stream {
    enum gputop_cc_stream_type type;

    /*
     * OA streams...
     */
    const struct gputop_metric_set *oa_metric_set;

    /* Aggregation may happen accross multiple perf data messages
     * so we may need to copy the last report so that aggregation
     * can continue with the next message... */
    uint8_t *continuation_report;


    /*
     * Tracepoint streams...
     */
    struct gputop_cc_tracepoint_field fields[GPUTOP_CC_MAX_FIELDS];
    int n_fields;


    /* Can be used for binding structure into JavaScript, e.g. to
     * associate a corresponding v8::Object... */
    void *js_priv;
};

int gputop_cc_get_counter_id(const char *guid, const char *counter_symbol_name);

void gputop_cc_handle_i915_perf_message(struct gputop_cc_stream *stream,
                                        uint8_t *data, int data_len,
                                        struct gputop_cc_oa_accumulator **accumulators,
                                        int n_accumulators);

void gputop_cc_reset_system_properties(void);
void gputop_cc_set_system_property(const char *name, double value);
void gputop_cc_set_system_property_string(const char *name, const char *value);
void gputop_cc_update_system_metrics(void);

struct gputop_cc_stream *
gputop_cc_oa_stream_new(const char *guid);
void gputop_cc_stream_destroy(struct gputop_cc_stream *stream);

struct gputop_cc_oa_accumulator *
gputop_cc_oa_accumulator_new(struct gputop_cc_stream *stream,
                             int aggregation_period,
                             bool enable_ctx_switch_events);
void gputop_cc_oa_accumulator_set_period(struct gputop_cc_oa_accumulator *accumulator,
                                         uint32_t aggregation_period);
void gputop_cc_oa_accumulator_destroy(struct gputop_cc_oa_accumulator *accumulator);

struct gputop_cc_stream *gputop_cc_tracepoint_stream_new(void);
void gputop_cc_tracepoint_add_field(struct gputop_cc_stream *stream,
                                    const char *name,
                                    const char *type,
                                    int offset,
                                    int size,
                                    bool is_signed);
void gputop_cc_handle_tracepoint_message(struct gputop_cc_stream *stream,
                                         uint8_t *data,
                                         int len);

#ifdef __cplusplus
}
#endif
