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

struct gputop_hash_table *queries;

struct gputop_worker_query {
    int id;
    uint64_t aggregation_period;

    struct oa_clock {
	uint64_t start;
	uint64_t timestamp;
	uint32_t last_raw;
    } oa_clock;

    uint64_t start_timestamp;
    uint64_t end_timestamp;

    struct gputop_perf_query *oa_query;

    /* Aggregation may happen accross multiple perf data messages
     * so we may need to copy the last report so that aggregation
     * can continue with the next message... */
    uint8_t *continuation_report;

    gputop_list_t link;
};

static gputop_list_t open_queries;

struct oa_sample {
   struct i915_perf_record_header header;
   uint8_t oa_report[];
};


static struct gputop_devinfo devinfo;

static int next_rpc_id = 1;

static void __attribute__((noreturn))
assert_not_reached(void)
{
    gputop_web_console_assert(0, "code should not be reached");
}

uint64_t
read_uint64_oa_counter(const struct gputop_perf_query *query,
		       const struct gputop_perf_query_counter *counter,
		       uint64_t *accumulator)
{
    return counter->oa_counter_read_uint64(&devinfo, query, accumulator);
}

uint32_t
read_uint32_oa_counter(const struct gputop_perf_query *query,
		       const struct gputop_perf_query_counter *counter,
		       uint64_t *accumulator)
{
    assert_not_reached();
    //return counter->oa_counter_read_uint32(&devinfo, query, accumulator);
}

bool
read_bool_oa_counter(const struct gputop_perf_query *query,
		     const struct gputop_perf_query_counter *counter,
		     uint64_t *accumulator)
{
    assert_not_reached();
    //return counter->oa_counter_read_bool(&devinfo, query, accumulator);
}

double
read_double_oa_counter(const struct gputop_perf_query *query,
		       const struct gputop_perf_query_counter *counter,
		       uint64_t *accumulator)
{
    assert_not_reached();
    //return counter->oa_counter_read_double(&devinfo, query, accumulator);
}

float
read_float_oa_counter(const struct gputop_perf_query *query,
		      const struct gputop_perf_query_counter *counter,
		      uint64_t *accumulator)
{
    return counter->oa_counter_read_float(&devinfo, query, accumulator);
}

/* NB: the timestamp is 32 bits counting in 80 nanosecond units so
 * it wraps every ~ 6 minutes, this oa_clock api accumulates a
 * 64bit monotonic timestamp in nanoseconds */
static void
oa_clock_init(struct oa_clock *clock, uint32_t raw_start)
{
    clock->timestamp = clock->start = (uint64_t)raw_start * 80;
    //gputop_web_console_log("oa_clock_init: start=%"PRIu32" timestamp=%"PRIu64, raw_start, clock->timestamp);
    clock->last_raw = raw_start;
}

static uint64_t
oa_clock_get_time(struct oa_clock *clock)
{
    return clock->timestamp;
}

static void
oa_clock_accumulate_raw(struct oa_clock *clock, uint32_t raw_timestamp)
{
    uint32_t delta = raw_timestamp - clock->last_raw;
    uint64_t elapsed;

    //gputop_web_console_log("oa_clock_accumulate_raw: raw=%"PRIu32" last = %"PRIu32" delta = %"PRIu32,
    //                        raw_timestamp, clock->last_raw, delta);
    //gputop_web_console_assert(((uint64_t)delta * 80) < 3000000000UL, "Huge timer jump foo");

    clock->timestamp += (uint64_t)delta * 80;
    clock->last_raw = raw_timestamp;
    //gputop_web_console_log("oa_clock_accumulate_raw: clock->last_raw=%"PRIu32, clock->last_raw);

    elapsed = clock->timestamp - clock->start;
    //gputop_web_console_log("oa_clock_accumulate_raw: elapsed=%"PRIu64, elapsed);
}

uint32_t
read_report_raw_timestamp(const uint32_t *report)
{
    return report[1];
}

#define JS_MAX_SAFE_INTEGER (((uint64_t)1<<53) - 1)

void
_gputop_query_update_counter(int counter, int id,
        double start_timestamp, double end_timestamp,
        double delta, double max, double ui64_value);

/* Returns the ID for a counter_name using the symbol_name */
static int EMSCRIPTEN_KEEPALIVE
get_counter_id(const char *guid, const char *counter_symbol_name)
{
    struct gputop_hash_entry *entry = gputop_hash_table_search(queries, guid);
    if (entry == NULL)
        return -1;

    struct gputop_perf_query *query = (struct gputop_perf_query *) entry->data;

    for (int t=0; t<query->n_counters; t++) {
        struct gputop_perf_query_counter *counter = &query->counters[t];
        if (!strcmp(counter->symbol_name, counter_symbol_name))
            return t;
    }
    return -1;
}

static void
forward_query_update(struct gputop_worker_query *query)
{
    struct gputop_perf_query *oa_query = query->oa_query;
    uint64_t delta;
    int i;

    if (query->start_timestamp == 0)
	gputop_web_console_warn("WW: Zero timestamp");

    delta = query->end_timestamp - query->start_timestamp;
    //printf("start ts = %"PRIu64" end ts = %"PRIu64" delta = %"PRIu64" agg. period =%"PRIu64"\n",
    //        query->start_timestamp, query->end_timestamp, delta, query->aggregation_period);

    for (i = 0; i < oa_query->n_counters; i++) {
        uint64_t u53_check;
        double   d_value;
        uint64_t max = 0;

	struct gputop_perf_query_counter *counter = &oa_query->counters[i];
        if (counter->max) {
            max = counter->max(&devinfo, oa_query, oa_query->accumulator);
        }

        switch(counter->data_type) {
            case GPUTOP_PERFQUERY_COUNTER_DATA_UINT32:
                d_value = read_uint32_oa_counter(oa_query, counter, oa_query->accumulator);
            break;
            case GPUTOP_PERFQUERY_COUNTER_DATA_UINT64:
                u53_check = read_uint64_oa_counter(oa_query, counter, oa_query->accumulator);
                if (u53_check > JS_MAX_SAFE_INTEGER) {
                    gputop_web_console_error("Clamping counter to large to represent in JavaScript %s ", counter->symbol_name);
                    u53_check = JS_MAX_SAFE_INTEGER;
                }
                d_value = u53_check;
            break;
            case GPUTOP_PERFQUERY_COUNTER_DATA_FLOAT:
                d_value = read_float_oa_counter(oa_query, counter, oa_query->accumulator);
            break;
            case GPUTOP_PERFQUERY_COUNTER_DATA_DOUBLE:
                d_value = read_double_oa_counter(oa_query, counter, oa_query->accumulator);
            break;
            case GPUTOP_PERFQUERY_COUNTER_DATA_BOOL32:
                d_value = read_bool_oa_counter(oa_query, counter, oa_query->accumulator);
            break;
        }

        _gputop_query_update_counter(i, query->id,
                    query->start_timestamp, query->end_timestamp,
                    delta, max, d_value);
    }
}

static void
notify_bad_report(struct gputop_worker_query *query, uint64_t last_timestamp)
{
    gputop_string_t *str = gputop_string_new("{ \"method\": \"oa_query_bad_report\", ");
    gputop_string_append_printf(str,
				"\"params\": [ { \"id\": %u, "
				"\"last_timestamp\": %"PRIu64,
				query->id,
				last_timestamp);

    gputop_string_append_printf(str, "} ], \"id\": %u }\n", next_rpc_id++);

    //_gputop_web_worker_post(str->str);
    gputop_string_free(str, true);
}

static void EMSCRIPTEN_KEEPALIVE
handle_perf_message(int id, uint8_t *data, int len)
{
    struct gputop_worker_query *query;

    gputop_list_for_each(query, &open_queries, link) {

	if (query->id == id) {
	    gputop_web_console_log("FIXME: parse perf data");
	    return;
	}
    }
    gputop_web_console_log("received perf data for unknown query id: %d", id);
}

static void EMSCRIPTEN_KEEPALIVE
handle_oa_query_i915_perf_data(struct gputop_worker_query *query, uint8_t *data, int len)
{
    struct gputop_perf_query *oa_query = query->oa_query;
    const struct i915_perf_record_header *header;
    uint8_t *last;
    uint64_t end_timestamp;

    if (query->continuation_report) {
	last = query->continuation_report;
	end_timestamp = query->end_timestamp;
    }

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

	case DRM_I915_PERF_RECORD_OA_BUFFER_OVERFLOW:
		gputop_web_console_log("i915_oa: OA buffer overflow\n");
		break;
        case DRM_I915_PERF_RECORD_OA_REPORT_LOST:
            gputop_web_console_log("i915_oa: OA report lost\n");
            break;

	case DRM_I915_PERF_RECORD_SAMPLE: {
	    struct oa_sample *sample = (struct oa_sample *)header;
	    uint8_t *report = sample->oa_report;
	    uint32_t raw_timestamp = read_report_raw_timestamp((uint32_t *)report);
	    uint64_t timestamp;

	    if (raw_timestamp == 0) {
#warning "check for zero report reason instead of checking timestamp"
		/* FIXME: check reason field too, since a zero timestamp may sometimes be valid */
		gputop_web_console_log("i915_oa: spurious report with zero timestamp\n");
		notify_bad_report(query, oa_clock_get_time(&query->oa_clock));
		continue;
	    }

	    if (!last) {
		memset(oa_query->accumulator, 0, sizeof(oa_query->accumulator));

		oa_clock_init(&query->oa_clock, raw_timestamp);
		timestamp = oa_clock_get_time(&query->oa_clock);

		query->start_timestamp = timestamp;
		end_timestamp = timestamp + query->aggregation_period;
	    } else {
		oa_clock_accumulate_raw(&query->oa_clock, raw_timestamp);
		timestamp = oa_clock_get_time(&query->oa_clock);
	    }

	    //gputop_web_console_log("timestamp = %"PRIu64" target duration=%u",
	    //			     timestamp, (unsigned)(end_timestamp - query->start_timestamp));
            if (timestamp >= end_timestamp) {
		forward_query_update(query);
		memset(oa_query->accumulator, 0, sizeof(oa_query->accumulator));
		query->start_timestamp = timestamp;

#if 0
		/* Note: end_timestamp isn't the last timestamp that belonged
		 * to the last update. We don't want the end_timestamp for the
		 * next update to be skewed by exact duration of the previous
		 * update otherwise the elapsed time relative to when the
		 * query started will appear to drift from wall clock time */
		end_timestamp += query->aggregation_period;

		if (end_timestamp <= query->start_timestamp) {
		    gputop_web_console_warn("i915_oa: not forwarding fast enough\n");
		    end_timestamp = timestamp + query->aggregation_period;
		}
#else
		/* XXX: the above comment proably isn't really a good way of
		 * considering this. We want aggregated updates to have a consistent
		 * statistical significance. If we always set our end_timestamp
		 * relative to when we opened the query then there's a risk that
		 * if we aren't keeping up with forwarding data or in cases where
		 * where we have to skip spurious reports from the hardware then
		 * some updates may represent quite varying durations. Setting
		 * the end_timestamp relative to now might be better from this pov.
		 * The issue of elapsed time appearing to drift described above isn't
		 * really an issue since the timestamps themselves will correctly
		 * reflect the progress of time which the UI can represent.
		 */
		end_timestamp = timestamp + query->aggregation_period;
#endif
	    }

	    //str = strdup("SAMPLE\n");
	    if (last) {
		query->end_timestamp = timestamp;

		/* On GEN8+ when a context switch occurs, the hardware
		 * generates a report to indicate that such an event
		 * occurred. We therefore skip over the accumulation for
		 * this report, and instead use it as the base for
		 * subsequent accumulation calculations.
		 *
		 * TODO:(matt-auld)
		 * This can be simplified once our kernel rebases with Sourab'
		 * patches, in particular his work which exposes to user-space
		 * a sample-source-field for OA reports. */
		if (oa_query->per_ctx_mode && gputop_devinfo.gen >= 8) {
		    uint32_t reason = (((uint32_t*)report)[0] >> OAREPORT_REASON_SHIFT) &
			OAREPORT_REASON_MASK;

		    if (!(reason & OAREPORT_REASON_CTX_SWITCH))
		      gputop_oa_accumulate_reports(oa_query, last, report);
		} else {
		    gputop_oa_accumulate_reports(oa_query, last, report);
		}
	    }

	    last = report;
	    break;
	}

	default:
	    gputop_web_console_log("i915 perf: Spurious header type = %d\n", header->type);
	    return;
	}
    }

    if (last) {
	int raw_size = query->oa_query->perf_raw_size;

	if (!query->continuation_report)
	    query->continuation_report = malloc(raw_size);

	memcpy(query->continuation_report, last, raw_size);
	query->end_timestamp = end_timestamp;
    }
}

static void EMSCRIPTEN_KEEPALIVE
handle_i915_perf_message(int id, uint8_t *data, int len)
{
    struct gputop_worker_query *query;

    printf(" %x ", data[0]);

    gputop_list_for_each(query, &open_queries, link) {

	if (query->id == id) {
	    if (query->oa_query)
		handle_oa_query_i915_perf_data(query, data, len);
	    return;
	}
    }
    gputop_web_console_log("received perf data for unknown query id: %d", id);
}

static void EMSCRIPTEN_KEEPALIVE
update_features(uint32_t devid, uint32_t n_eus, uint32_t n_eu_slices,
        uint32_t n_eu_sub_slices, uint32_t eu_threads_count,
        uint32_t subslice_mask, uint32_t slice_mask) {

    devinfo.devid = devid;
    devinfo.n_eus = n_eus;
    devinfo.n_eu_slices = n_eu_slices;
    devinfo.n_eu_sub_slices = n_eu_sub_slices;
    devinfo.eu_threads_count = eu_threads_count;
    devinfo.subslice_mask = subslice_mask;
    devinfo.slice_mask = slice_mask;

    queries = gputop_hash_table_create(NULL, gputop_key_hash_string,
                                       gputop_key_string_equal);

    if (IS_HASWELL(devid)) {
        _gputop_web_console_log("Adding Haswell queries\n");
        emscripten_run_script("gputop.load_oa_queries('hsw');");
        gputop_oa_add_queries_hsw(&devinfo);
    } else if (IS_BROADWELL(devid)) {
        _gputop_web_console_log("Adding Broadwell queries\n");
        emscripten_run_script("gputop.load_oa_queries('bdw');");
        gputop_oa_add_queries_bdw(&devinfo);
    } else if (IS_CHERRYVIEW(devid)) {
        _gputop_web_console_log("Adding Cherryview queries\n");
        gputop_oa_add_queries_chv(&devinfo);
        emscripten_run_script("gputop.load_oa_queries('chv');");
    } else if (IS_SKYLAKE(devid)) {
        _gputop_web_console_log("Adding Skylake queries\n");
        emscripten_run_script("gputop.load_oa_queries('skl');");
        gputop_oa_add_queries_skl(&devinfo);
    } else
        assert_not_reached();

}

void EMSCRIPTEN_KEEPALIVE
gputop_webworker_on_test(const char *msg, float val, const char *req_uuid)
{
    gputop_web_console_log("test message from ui: (%s, %f)\n", msg, val);
}

void EMSCRIPTEN_KEEPALIVE
gputop_webworker_on_open_oa_query(uint32_t id,
				  char *guid,
				  uint32_t aggregation_period)
{
    struct gputop_worker_query *query = malloc(sizeof(*query));
    memset(query, 0, sizeof(*query));
    query->id = id;
    query->aggregation_period = aggregation_period;

    struct gputop_hash_entry *entry = gputop_hash_table_search(queries, guid);

    query->oa_query = (struct gputop_perf_query*) entry->data;

    gputop_list_insert(open_queries.prev, &query->link);
}

void EMSCRIPTEN_KEEPALIVE
gputop_webworker_update_query_period(uint32_t id,
                                     uint32_t aggregation_period)
{
    struct gputop_worker_query *query;
    gputop_list_for_each(query, &open_queries, link) {
        if (query->id == id) {
            query->aggregation_period = aggregation_period;
        }
    }

}


void EMSCRIPTEN_KEEPALIVE
gputop_webworker_on_close_oa_query(uint32_t id)
{
    struct gputop_worker_query *query;

    gputop_web_console_log("on_close_oa_query(%d)\n", id);

    gputop_list_for_each(query, &open_queries, link) {
        if (query->id == id) {
            gputop_list_remove(&query->link);
            free(query);
            return;
        }
    }

    gputop_web_console_warn("webworker: requested to close unknown query ID: %d\n", id);
}

void EMSCRIPTEN_KEEPALIVE
gputop_webworker_init(void)
{
    gputop_list_init(&open_queries);
    gputop_web_console_log("EMSCRIPTEN Init Compilation (" __TIME__ " " __DATE__ ")");
}

void EMSCRIPTEN_KEEPALIVE
myloop() {
    //gputop_web_console_log("main run\n");
}

int
main() {
    emscripten_set_main_loop(myloop, 0, 1);
    printf("emscripten_set_main_loop!\n");
    return 0;
}
