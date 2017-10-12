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

#define _GNU_SOURCE

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

#include <i915_drm.h>

#include <util/macros.h>

#include "gputop-client-c.h"
#include "gputop-client-c-runtime.h"

#include "gputop-oa-counters.h"

#include "oa-hsw.h"
#include "oa-bdw.h"
#include "oa-chv.h"
#include "oa-sklgt2.h"
#include "oa-sklgt3.h"
#include "oa-sklgt4.h"
#include "oa-bxt.h"
#include "oa-kblgt2.h"
#include "oa-kblgt3.h"
#include "oa-glk.h"
#include "oa-cflgt2.h"

#define PERF_RECORD_SAMPLE 9

struct perf_event_header {
    uint32_t type;
    uint16_t misc;
    uint16_t size;
};

struct oa_sample {
   struct drm_i915_perf_record_header header;
   uint8_t oa_report[];
};

static struct gputop_devinfo gputop_devinfo;

static void __attribute__((noreturn))
assert_not_reached(void)
{
    gputop_cr_console_assert(0, "code should not be reached");
    assert(0); /* just to hide compiler warning about noreturn */
}

#define JS_MAX_SAFE_INTEGER (((uint64_t)1<<53) - 1)

/* Returns the ID for a counter_name using the symbol_name */
int EMSCRIPTEN_KEEPALIVE
gputop_cc_get_counter_id(const char *hw_config_guid, const char *counter_symbol_name)
{
    const struct gputop_metric_set *metric_set = gputop_cr_lookup_metric_set(hw_config_guid);

    for (int t=0; t<metric_set->n_counters; t++) {
        const struct gputop_metric_set_counter *counter = &metric_set->counters[t];
        if (!strcmp(counter->symbol_name, counter_symbol_name))
            return t;
    }
    return -1;
}

static void
forward_oa_accumulator_events(struct gputop_cc_stream *stream,
                              struct gputop_cc_oa_accumulator *oa_accumulator,
                              uint32_t events)
{
    const struct gputop_metric_set *oa_metric_set = stream->oa_metric_set;
    int i;

    //printf("start ts = %"PRIu64" end ts = %"PRIu64" agg. period =%"PRIu64"\n",
    //        stream->start_timestamp, stream->end_timestamp, oa_accumulator->aggregation_period);

    if (!_gputop_cr_accumulator_start_update(stream,
                                             oa_accumulator,
                                             events,
                                             oa_accumulator->first_timestamp,
                                             oa_accumulator->last_timestamp))
        return;

    for (i = 0; i < oa_metric_set->n_counters; i++) {
        uint64_t u53_check;
        double d_value = 0;
        uint64_t max = 0;

        const struct gputop_metric_set_counter *counter = &oa_metric_set->counters[i];

        switch(counter->data_type) {
            case GPUTOP_PERFQUERY_COUNTER_DATA_UINT64:
                if (counter->max_uint64) {
                    u53_check = counter->max_uint64(&gputop_devinfo, oa_metric_set,
                                                    oa_accumulator->deltas);
                    if (u53_check > JS_MAX_SAFE_INTEGER) {
                        gputop_cr_console_error("'Max' value is to large to represent in JavaScript: %s ", counter->symbol_name);
                        u53_check = JS_MAX_SAFE_INTEGER;
                    }
                    max = u53_check;
                }

                u53_check = counter->oa_counter_read_uint64(&gputop_devinfo,
                                                            oa_metric_set,
                                                            oa_accumulator->deltas);
                if (u53_check > JS_MAX_SAFE_INTEGER) {
                    gputop_cr_console_error("Clamping counter to large to represent in JavaScript %s ", counter->symbol_name);
                    u53_check = JS_MAX_SAFE_INTEGER;
                }
                d_value = u53_check;
                break;
            case GPUTOP_PERFQUERY_COUNTER_DATA_FLOAT:
                if (counter->max_float) {
                    max = counter->max_float(&gputop_devinfo, oa_metric_set,
                                             oa_accumulator->deltas);
                }

                d_value = counter->oa_counter_read_float(&gputop_devinfo,
                                                         oa_metric_set,
                                                         oa_accumulator->deltas);
                break;
            case GPUTOP_PERFQUERY_COUNTER_DATA_UINT32:
            case GPUTOP_PERFQUERY_COUNTER_DATA_DOUBLE:
            case GPUTOP_PERFQUERY_COUNTER_DATA_BOOL32:
                gputop_cr_console_assert(0, "Unexpected counter data type");
                break;
        }

        _gputop_cr_accumulator_append_count(i, max, d_value);
    }

    _gputop_cr_accumulator_end_update();
}

void EMSCRIPTEN_KEEPALIVE
gputop_cc_handle_tracepoint_message(struct gputop_cc_stream *stream,
                                    uint8_t *data,
                                    int len)
{
    const struct perf_event_header *header;

    gputop_cr_console_error("Tracepoint message: stream=%p, data=%p, len=%d\n",
                            stream, data, len);

    for (header = (void *)data;
         (uint8_t *)header < (data + len);
         header = (void *)(((uint8_t *)header) + header->size))
    {
        if (header->size == 0) {
            gputop_cr_console_error("Spurious header size == 0\n");
            break;
        }

        if (((uint8_t *)header) + header->size > (data + len)) {
            gputop_cr_console_error("Spurious incomplete perf record forwarded\n");
            break;
        }

        switch (header->type) {
        case PERF_RECORD_SAMPLE:
            gputop_cr_console_log("Tracepoint sample received");
            break;
        default:
            break;
        }
    }

    gputop_cr_console_log("FIXME: parse perf tracepoint data");
}

void EMSCRIPTEN_KEEPALIVE
gputop_cc_handle_i915_perf_message(struct gputop_cc_stream *stream,
                                   uint8_t *data, int data_len,
                                   struct gputop_cc_oa_accumulator **accumulators,
                                   int n_accumulators)
{
    const struct drm_i915_perf_record_header *header;
    uint8_t *last = NULL;

    assert(stream);

    if (stream->continuation_report)
        last = stream->continuation_report;
    else {
        for (int i = 0; i < n_accumulators; i++) {
            struct gputop_cc_oa_accumulator *oa_accumulator =
                accumulators[i];

            assert(oa_accumulator);
            gputop_cc_oa_accumulator_clear(oa_accumulator);
        }
    }

    //int i = 0;
    for (header = (void *)data;
         (uint8_t *)header < (data + data_len);
         header = (void *)(((uint8_t *)header) + header->size))
    {
#if 0
        gputop_cr_console_log("header[%d] = %p size=%d type = %d", i, header, header->size, header->type);

        i++;
        if (i > 200) {
            gputop_cr_console_log("perf message too large!\n");
            return;
        }
#endif

        switch (header->type) {

        case DRM_I915_PERF_RECORD_OA_BUFFER_LOST:
            gputop_cr_console_log("i915_oa: OA buffer error - all records lost\n");
            break;
        case DRM_I915_PERF_RECORD_OA_REPORT_LOST:
            gputop_cr_console_log("i915_oa: OA report lost\n");
            break;

        case DRM_I915_PERF_RECORD_SAMPLE: {
            struct oa_sample *sample = (struct oa_sample *)header;

            if (last) {
                for (int i = 0; i < n_accumulators; i++) {
                    struct gputop_cc_oa_accumulator *oa_accumulator =
                        accumulators[i];

                    assert(oa_accumulator);

                    if (gputop_cc_oa_accumulate_reports(oa_accumulator,
                                                        last, sample->oa_report))
                    {
                        uint64_t elapsed = (oa_accumulator->last_timestamp -
                                            oa_accumulator->first_timestamp);
                        uint32_t events = 0;
                        //gputop_cr_console_log("i915_oa: accumulated reports\n");

                        if (elapsed > oa_accumulator->aggregation_period) {
                            //gputop_cr_console_log("i915_oa: PERIOD ELAPSED (%d)\n", (int)oa_accumulator->aggregation_period);
                            events |= ACCUMULATOR_EVENT_PERIOD_ELAPSED;
                        }

                        if (events)
                            forward_oa_accumulator_events(stream, oa_accumulator, events);
                    }
                }
            }

            last = sample->oa_report;

            break;
        }

        default:
            gputop_cr_console_log("i915 perf: Spurious header type = %d\n", header->type);
            return;
        }
    }

    if (last) {
        int raw_size = stream->oa_metric_set->perf_raw_size;

        if (!stream->continuation_report)
            stream->continuation_report = malloc(raw_size);

        memcpy(stream->continuation_report, last, raw_size);
    }
}

void EMSCRIPTEN_KEEPALIVE
gputop_cc_reset_system_properties(void)
{
    memset(&gputop_devinfo, 0, sizeof(gputop_devinfo));
}

void EMSCRIPTEN_KEEPALIVE
gputop_cc_set_system_property(const char *name, double value)
{
    /* Use _Generic so we don't get caught out by a silent error if
     * we mess with struct gputop_devinfo...
     */
#define PROP(NAME, TYPE) { #NAME,                                \
                           TYPE_ ## TYPE,                        \
                           &gputop_devinfo.NAME,                 \
                           sizeof(gputop_devinfo.NAME) }
    static const struct {
        const char *name;
        enum {
            TYPE_U32,
            TYPE_U64,
        } type;
        void *symbol;
        size_t size;
    } table[] = {
        PROP(devid, U32),
        PROP(gen, U32),
        PROP(timestamp_frequency, U64),
        PROP(n_eus, U64),
        PROP(n_eu_slices, U64),
        PROP(n_eu_sub_slices, U64),
        PROP(eu_threads_count, U64),
        PROP(subslice_mask, U64),
        PROP(slice_mask, U64),
        PROP(gt_min_freq, U64),
        PROP(gt_max_freq, U64),
    };
#undef PROP

    for (uint32_t i = 0; ARRAY_SIZE(table); i++) {
        if (strcmp(name, table[i].name) == 0) {
            switch (table[i].type) {
            case TYPE_U32:
                assert(sizeof(uint32_t) == table[i].size);
                gputop_cr_console_assert(value >= 0 && value <= UINT32_MAX,
                                         "Value for uint32 property out of range");
                *((uint32_t *)table[i].symbol) = (uint32_t)value;
                return;
            case TYPE_U64:
                assert(sizeof(uint64_t) == table[i].size);
                gputop_cr_console_assert(value >= 0,
                                         "Value for uint64 property out of range");
                *((uint64_t *)table[i].symbol) = (uint64_t)value;
                return;
            default:
                gputop_cr_console_assert(0, "Unexpected struct gputop_devinfo %s member type",
                                         table[i].name);
            }
        }
    }

    gputop_cr_console_error("Unknown system property %s\n", name);
}

void EMSCRIPTEN_KEEPALIVE
gputop_cc_set_system_property_string(const char *name, const char *value)
{
    /* Use _Generic so we don't get caught out by a silent error if
     * we mess with struct gputop_devinfo...
     */
#define PROP(NAME) { #NAME, gputop_devinfo.NAME, sizeof(gputop_devinfo.NAME), }
    static const struct {
        const char *name;
        char *symbol;
        uint32_t max_size;
    } table[] = {
        PROP(devname),
        PROP(prettyname),
    };
#undef PROP

    for (uint32_t i = 0; ARRAY_SIZE(table); i++) {
        if (strcmp(name, table[i].name) == 0) {
            strncpy(table[i].symbol, value, table[i].max_size);
            return;
        }
    }

    gputop_cr_console_error("Unknown system property %s\n", name);
}

static void
register_metric_set(const struct gputop_metric_set *metric_set, void *data)
{
    gputop_cr_index_metric_set(metric_set->hw_config_guid, metric_set);
}

void EMSCRIPTEN_KEEPALIVE
gputop_cc_update_system_metrics(void)
{
    uint32_t devid = gputop_devinfo.devid;
    struct {
        char *devname;
        void (*add_metrics_cb)(const struct gputop_devinfo *devinfo,
                               void (*register_metric_set)(const struct gputop_metric_set *,
                                                           void *),
                               void *data);
    } devname_to_metric_func[] = {
        { "hsw", gputop_oa_add_metrics_hsw },
        { "bdw", gputop_oa_add_metrics_bdw },
        { "chv", gputop_oa_add_metrics_chv },
        { "sklgt2", gputop_oa_add_metrics_sklgt2 },
        { "sklgt3", gputop_oa_add_metrics_sklgt3 },
        { "sklgt4", gputop_oa_add_metrics_sklgt4 },
        { "kblgt2", gputop_oa_add_metrics_kblgt2 },
        { "kblgt3", gputop_oa_add_metrics_kblgt3 },
        { "bxt", gputop_oa_add_metrics_bxt },
        { "glk", gputop_oa_add_metrics_glk },
        { "cflgt2", gputop_oa_add_metrics_cflgt2 },
   };

    gputop_cr_console_assert(devid != 0, "Device ID not initialized before trying to update system metrics");

    for (uint32_t i = 0; i < ARRAY_SIZE(devname_to_metric_func); i++) {
        if (!strcmp(gputop_devinfo.devname, devname_to_metric_func[i].devname)) {
            devname_to_metric_func[i].add_metrics_cb(&gputop_devinfo,
                                                     register_metric_set, NULL);
            return;
        }
    }

    gputop_cr_console_error("FIXME: Unknown platform device ID 0x%x: " __FILE__, devid);
    assert_not_reached();
}

struct gputop_cc_stream * EMSCRIPTEN_KEEPALIVE
gputop_cc_oa_stream_new(const char *hw_config_guid)
{
    struct gputop_cc_stream *stream = malloc(sizeof(*stream));

    assert(stream);

    memset(stream, 0, sizeof(*stream));

    stream->type = STREAM_TYPE_OA;

    stream->oa_metric_set = gputop_cr_lookup_metric_set(hw_config_guid);
    assert(stream->oa_metric_set);
    assert(stream->oa_metric_set->perf_oa_format);

    return stream;
}

struct gputop_cc_oa_accumulator * EMSCRIPTEN_KEEPALIVE
gputop_cc_oa_accumulator_new(struct gputop_cc_stream *stream,
                             int aggregation_period,
                             bool enable_ctx_switch_events)
{
    struct gputop_cc_oa_accumulator *accumulator = malloc(sizeof(*accumulator));

    assert(accumulator);
    assert(stream);

    gputop_cc_oa_accumulator_init(accumulator,
                                  &gputop_devinfo,
                                  stream->oa_metric_set,
                                  enable_ctx_switch_events,
                                  aggregation_period);
    return accumulator;
}

void EMSCRIPTEN_KEEPALIVE
gputop_cc_oa_accumulator_set_period(struct gputop_cc_oa_accumulator *accumulator,
                                    uint32_t aggregation_period)
{
    assert(accumulator);

    accumulator->aggregation_period = aggregation_period;
}

void EMSCRIPTEN_KEEPALIVE
gputop_cc_oa_accumulator_destroy(struct gputop_cc_oa_accumulator *accumulator)
{
    assert(accumulator);

    gputop_cr_console_log("Freeing client-c OA accumulator %p\n", accumulator);

    free(accumulator);
}


struct gputop_cc_stream * EMSCRIPTEN_KEEPALIVE
gputop_cc_tracepoint_stream_new(void)
{
    struct gputop_cc_stream *stream = malloc(sizeof(*stream));

    assert(stream);

    memset(stream, 0, sizeof(*stream));

    stream->type = STREAM_TYPE_TRACEPOINT;

    return stream;
}

void EMSCRIPTEN_KEEPALIVE
gputop_cc_tracepoint_add_field(struct gputop_cc_stream *stream,
                               const char *name,
                               const char *type_name,
                               int offset,
                               int size,
                               bool is_signed)
{
    enum gputop_cc_field_type type;
    struct gputop_cc_tracepoint_field *field;

    gputop_cr_console_log("Ading field: %s\n", name);
    assert(stream->n_fields < GPUTOP_CC_MAX_FIELDS);

    if (strcmp(type_name, "char") == 0 && size == 1 && is_signed) {
        type = FIELD_TYPE_INT8;
    } else if (strcmp(type_name, "unsigned char") == 0 && size == 1 && !is_signed) {
        type = FIELD_TYPE_UINT8;
    } else if (strcmp(type_name, "short") == 0 && size == 2 && is_signed) {
        type = FIELD_TYPE_INT16;
    } else if (strcmp(type_name, "unsigned short") == 0 && size == 2 && !is_signed) {
        type = FIELD_TYPE_UINT16;
    } else if (strcmp(type_name, "int") == 0 && size == 4 && is_signed) {
        type = FIELD_TYPE_INT32;
    } else if (strcmp(type_name, "unsigned int") == 0 && size == 4 && !is_signed) {
        type = FIELD_TYPE_UINT32;
    } else if (strcmp(type_name, "int") == 0 && size == 8 && is_signed) {
        type = FIELD_TYPE_INT64;
    } else if (strcmp(type_name, "unsigned int") == 0 && size == 6 && !is_signed) {
        type = FIELD_TYPE_UINT64;
    } else {
        /* skip unsupport types */
        return;
    }

    field = &stream->fields[stream->n_fields++];
    field->name = strdup(name);
    field->type = type;
    field->offset = offset;
}

void EMSCRIPTEN_KEEPALIVE
gputop_cc_stream_destroy(struct gputop_cc_stream *stream)
{
    gputop_cr_console_log("Freeing client-c stream %p\n", stream);

    assert(stream);

    free(stream->continuation_report);
    free(stream);
}

#ifdef EMSCRIPTEN
static void
dummy_mainloop_callback(void)
{
}

int
main() {
    /* XXX: this is a hack to ensure we leave the Runtime initialized
     * even though we don't use the emscripten mainloop callback itself
     */
    emscripten_set_main_loop(dummy_mainloop_callback, 1, 1);

    return 0;
}
#endif
