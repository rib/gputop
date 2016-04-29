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


#include <stdint.h>
#include <stdlib.h>
#include <stdarg.h>
#include <inttypes.h>
#include <string.h>
#include <sys/types.h>

#include <uuid/uuid.h>
#include <emscripten.h>

#include <intel_chipset.h>
#include <i915_oa_drm.h>

#include <gputop-string.h>
#include <gputop-oa-counters.h>

#include "gputop-web-lib.h"

#include "oa-hsw.h"
#include "oa-bdw.h"
#include "oa-chv.h"
#include "oa-skl.h"

struct gputop_webc_stream {
    uint64_t aggregation_period;
    bool per_ctx_mode;

    struct gputop_metric_set *oa_metric_set;
    struct gputop_oa_accumulator oa_accumulator;

    /* Aggregation may happen accross multiple perf data messages
     * so we may need to copy the last report so that aggregation
     * can continue with the next message... */
    uint8_t *continuation_report;
};

struct oa_sample {
   struct i915_perf_record_header header;
   uint8_t oa_report[];
};


static void __attribute__((noreturn))
assert_not_reached(void)
{
    gputop_web_console_assert(0, "code should not be reached");
}

#define JS_MAX_SAFE_INTEGER (((uint64_t)1<<53) - 1)

void
_gputop_stream_update_counter(int counter, struct gputop_webc_stream *stream,
                              double start_timestamp, double end_timestamp,
                              double delta, double max, double ui64_value);

/* Returns the ID for a counter_name using the symbol_name */
int EMSCRIPTEN_KEEPALIVE
gputop_webc_get_counter_id(const char *guid, const char *counter_symbol_name)
{
    struct gputop_metric_set *metric_set = gputop_web_lookup_metric_set(guid);

    for (int t=0; t<metric_set->n_counters; t++) {
        struct gputop_metric_set_counter *counter = &metric_set->counters[t];
        if (!strcmp(counter->symbol_name, counter_symbol_name))
            return t;
    }
    return -1;
}

enum update_reason {
    UPDATE_REASON_PERIOD            = 1,
    UPDATE_REASON_CTX_SWITCH_TO     = 2,
    UPDATE_REASON_CTX_SWITCH_AWAY   = 4
};

static void
forward_stream_update(struct gputop_webc_stream *stream,
                      enum update_reason reason)
{
    struct gputop_metric_set *oa_metric_set = stream->oa_metric_set;
    struct gputop_oa_accumulator *oa_accumulator = &stream->oa_accumulator;
    int i;

    //printf("start ts = %"PRIu64" end ts = %"PRIu64" agg. period =%"PRIu64"\n",
    //        stream->start_timestamp, stream->end_timestamp, stream->aggregation_period);

    for (i = 0; i < oa_metric_set->n_counters; i++) {
        uint64_t u53_check;
        double   d_value;
        uint64_t max = 0;

        struct gputop_metric_set_counter *counter = &oa_metric_set->counters[i];
        if (counter->max) {
            max = counter->max(&gputop_devinfo, oa_metric_set,
                               oa_accumulator->deltas);
        }

        switch(counter->data_type) {
            case GPUTOP_PERFQUERY_COUNTER_DATA_UINT64:
                u53_check = counter->oa_counter_read_uint64(&gputop_devinfo,
                                                            oa_metric_set,
                                                            oa_accumulator->deltas);
                if (u53_check > JS_MAX_SAFE_INTEGER) {
                    gputop_web_console_error("Clamping counter to large to represent in JavaScript %s ", counter->symbol_name);
                    u53_check = JS_MAX_SAFE_INTEGER;
                }
                d_value = u53_check;
                break;
            case GPUTOP_PERFQUERY_COUNTER_DATA_FLOAT:
                d_value = counter->oa_counter_read_float(&gputop_devinfo,
                                                         oa_metric_set,
                                                         oa_accumulator->deltas);
                break;
            case GPUTOP_PERFQUERY_COUNTER_DATA_UINT32:
            case GPUTOP_PERFQUERY_COUNTER_DATA_DOUBLE:
            case GPUTOP_PERFQUERY_COUNTER_DATA_BOOL32:
                gputop_web_console_assert(0, "Unexpected counter data type");
                break;
        }

        _gputop_stream_update_counter(i,
                                      stream,
                                      oa_accumulator->first_timestamp,
                                      oa_accumulator->last_timestamp,
                                      max,
                                      d_value,
                                      reason);
    }
}

void EMSCRIPTEN_KEEPALIVE
gputop_webc_handle_perf_message(struct gputop_webc_stream *stream,
                                uint8_t *data,
                                int len)
{
    gputop_web_console_log("FIXME: parse perf data");
}

// function that resets the accumulator clock and the continuation_report
void EMSCRIPTEN_KEEPALIVE
gputop_webc_reset_accumulator(struct gputop_webc_stream *stream)
{
    stream->continuation_report = NULL;
    (&stream->oa_accumulator)->clock.initialized = false;
}

void EMSCRIPTEN_KEEPALIVE
gputop_webc_handle_i915_perf_message(struct gputop_webc_stream *stream,
                                     uint8_t *data, int len)
{
    struct gputop_oa_accumulator *oa_accumulator = &stream->oa_accumulator;
    const struct i915_perf_record_header *header;
    uint8_t *last = NULL;

    assert(stream);
    assert(oa_accumulator);

    if (stream->continuation_report)
        last = stream->continuation_report;
    else
        gputop_oa_accumulator_clear(oa_accumulator);

    for (header = (void *)data;
         (uint8_t *)header < (data + len);
         header = (void *)(((uint8_t *)header) + header->size))
    {
#if 0
        gputop_web_console_log("header[%d] = %p size=%d type = %d", i, header, header->size, header->type);

        i++;
        if (i > 200) {
            gputop_web_console_log("perf message too large!\n");
            return;
        }
#endif

        switch (header->type) {

        case DRM_I915_PERF_RECORD_OA_BUFFER_LOST:
            gputop_web_console_log("i915_oa: OA buffer error - all records lost\n");
            break;
        case DRM_I915_PERF_RECORD_OA_REPORT_LOST:
            gputop_web_console_log("i915_oa: OA report lost\n");
            break;

        case DRM_I915_PERF_RECORD_SAMPLE: {
            struct oa_sample *sample = (struct oa_sample *)header;
            enum update_reason reason = 0;

            if (last) {
                if (gputop_oa_accumulate_reports(oa_accumulator,
                                                 last, sample->oa_report,
                                                 stream->per_ctx_mode))
                {
                    uint64_t elapsed = (oa_accumulator->last_timestamp -
                                        oa_accumulator->first_timestamp);

                    if (elapsed > stream->aggregation_period)
                        reason = UPDATE_REASON_PERIOD;
                    if (oa_accumulator->flags & GPUTOP_ACCUMULATOR_CTX_SW_TO_SEEN)
                        reason = UPDATE_REASON_CTX_SWITCH_TO;
                    if (oa_accumulator->flags & GPUTOP_ACCUMULATOR_CTX_SW_FROM_SEEN)
                        reason = UPDATE_REASON_CTX_SWITCH_AWAY;

                    if (reason) {
                        forward_stream_update(stream, reason);
                        gputop_oa_accumulator_clear(oa_accumulator);
                    }
                }
            }

            last = sample->oa_report;

            break;
        }

        default:
            gputop_web_console_log("i915 perf: Spurious header type = %d\n", header->type);
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

/* The C code generated by oa-gen.py calls this function for each
 * metric set.
 */
void
gputop_register_oa_metric_set(struct gputop_metric_set *metric_set)
{
    gputop_web_console_log("register %s metric set with guid = %s\n",
                           metric_set->name,
                           metric_set->guid);
    gputop_web_index_metric_set(metric_set->guid, metric_set);
}

void EMSCRIPTEN_KEEPALIVE
gputop_webc_update_features(uint32_t devid,
                            uint32_t gen,
                            uint32_t timestamp_frequency,
                            uint32_t n_eus,
                            uint32_t n_eu_slices,
                            uint32_t n_eu_sub_slices,
                            uint32_t eu_threads_count,
                            uint32_t subslice_mask,
                            uint32_t slice_mask,
                            uint32_t gt_min_freq,
                            uint32_t gt_max_freq)
{
    gputop_devinfo.devid = devid;
    gputop_devinfo.gen = gen;
    gputop_devinfo.timestamp_frequency = timestamp_frequency;
    gputop_devinfo.n_eus = n_eus;
    gputop_devinfo.n_eu_slices = n_eu_slices;
    gputop_devinfo.n_eu_sub_slices = n_eu_sub_slices;
    gputop_devinfo.eu_threads_count = eu_threads_count;
    gputop_devinfo.subslice_mask = subslice_mask;
    gputop_devinfo.slice_mask = slice_mask;
    gputop_devinfo.gt_min_freq = gt_min_freq;
    gputop_devinfo.gt_max_freq = gt_max_freq;

    if (IS_HASWELL(devid)) {
        _gputop_web_console_log("Adding Haswell metrics\n");
        gputop_oa_add_metrics_hsw(&gputop_devinfo);
    } else if (IS_BROADWELL(devid)) {
        _gputop_web_console_log("Adding Broadwell metrics\n");
        gputop_oa_add_metrics_bdw(&gputop_devinfo);
    } else if (IS_CHERRYVIEW(devid)) {
        _gputop_web_console_log("Adding Cherryview metrics\n");
        gputop_oa_add_metrics_chv(&gputop_devinfo);
    } else if (IS_SKYLAKE(devid)) {
        _gputop_web_console_log("Adding Skylake metrics\n");
        gputop_oa_add_metrics_skl(&gputop_devinfo);
    } else
        assert_not_reached();

}

struct gputop_webc_stream * EMSCRIPTEN_KEEPALIVE
gputop_webc_stream_new(const char *guid,
                       bool per_ctx_mode,
                       uint32_t aggregation_period)
{
    struct gputop_webc_stream *stream = malloc(sizeof(*stream));

    assert(stream);

    memset(stream, 0, sizeof(*stream));
    stream->aggregation_period = aggregation_period;
    stream->per_ctx_mode = per_ctx_mode;

    stream->oa_metric_set = gputop_web_lookup_metric_set(guid);
    assert(stream->oa_metric_set);
    assert(stream->oa_metric_set->perf_oa_format);

    gputop_oa_accumulator_init(&stream->oa_accumulator, stream->oa_metric_set);

    return stream;
}

void EMSCRIPTEN_KEEPALIVE
gputop_webc_update_stream_period(struct gputop_webc_stream *stream,
                                 uint32_t aggregation_period)
{
    stream->aggregation_period = aggregation_period;
}

void EMSCRIPTEN_KEEPALIVE
gputop_webc_stream_destroy(struct gputop_webc_stream *stream)
{
    gputop_web_console_log("Freeing webc stream %p\n", stream);

    free(stream->continuation_report);
    free(stream);
}

static void
dummy_mainloop_callback(void)
{
}

void EMSCRIPTEN_KEEPALIVE
gputop_webc_init(void)
{
    gputop_web_console_log("EMSCRIPTEN Init Compilation (" __TIME__ " " __DATE__ ")");
}

int
main() {
    gputop_webc_init();

    /* XXX: this is a hack to ensure we leave the Runtime initialized
     * even though we don't use the emscripten mainloop callback itself
     */
    emscripten_set_main_loop(dummy_mainloop_callback, 1, 1);

    return 0;
}
