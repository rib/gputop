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

#include <gputop-perf.h>
#include <gputop-web.h>
#include <gputop-string.h>
#include <gputop.pb-c.h>
#include <gputop-oa-counters.h>
#include <gputop-util.h>

#include "oa-hsw.h"
#include "oa-bdw.h"
#include "oa-chv.h"
#include "oa-skl.h"


struct gputop_hash_table *queries;
struct array *perf_oa_supported_query_guids;

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
static int socket = 0;

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
    //uint64_t elapsed;

    //gputop_web_console_log("oa_clock_accumulate_raw: raw=%"PRIu32" last = %"PRIu32" delta = %"PRIu32,
	//		   raw_timestamp, clock->last_raw, delta);
    //gputop_web_console_assert(((uint64_t)delta * 80) < 3000000000UL, "Huge timer jump foo");

    clock->timestamp += (uint64_t)delta * 80;
    clock->last_raw = raw_timestamp;
    //gputop_web_console_log("oa_clock_accumulate_raw: clock->last_raw=%"PRIu32, clock->last_raw);

    //elapsed = clock->timestamp - clock->start;
    //gputop_web_console_log("oa_clock_accumulate_raw: elapsed=%"PRIu64, elapsed);
}

uint32_t
read_report_raw_timestamp(const uint32_t *report)
{
    return report[1];
}

#define JS_MAX_SAFE_INTEGER (((uint64_t)1<<53) - 1)

static void
append_raw_oa_counter(gputop_string_t *str,
		      struct gputop_perf_query *query,
		      const struct gputop_perf_query_counter *counter)
{
    uint64_t u53_check;

    switch(counter->data_type) {
    case GPUTOP_PERFQUERY_COUNTER_DATA_UINT32:
	gputop_string_append_printf(str, "%" PRIu32,
		  read_uint32_oa_counter(query,
					 counter,
					 query->accumulator));
	break;
    case GPUTOP_PERFQUERY_COUNTER_DATA_UINT64:
	u53_check = read_uint64_oa_counter(query,
					   counter,
					   query->accumulator);
	if (u53_check > JS_MAX_SAFE_INTEGER) {
	    gputop_web_console_error("Clamping counter to large to represent in JavaScript");
	    u53_check = JS_MAX_SAFE_INTEGER;
	}
	gputop_string_append_printf(str, "%" PRIu64, u53_check);
	break;
    case GPUTOP_PERFQUERY_COUNTER_DATA_FLOAT:
	gputop_string_append_printf(str, "%f",
		  read_float_oa_counter(query,
					counter,
					query->accumulator));
	break;
    case GPUTOP_PERFQUERY_COUNTER_DATA_DOUBLE:
	gputop_string_append_printf(str, "%f",
		  read_double_oa_counter(query,
					 counter,
					 query->accumulator));
	break;
    case GPUTOP_PERFQUERY_COUNTER_DATA_BOOL32:
	gputop_string_append_printf(str, "%s",
		  read_bool_oa_counter(query,
				       counter,
				       query->accumulator) ? "TRUE" : "FALSE");
	break;
    }
}

static void
forward_query_update(struct gputop_worker_query *query)
{
    struct gputop_perf_query *oa_query = query->oa_query;
    uint64_t delta;
    int i;

    //if (query->start_timestamp == 0)
	//gputop_web_console_warn("WW: Zero timestamp");

    gputop_string_t *str = gputop_string_new("{ \"method\": \"oa_query_update\", ");
    gputop_string_append_printf(str,
				"\"params\": [ { \"id\": %u, "
				"\"gpu_start\": %" PRIu64 ", "
				"\"gpu_end\": %" PRIu64 ", "
				"\"counters\": [ \n",
				query->id,
				query->start_timestamp,
				query->end_timestamp);
    delta = query->end_timestamp - query->start_timestamp;
    //printf("start ts = %"PRIu64" end ts = %"PRIu64" delta = %"PRIu64" agg. period =%"PRIu64"\n",
	//   start_timestamp, end_timestamp, delta, query->aggregation_period);

    for (i = 0; i < oa_query->n_counters; i++) {
	struct gputop_perf_query_counter *counter = &oa_query->counters[i];

	gputop_string_append(str, "  [ ");
	append_raw_oa_counter(str, oa_query, counter);

	if (counter->max) {
	    uint64_t max = counter->max(&devinfo, oa_query, oa_query->accumulator);
	    gputop_string_append_printf(str, ", %"PRIu64" ]", max);
	} else
	    gputop_string_append(str, ", 0 ]");

	if (i < (oa_query->n_counters - 1))
	    gputop_string_append(str, ",\n");
	else
	    gputop_string_append(str, "\n");
    }

    gputop_string_append_printf(str, "] } ], \"id\": %u }\n", next_rpc_id++);

    _gputop_web_worker_post(str->str);
    gputop_string_free(str, true);
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

    _gputop_web_worker_post(str->str);
    gputop_string_free(str, true);
}

static void
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

static void
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
		gputop_oa_accumulate_reports(oa_query, last, report);
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

static void
handle_i915_perf_message(int id, uint8_t *data, int len)
{
    struct gputop_worker_query *query;

    gputop_list_for_each(query, &open_queries, link) {

	if (query->id == id) {
	    if (query->oa_query)
		handle_oa_query_i915_perf_data(query, data, len);
	    return;
	}
    }
    gputop_web_console_log("received perf data for unknown query id: %d", id);
}


static const char *
counter_type_name(gputop_counter_type_t type)
{
    switch (type) {
    case GPUTOP_PERFQUERY_COUNTER_RAW:
	return "raw";
    case GPUTOP_PERFQUERY_COUNTER_DURATION_RAW:
	return "raw_duration";
    case GPUTOP_PERFQUERY_COUNTER_DURATION_NORM:
	return "normalized_duration";
    case GPUTOP_PERFQUERY_COUNTER_EVENT:
	return "event";
    case GPUTOP_PERFQUERY_COUNTER_THROUGHPUT:
	return "throughput";
    case GPUTOP_PERFQUERY_COUNTER_TIMESTAMP:
	return "timestamp";
    }
    assert_not_reached();
    return "unknown";
}

static const char *
data_counter_type_name(gputop_counter_data_type_t type)
{

    switch(type) {
    case GPUTOP_PERFQUERY_COUNTER_DATA_UINT64:
	return "uint64";
    case GPUTOP_PERFQUERY_COUNTER_DATA_UINT32:
	return "uint32";
    case GPUTOP_PERFQUERY_COUNTER_DATA_DOUBLE:
	return "double";
    case GPUTOP_PERFQUERY_COUNTER_DATA_FLOAT:
	return "float";
    case GPUTOP_PERFQUERY_COUNTER_DATA_BOOL32:
	return "bool";
    }
    assert_not_reached();
    return "unknown";
}

static void
append_i915_oa_query(gputop_string_t *str, struct gputop_perf_query *query)
{
    int i;

    gputop_string_append(str, "  {\n");
    gputop_string_append_printf(str, "    \"name\": \"%s\",\n", query->name);
    gputop_string_append_printf(str, "    \"guid\": \"%s\",\n", query->guid);
    gputop_string_append_printf(str, "    \"counters\": [\n");

    for (i = 0; i < query->n_counters; i++) {
	struct gputop_perf_query_counter *counter = &query->counters[i];

	switch (counter->type) {
	case GPUTOP_PERFQUERY_COUNTER_EVENT:
	case GPUTOP_PERFQUERY_COUNTER_DURATION_NORM:
	case GPUTOP_PERFQUERY_COUNTER_DURATION_RAW:
	case GPUTOP_PERFQUERY_COUNTER_THROUGHPUT:
	case GPUTOP_PERFQUERY_COUNTER_RAW:
	case GPUTOP_PERFQUERY_COUNTER_TIMESTAMP:
	    gputop_string_append_printf(str, "      { \"name\": \"%s\", ", counter->name);
	    gputop_string_append_printf(str, "\"index\": %u, ", i);
	    gputop_string_append_printf(str, "\"description\": \"%s\", ", counter->desc);
	    gputop_string_append_printf(str, "\"type\": \"%s\", ",
					counter_type_name(counter->type));
	    gputop_string_append_printf(str, "\"data_type\": \"%s\" ",
					data_counter_type_name(counter->data_type));
	    gputop_string_append(str, "}");
	    break;
	}

	if (i < (query->n_counters - 1))
	    gputop_string_append(str, ",\n");
	else
	    gputop_string_append(str, "\n");
    }

    gputop_string_append(str, "    ]\n");
    gputop_string_append(str, "  }\n");
}

static void
send_pb_message(int socket, ProtobufCMessage *pb_message)
{
    int len = protobuf_c_message_get_packed_size(pb_message);
    void *buf = malloc(len);

    protobuf_c_message_pack(pb_message, buf);

    gputop_web_console_log("sent from web worker len=%d\n", len);
    _gputop_web_socket_post(socket, buf, len);

    free(buf);
}

static void
update_features(Gputop__Features *features)
{
    gputop_string_t *str;
    bool n_queries = 0;
    int i;

    devinfo.devid = features->devinfo->devid;
    devinfo.n_eus = features->devinfo->n_eus;
    devinfo.n_eu_slices = features->devinfo->n_eu_slices;
    devinfo.eu_threads_count = features->devinfo->eu_threads_count;
    devinfo.n_eu_sub_slices = features->devinfo->n_eu_sub_slices;
    devinfo.subslice_mask = features->devinfo->subslice_mask;
    devinfo.slice_mask = features->devinfo->slice_mask;

    str = gputop_string_new("{ \"method\": \"features_notify\", \"params\": [ { \"oa_queries\": [\n");

    queries = gputop_hash_table_create(NULL, gputop_key_hash_string,
                                       gputop_key_string_equal);
    perf_oa_supported_query_guids = array_new(sizeof(char*), 1);

    if (features->fake_mode)
        gputop_oa_add_queries_bdw(&devinfo);
    else if (IS_HASWELL(devinfo.devid)) {
	_gputop_web_console_log("Adding Haswell queries\n");
	gputop_oa_add_queries_hsw(&devinfo);
    } else if (IS_BROADWELL(devinfo.devid)) {
	_gputop_web_console_log("Adding Broadwell queries\n");
	gputop_oa_add_queries_bdw(&devinfo);
    } else if (IS_CHERRYVIEW(devinfo.devid)) {
	_gputop_web_console_log("Adding Cherryview queries\n");
	gputop_oa_add_queries_chv(&devinfo);
    } else if (IS_SKYLAKE(devinfo.devid)) {
	_gputop_web_console_log("Adding Skylake queries\n");
	gputop_oa_add_queries_skl(&devinfo);
    } else
	assert_not_reached();

    for (i = 0; i < features->n_supported_oa_query_guids; i++)
    {
        struct gputop_perf_query *query = (gputop_hash_table_search(queries,
            features->supported_oa_query_guids[i]))->data;

        if (query->name) {
            if (n_queries)
                gputop_string_append(str, ",\n");
            append_i915_oa_query(str, query);
            n_queries++;
            array_append(perf_oa_supported_query_guids, &query->guid);
        }
    }

    gputop_string_append(str, "],\n");
    gputop_string_append_printf(str, " \"n_cpus\": %u,\n", features->n_cpus);
    gputop_string_append(str, " \"cpu_model\": \"");
    gputop_string_append_escaped(str, features->cpu_model);
    gputop_string_append(str, "\",\n");
    gputop_string_append(str, " \"kernel_release\": \"");
    gputop_string_append_escaped(str, features->kernel_release);
    gputop_string_append(str, "\",\n");
    gputop_string_append(str, " \"kernel_build\": \"");
    gputop_string_append_escaped(str, features->kernel_build);
    gputop_string_append(str, "\"\n");
    gputop_string_append(str, " } ] }\n");

    _gputop_web_worker_post(str->str);
    gputop_string_free(str, true);
}

static void
forward_log(Gputop__Log *log)
{
    gputop_string_t *str = gputop_string_new("{ \"method\": \"log\", \"params\": [ [\n");
    int i;

    for (i = 0; i < log->n_entries; i++) {
	Gputop__LogEntry *entry = log->entries[i];
	char *msg = entry->log_message;
	int j;

	/* XXX: hack to remove newlines which mess up the json format... */
	for (j = 0; msg[j]; j++) {
	    if (msg[j] == '\n')
		msg[j] = ' ';
	}

	gputop_string_append_printf(str, "    { \"level\": %d, \"message\": \"%s\" }",
				    entry->log_level,
				    msg);

	if (i < (log->n_entries - 1))
	    gputop_string_append(str, ",\n");
	else
	    gputop_string_append(str, "\n");

    }

    gputop_string_append_printf(str, "  ]\n], \"id\": %u }", next_rpc_id++);

    _gputop_web_worker_post(str->str);
    gputop_string_free(str, true);
}

static void
forward_close_notify(Gputop__CloseNotify *notify)
{
    gputop_string_t *str = gputop_string_new("{ \"method\": \"close_notify\", ");

    gputop_string_append_printf(str, "\"params\": [ %u ] }", notify->id);

    _gputop_web_worker_post(str->str);
    gputop_string_free(str, true);
}

static void
forward_fill_notify(Gputop__BufferFillNotify *notify)
{
    gputop_string_t *str = gputop_string_new("{ \"method\": \"fill_notify\", ");

    gputop_string_append_printf(str, "\"params\": [ %u, %u ] }", notify->query_id, notify->fill_percentage);

    _gputop_web_worker_post(str->str);
    gputop_string_free(str, true);
}

static void
post_empty_reply(const char *req_uuid)
{
    char *str = NULL;

    asprintf(&str, "{ \"params\": [ {} ], \"uuid\": \"%s\" }", req_uuid);

    _gputop_web_worker_post(str);

    free(str);
}

static void
forward_error(Gputop__Message *message)
{
    gputop_string_t *str = gputop_string_new("{ \"params\": [ { \"error\": \"");
    gputop_string_append_escaped(str, message->error);
    gputop_string_append(str, "\"} ], \"uuid\": \"");
    gputop_string_append(str, message->reply_uuid);
    gputop_string_append(str, "\" }");

    _gputop_web_worker_post(str->str);
    gputop_string_free(str, true);
}

static void
forward_ack(Gputop__Message *message)
{
    gputop_web_console_log("ACK: %s", message->reply_uuid);
    post_empty_reply(message->reply_uuid);
}

static void
handle_protobuf_message(uint8_t *data, int len)
{
    Gputop__Message *message;

    _gputop_web_console_log("PROTOBUF MESSAGE RECEIVED\n");

    message = (void *)protobuf_c_message_unpack(&gputop__message__descriptor,
						NULL, /* default allocator */
						len,
						data);

    switch (message->cmd_case) {
    case GPUTOP__MESSAGE__CMD_ERROR:
	/* FIXME: check if the message originated here */
	forward_error(message);
	break;
    case GPUTOP__MESSAGE__CMD_ACK:
	/* FIXME: check if the message originated here */
	forward_ack(message);
	break;
    case GPUTOP__MESSAGE__CMD_FEATURES:
	_gputop_web_console_log("Features message\n");
	update_features(message->features);
	break;
    case GPUTOP__MESSAGE__CMD_LOG:
	_gputop_web_console_log("Logs message\n");
	forward_log(message->log);
	break;
    case GPUTOP__MESSAGE__CMD_CLOSE_NOTIFY:
	forward_close_notify(message->close_notify);
	break;
    case GPUTOP__MESSAGE__CMD_FILL_NOTIFY:
	forward_fill_notify(message->fill_notify);
	break;
    case GPUTOP__MESSAGE__CMD__NOT_SET:
	assert_not_reached();
    }

    free(message);
}

void EMSCRIPTEN_KEEPALIVE
gputop_webworker_on_test(const char *msg, float val, const char *req_uuid)
{
    gputop_web_console_log("test message from ui: (%s, %f)\n", msg, val);

    post_empty_reply(req_uuid);
}

void EMSCRIPTEN_KEEPALIVE
gputop_webworker_on_open_oa_query(uint32_t id,
				  const char *guid,
				  int period_exponent,
				  unsigned overwrite,
				  uint32_t aggregation_period,
				  unsigned live_updates,
				  const char *req_uuid)
{
    Gputop__Request req = GPUTOP__REQUEST__INIT;
    Gputop__OpenQuery open = GPUTOP__OPEN_QUERY__INIT;
    Gputop__OAQueryInfo oa_query = GPUTOP__OAQUERY_INFO__INIT;
    struct gputop_worker_query *query = malloc(sizeof(*query));
    struct gputop_hash_entry *entry = NULL;

    memset(query, 0, sizeof(*query));

    gputop_web_console_log("on_open_oa_query set=%s, exponent=%d, overwrite=%d live=%s agg_period=%"PRIu32"\n",
			   guid, period_exponent, overwrite,
			   live_updates ? "true" : "false",
			   aggregation_period);


    open.id = id;
    open.type_case = GPUTOP__OPEN_QUERY__TYPE_OA_QUERY;
    open.oa_query = &oa_query;
    open.live_updates = live_updates;
    open.overwrite = overwrite;

    oa_query.guid = (char*)guid;
    oa_query.period_exponent = period_exponent;

    req.uuid = (char*)req_uuid;
    req.req_case = GPUTOP__REQUEST__REQ_OPEN_QUERY;
    req.open_query = &open;

    send_pb_message(socket, &req.base);

    memset(query, 0, sizeof(*query));
    query->id = id;
    query->aggregation_period = aggregation_period;

    entry = gputop_hash_table_search(queries, guid);
    if (entry != NULL)
        query->oa_query = entry->data;
    else
        return;

    gputop_list_insert(open_queries.prev, &query->link);
}

void EMSCRIPTEN_KEEPALIVE
gputop_webworker_on_close_oa_query(uint32_t id, char *req_uuid)
{
    Gputop__Request req = GPUTOP__REQUEST__INIT;
    struct gputop_worker_query *query;

    gputop_web_console_log("on_close_oa_query(%d)\n", id);

    gputop_list_for_each(query, &open_queries, link) {
	if (query->id == id) {
	    req.uuid = req_uuid;
	    req.req_case = GPUTOP__REQUEST__REQ_CLOSE_QUERY;
	    req.close_query = id;

	    send_pb_message(socket, &req.base);

	    gputop_list_remove(&query->link);
	    free(query);
	    return;
	}
    }

    gputop_web_console_warn("webworker: requested to close unknown query ID: %d\n", id);
}

void EMSCRIPTEN_KEEPALIVE
gputop_webworker_on_open_tracepoint(uint32_t id,
				    int pid,
				    int cpu,
				    const char *system,
				    const char *event,
				    unsigned overwrite,
				    unsigned live_updates,
				    const char *req_uuid)
{
    Gputop__Request req = GPUTOP__REQUEST__INIT;
    Gputop__OpenQuery open = GPUTOP__OPEN_QUERY__INIT;
    Gputop__TraceInfo trace = GPUTOP__TRACE_INFO__INIT;
    struct gputop_worker_query *query = malloc(sizeof(*query));

    gputop_web_console_log("on_open_trace pid=%d cpu=%d %s:%s overwrite=%s live=%s\n",
			   pid, cpu, system, event, overwrite ? "true" : "false", live_updates ? "true" : "false");

    open.id = id;
    open.type_case = GPUTOP__OPEN_QUERY__TYPE_TRACE;
    open.trace = &trace;
    open.live_updates = live_updates;
    open.overwrite = overwrite;

    trace.pid = pid;
    trace.cpu = cpu;
    trace.system = (char*)system;
    trace.event = (char*)event;

    req.uuid = (char*)req_uuid;
    req.req_case = GPUTOP__REQUEST__REQ_OPEN_QUERY;
    req.open_query = &open;

    send_pb_message(socket, &req.base);

    memset(query, 0, sizeof(*query));
    query->id = id;
    gputop_list_insert(open_queries.prev, &query->link);
}

void EMSCRIPTEN_KEEPALIVE
gputop_webworker_on_open_generic(uint32_t id,
				 int pid,
				 int cpu,
				 unsigned type,	    /* XXX: emscripten apparently
						     * gets upset with uint64_t args!?
						     * might just need to test with
						     * a more recent version.*/
				 unsigned config,
				 unsigned overwrite,
				 unsigned live_updates,
				 const char *req_uuid)
{
    Gputop__Request req = GPUTOP__REQUEST__INIT;
    Gputop__OpenQuery open = GPUTOP__OPEN_QUERY__INIT;
    Gputop__GenericEventInfo generic = GPUTOP__GENERIC_EVENT_INFO__INIT;
    struct gputop_worker_query *query = malloc(sizeof(*query));

    gputop_web_console_log("on_open_generic pid=%d, cpu=%d, type=%u config=%u overwrite=%d live=%s\n",
			   pid, cpu, type, config, overwrite, live_updates ? "true" : "false");

    open.id = id;
    open.type_case = GPUTOP__OPEN_QUERY__TYPE_GENERIC;
    open.generic = &generic;
    open.live_updates = live_updates;
    open.overwrite = overwrite;

    generic.pid = pid;
    generic.cpu = cpu;
    generic.type = type;
    generic.config = config;

    req.uuid = (char*)req_uuid;
    req.req_case = GPUTOP__REQUEST__REQ_OPEN_QUERY;
    req.open_query = &open;

    send_pb_message(socket, &req.base);

    memset(query, 0, sizeof(*query));
    query->id = id;
    gputop_list_insert(open_queries.prev, &query->link);
}

/* Messages from the device... */
static void
gputop_websocket_onmessage(int socket, uint8_t *data, int len, void *user_data)
{
    //gputop_web_console_log("onmessage len=%d\n", len);

    /* FIXME: don't use hardcoded enum values here! */
    switch (data[0]) {
    case 1: { /* WS_MESSAGE_PERF */
	uint32_t id = *((uint32_t *)(data + 4));

	//gputop_web_console_log("perf message, len=%d, id=%d\n", len, id);
	handle_perf_message(id, data + 8, len - 8);
        break;
    }
    case 2: /* WS_MESSAGE_PROTOBUF */
	handle_protobuf_message(data + 8, len - 8);
        break;
    case 3: {/* WS_MESSAGE_I915_PERF */
	uint32_t id = *((uint32_t *)(data + 4));

	//gputop_web_console_log("perf message, len=%d, id=%d\n", len, id);
	handle_i915_perf_message(id, data + 8, len - 8);
        break;
    }
    }
}

static void
gputop_websocket_onerror(int socket, void *user_data)
{
    gputop_web_console_log("websocket error\n");
}

static void
gputop_websocket_onclose(int socket, void *user_data)
{
    gputop_web_console_log("websocket closed\n");
}

static void
gputop_websocket_onopen(int socket, void *user_data)
{
    Gputop__Request req = GPUTOP__REQUEST__INIT;
    char uuid_str[64];
    uuid_t uuid;

    uuid_generate(uuid);
    uuid_unparse(uuid, uuid_str);

    req.uuid = uuid_str;
    req.req_case = GPUTOP__REQUEST__REQ_GET_FEATURES;
    req.get_features = true;

    send_pb_message(socket, &req.base);
}

void EMSCRIPTEN_KEEPALIVE
gputop_webworker_init(void)
{
    const char *host = _gputop_web_get_location_host();
    char *url = NULL;

    asprintf(&url, "ws://%s/gputop/", host);

    gputop_list_init(&open_queries);

    socket = _gputop_web_socket_new(url, "binary");
    _gputop_web_socket_set_onopen(socket,
				  gputop_websocket_onopen,
				  NULL); /* user data */
    _gputop_web_socket_set_onerror(socket,
				   gputop_websocket_onerror,
				   NULL); /* user data */
    _gputop_web_socket_set_onclose(socket,
				   gputop_websocket_onclose,
				   NULL); /* user data */
    _gputop_web_socket_set_onmessage(socket,
				     gputop_websocket_onmessage,
				     NULL); /* user data */
}
