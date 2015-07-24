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
#include <inttypes.h>
#include <string.h>

#include <emscripten.h>

#include <intel_chipset.h>
#include <i915_oa_drm.h>

#include <gputop-perf.h>
#include <gputop-web.h>
#include <gputop-string.h>
#include <gputop.pb-c.h>

#include "oa-hsw.h"
#include "oa-bdw.h"

struct gputop_worker_query {
    int id;
    int aggregation_period;
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

enum perf_record_type {
    PERF_RECORD_LOST = 2,
    PERF_RECORD_THROTTLE = 5,
    PERF_RECORD_UNTHROTTLE = 6,
    PERF_RECORD_SAMPLE = 9,
    PERF_RECORD_DEVICE = 13,
};

struct perf_event_header {
    uint32_t type;
    uint16_t misc;
    uint16_t size;
};

/* Samples read from the perf circular buffer */
struct oa_perf_sample {
   struct perf_event_header header;
   uint32_t raw_size;
   uint8_t raw_data[];
};


static struct gputop_devinfo devinfo;
static int socket = 0;

static int next_rpc_id = 1;

struct gputop_perf_query perf_queries[MAX_PERF_QUERIES];

static void __attribute__((noreturn))
assert_not_reached(void)
{
    gputop_web_console_assert(0, "code should not be reached");
}

static void
accumulate_uint32(const uint32_t *report0,
                  const uint32_t *report1,
                  uint64_t *accumulator)
{
   *accumulator += (uint32_t)(*report1 - *report0);
}

static void
accumulate_uint40(int a_index,
		  const uint32_t *report0,
		  const uint32_t *report1,
		  uint64_t *accumulator)
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

    *accumulator += delta;
}

void
gputop_perf_accumulate(struct gputop_perf_query *query,
		       const uint8_t *report0,
		       const uint8_t *report1)
{
   uint64_t *accumulator = query->accumulator;
   const uint32_t *start = (const uint32_t *)report0;
   const uint32_t *end = (const uint32_t *)report1;
   int i;

   if (IS_BROADWELL(devinfo.devid) &&
       query->perf_oa_format == I915_OA_FORMAT_A36_B8_C8_BDW)
   {
       int idx = 0;

       accumulate_uint32(start + 1, end + 1, accumulator + idx++); /* timestamp */
       accumulate_uint32(start + 3, end + 3, accumulator + idx++); /* clock */

       /* 32x 40bit A counters... */
       for (i = 0; i < 32; i++)
	   accumulate_uint40(i, start, end, accumulator + idx++);

       /* 4x 32bit A counters... */
       for (i = 0; i < 4; i++)
	   accumulate_uint32(start + 36 + i, end + 36 + i, accumulator + idx++);

       /* 8x 32bit B counters + 8x 32bit C counters... */
       for (i = 0; i < 16; i++)
	   accumulate_uint32(start + 48 + i, end + 48 + i, accumulator + idx++);

   } else if (IS_HASWELL(devinfo.devid) &&
	      query->perf_oa_format == I915_OA_FORMAT_A45_B8_C8_HSW)
   {
       accumulate_uint32(start + 1, end + 1, accumulator); /* timestamp */

       for (i = 0; i < 61; i++)
	   accumulate_uint32(start + 3 + i, end + 3 + i, accumulator + 1 + i);
   } else
       assert_not_reached();
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

uint64_t
read_report_timestamp(const uint32_t *report)
{
   uint64_t timestamp = report[1];

   /* The least significant timestamp bit represents 80ns */
   timestamp *= 80;

   return timestamp;
}

static void
append_raw_oa_counter(gputop_string_t *str,
		      struct gputop_perf_query *query,
		      const struct gputop_perf_query_counter *counter)
{
    switch(counter->data_type) {
    case GPUTOP_PERFQUERY_COUNTER_DATA_UINT32:
	gputop_string_append_printf(str, "%" PRIu32,
		  read_uint32_oa_counter(query,
					 counter,
					 query->accumulator));
	break;
    case GPUTOP_PERFQUERY_COUNTER_DATA_UINT64:
	gputop_string_append_printf(str, "%" PRIu64,
		  read_uint64_oa_counter(query,
					 counter,
					 query->accumulator));
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
    int i;

    gputop_string_t *str = gputop_string_new("{ \"method\": \"oa_query_update\", ");
    gputop_string_append_printf(str,
				"\"params\": [ { \"id\": %u, "
				"\"gpu_start\": %" PRIu64 ", "
				"\"gpu_end\": %" PRIu64 ", "
				"\"counters\": [ \n",
				query->id,
				query->start_timestamp,
				query->end_timestamp);

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
handle_query_perf_data(struct gputop_worker_query *query, uint8_t *data, int len)
{
    struct gputop_perf_query *oa_query = query->oa_query;
    const struct perf_event_header *header;
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
	case PERF_RECORD_LOST: {
	    struct {
		struct perf_event_header header;
		uint64_t id;
		uint64_t n_lost;
	    } *lost = (void *)header;
	    gputop_web_console_log("i915_oa: Lost %" PRIu64 " events\n", lost->n_lost);
	    break;
	}

	case PERF_RECORD_THROTTLE:
	    gputop_web_console_log("i915_oa: Sampling has been throttled\n");
	    break;

	case PERF_RECORD_UNTHROTTLE:
	    gputop_web_console_log("i915_oa: Sampling has been unthrottled\n");
	    break;

	case PERF_RECORD_DEVICE: {
	    struct i915_oa_event {
		struct perf_event_header header;
		i915_oa_event_header_t oa_header;
	    } *oa_event = (void *)header;

	    switch (oa_event->oa_header.type) {
	    case I915_OA_RECORD_BUFFER_OVERFLOW:
		gputop_web_console_log("i915_oa: OA buffer overflow\n");
		break;
	    case I915_OA_RECORD_REPORT_LOST:
		gputop_web_console_log("i915_oa: OA report lost\n");
		break;
	    }
	    break;
	}

	case PERF_RECORD_SAMPLE: {
	    struct oa_perf_sample *perf_sample = (struct oa_perf_sample *)header;
	    uint8_t *report = (uint8_t *)perf_sample->raw_data;
            uint64_t timestamp = read_report_timestamp((uint32_t *)report);

            if (timestamp >= end_timestamp) {
		forward_query_update(query);
		memset(oa_query->accumulator, 0, sizeof(oa_query->accumulator));
		query->start_timestamp = timestamp;
		end_timestamp += query->aggregation_period * 1000000;
	    }

	    //str = strdup("SAMPLE\n");
	    if (last) {
		query->end_timestamp = timestamp;
		gputop_perf_accumulate(oa_query, last, report);
	    } else {
		memset(oa_query->accumulator, 0, sizeof(oa_query->accumulator));
		query->start_timestamp = timestamp;
		end_timestamp = timestamp + (query->aggregation_period * 1000000);
	    }

	    last = report;
	    break;
	}

	default:
	    gputop_web_console_log("i915_oa: Spurious header type = %d\n", header->type);
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
handle_perf_message(int id, uint8_t *data, int len)
{
    struct gputop_worker_query *query;

    gputop_list_for_each(query, &open_queries, link) {

	if (query->id == id) {
	    handle_query_perf_data(query, data, len);
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
    gputop_string_append_printf(str, "    \"metric_set\": %d,\n", query->perf_oa_metrics_set);
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

    devinfo.devid = features->devinfo->devid;
    devinfo.n_eus = features->devinfo->n_eus;
    devinfo.n_eu_slices = features->devinfo->n_eu_slices;
    devinfo.n_eu_sub_slices = features->devinfo->n_eu_sub_slices;
    devinfo.n_samplers = features->devinfo->n_samplers;

    str = gputop_string_new("{ \"method\": \"features_notify\", \"params\": [ { \"oa_queries\": [\n");

    if (IS_HASWELL(devinfo.devid)) {
	_gputop_web_console_log("Adding Haswell queries\n");
	gputop_oa_add_render_basic_counter_query_hsw();
	append_i915_oa_query(str, &perf_queries[GPUTOP_PERF_QUERY_3D_BASIC]);

	gputop_oa_add_compute_basic_counter_query_hsw();
	gputop_oa_add_compute_extended_counter_query_hsw();
	gputop_oa_add_memory_reads_counter_query_hsw();
	gputop_oa_add_memory_writes_counter_query_hsw();
	gputop_oa_add_sampler_balance_counter_query_hsw();
    } else if (IS_BROADWELL(devinfo.devid)) {
	_gputop_web_console_log("Adding Broadwell queries\n");
	gputop_oa_add_render_basic_counter_query_bdw();
    } else
	assert_not_reached();

    gputop_string_append(str, "] } ] }\n");

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
handle_protobuf_message(uint8_t *data, int len)
{
    Gputop__Message *message;

    _gputop_web_console_log("PROTOBUF MESSAGE RECEIVED\n");

    message = (void *)protobuf_c_message_unpack(&gputop__message__descriptor,
						NULL, /* default allocator */
						len,
						data);

    switch (message->cmd_case) {
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
    case GPUTOP__MESSAGE__CMD__NOT_SET:
	assert_not_reached();
    }

    free(message);
}

void EMSCRIPTEN_KEEPALIVE
gputop_webworker_on_test(const char *msg, float val)
{
    gputop_web_console_log("test message from ui: (%s, %f)\n", msg, val);
}

void EMSCRIPTEN_KEEPALIVE
gputop_webworker_on_open_oa_query(int id,
				  int perf_metric_set,
				  int period_exponent,
				  bool overwrite,
				  int aggregation_period)
{
    Gputop__Request req = GPUTOP__REQUEST__INIT;
    Gputop__OpenQuery open = GPUTOP__OPEN_QUERY__INIT;
    Gputop__OAQueryInfo oa_query = GPUTOP__OAQUERY_INFO__INIT;
    struct gputop_worker_query *query = malloc(sizeof(*query));

    gputop_web_console_log("on_open_oa_query set=%d, exponent=%d, overwrite=%d\n",
			   perf_metric_set, period_exponent, overwrite);

    open.id = id;
    open.type_case = GPUTOP__OPEN_QUERY__TYPE_OA_QUERY;
    open.oa_query = &oa_query;

    oa_query.metric_set = perf_metric_set;
    oa_query.period_exponent = period_exponent;
    oa_query.overwrite = overwrite;

    req.req_case = GPUTOP__REQUEST__REQ_OPEN_QUERY;
    req.open_query = &open;

    send_pb_message(socket, &req.base);

    memset(query, 0, sizeof(*query));
    query->id = id;
    query->aggregation_period = aggregation_period;
    query->oa_query = &perf_queries[perf_metric_set];
    gputop_list_insert(open_queries.prev, &query->link);
}

void EMSCRIPTEN_KEEPALIVE
gputop_webworker_on_close_oa_query(int id)
{
    Gputop__Request req = GPUTOP__REQUEST__INIT;
    struct gputop_worker_query *query;

    gputop_web_console_log("on_close_oa_query(%d)\n", id);

    gputop_list_for_each(query, &open_queries, link) {
	if (query->id == id) {

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

/* Messages from the device... */
static void
gputop_websocket_onmessage(int socket, uint8_t *data, int len, void *user_data)
{
    //gputop_web_console_log("onmessage len=%d\n", len);

    if (data[0] == 1) {
	int id = data[1];
	//gputop_web_console_log("perf message, len=%d, id=%d\n", len, id);
	handle_perf_message(id, data + 8, len - 8);
    } else
	handle_protobuf_message(data + 8, len - 8);
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
