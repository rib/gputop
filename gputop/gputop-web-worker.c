
#include <stdint.h>
#include <stdlib.h>
#include <inttypes.h>
#include <string.h>
#include <assert.h>

#include <emscripten.h>

#include "intel_chipset.h"

#include <gputop-perf.h>
#include <gputop-web.h>
#include <gputop-string.h>
#include <gputop.pb-c.h>

#include "oa-hsw.h"
#include "oa-bdw.h"

enum perf_record_type {
    PERF_RECORD_LOST = 2,
    PERF_RECORD_THROTTLE = 6,
    PERF_RECORD_UNTHROTTLE = 7,
    PERF_RECORD_SAMPLE = 9,
    PERF_RECORD_DEVICE = 13,
};

struct perf_event_header {
    uint32_t type;
    uint16_t misc;
    uint16_t size;
};

#if 0
#define I915_OA_FORMAT_A13_HSW		0
#define I915_OA_FORMAT_A29_HSW		1
#define I915_OA_FORMAT_A13_B8_C8_HSW	2
#define I915_OA_FORMAT_B4_C8_HSW	4
#define I915_OA_FORMAT_A45_B8_C8_HSW	5
#define I915_OA_FORMAT_B4_C8_A16_HSW	6
#define I915_OA_FORMAT_C4_B8_HSW	7

#define I915_OA_FORMAT_A12_BDW		0
#define I915_OA_FORMAT_A12_B8_C8_BDW	2
#define I915_OA_FORMAT_A36_B8_C8_BDW	5
#define I915_OA_FORMAT_C4_B8_BDW	7
#endif

typedef struct _drm_i915_oa_event_header {
	uint32_t type;
	uint32_t padding;
} drm_i915_oa_event_header_t;

enum drm_i915_oa_event_type {

	/*
	 * struct {
	 *	struct perf_event_header	header;
	 *	drm_i915_oa_event_header_t	i915_oa_header;
	 * };
	 */
	I915_OA_RECORD_BUFFER_OVERFLOW		= 1,

	/*
	 * struct {
	 *	struct perf_event_header	header;
	 *	drm_i915_oa_event_header_t	i915_oa_header;
	 * };
	 */
	I915_OA_RECORD_REPORT_LOST		= 2,

	I915_OA_RECORD_MAX,			/* non-ABI */
};

/* Samples read from the perf circular buffer */
struct oa_perf_sample {
   struct perf_event_header header;
   uint32_t raw_size;
   uint8_t raw_data[];
};


static struct gputop_devinfo devinfo;


struct gputop_perf_query perf_queries[MAX_PERF_QUERIES];

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
       assert(0);
}

uint64_t
read_uint64_oa_counter(const struct gputop_perf_query *query,
		       const struct gputop_perf_query_counter *counter,
		       uint64_t *accumulator)
{
    return counter->oa_counter_read_uint64(&gputop_devinfo, query, accumulator);
}

uint32_t
read_uint32_oa_counter(const struct gputop_perf_query *query,
		       const struct gputop_perf_query_counter *counter,
		       uint64_t *accumulator)
{
    assert(0);
    //return counter->oa_counter_read_uint32(&gputop_devinfo, query, accumulator);
}

bool
read_bool_oa_counter(const struct gputop_perf_query *query,
		     const struct gputop_perf_query_counter *counter,
		     uint64_t *accumulator)
{
    assert(0);
    //return counter->oa_counter_read_bool(&gputop_devinfo, query, accumulator);
}

double
read_double_oa_counter(const struct gputop_perf_query *query,
		       const struct gputop_perf_query_counter *counter,
		       uint64_t *accumulator)
{
    assert(0);
    //return counter->oa_counter_read_double(&gputop_devinfo, query, accumulator);
}

float
read_float_oa_counter(const struct gputop_perf_query *query,
		      const struct gputop_perf_query_counter *counter,
		      uint64_t *accumulator)
{
    return counter->oa_counter_read_float(&gputop_devinfo, query, accumulator);
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
update_web_ui(struct gputop_perf_query *query)
{
    int j;

    gputop_string_t *str = gputop_string_new("{\n");


    for (j = 0; j < query->n_counters; j++) {
	struct gputop_perf_query_counter *counter = &query->counters[j];

	switch (counter->type) {
	case GPUTOP_PERFQUERY_COUNTER_EVENT:
	case GPUTOP_PERFQUERY_COUNTER_DURATION_NORM:
	case GPUTOP_PERFQUERY_COUNTER_DURATION_RAW:
	case GPUTOP_PERFQUERY_COUNTER_THROUGHPUT:
	case GPUTOP_PERFQUERY_COUNTER_RAW:
	case GPUTOP_PERFQUERY_COUNTER_TIMESTAMP:
	    gputop_string_append_printf(str, "  { %s: ", counter->name);
	    append_raw_oa_counter(str, query, counter);
	    gputop_string_append(str, "}\n");
	    break;
	}

#if 0
	if (counter->max) {
	    uint64_t max = counter->max(&gputop_devinfo, query, query->accumulator);
	    print_range_oa_counter(win, y, 60, query, counter, max);
	}
#endif
    }

    gputop_string_append(str, "}\n");

    _gputop_web_worker_post(str->str);
    gputop_string_free(str, true);
}

static uint8_t *last_record = NULL;

static void
handle_perf_message(uint8_t *data, int len)
{
    const struct perf_event_header *header;
    uint8_t *last;
    uint64_t tail;
    uint64_t last_tail;
    char *str;
    //int i = 0;

    for (header = (void *)data;
	 (uint8_t *)header < (data + len);
	 header = (void *)(((uint8_t *)header) + header->size))
    {
#if 0
	asprintf(&str, "header[%d] = %p size=%d type = %d", i, header, header->size, header->type);
	_gputop_web_console_log(str);
	free(str);
	str = NULL;

	i++;
	if (i > 200) {
	    _gputop_web_console_log("perf message too large!\n");
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
	    asprintf(&str, "i915_oa: Lost %" PRIu64 " events\n", lost->n_lost);
	    break;
	}

	case PERF_RECORD_THROTTLE:
	    str = strdup("i915_oa: Sampling has been throttled\n");
	    break;

	case PERF_RECORD_UNTHROTTLE:
	    str = strdup("i915_oa: Sampling has been unthrottled\n");
	    break;

	case PERF_RECORD_DEVICE: {
	    struct i915_oa_event {
		struct perf_event_header header;
		drm_i915_oa_event_header_t oa_header;
	    } *oa_event = (void *)header;

	    switch (oa_event->oa_header.type) {
	    case I915_OA_RECORD_BUFFER_OVERFLOW:
		str = strdup("i915_oa: OA buffer overflow\n");
		break;
	    case I915_OA_RECORD_REPORT_LOST:
		str = strdup("i915_oa: OA report lost\n");
		break;
	    }
	    break;
	}

	case PERF_RECORD_SAMPLE: {
	    struct oa_perf_sample *perf_sample = (struct oa_perf_sample *)header;
	    uint8_t *report = (uint8_t *)perf_sample->raw_data;

	    //str = strdup("SAMPLE\n");
	    if (last)
		gputop_perf_accumulate(&perf_queries[GPUTOP_PERF_QUERY_3D_BASIC], last, report);

	    last = report;
	    last_tail = tail;
	    break;
	}

	default:
	    asprintf(&str, "i915_oa: Spurious header type = %d\n", header->type);
	}

	if (str) {
	    _gputop_web_console_log(str);
	    free(str);
	    str = NULL;
	}
    }

    //update_web_ui(&perf_queries[GPUTOP_PERF_QUERY_3D_BASIC]);
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
    assert(0);
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
    assert(0);
    return "unknown";
}

static void
advertise_query(struct gputop_perf_query *query)
{
    int i;

    gputop_string_t *str = gputop_string_new("{ \"queries\": [\n");

    gputop_string_append(str, "  {\n");
    gputop_string_append_printf(str, "    \"name\": \"%s\",\n", query->name);
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

    gputop_string_append(str, "]}\n");

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
    case GPUTOP__MESSAGE__CMD_DEVINFO:
	_gputop_web_console_log("DevInfo message\n");

	devinfo.devid = message->devinfo->devid;
	devinfo.n_eus = message->devinfo->n_eus;
	devinfo.n_eu_slices = message->devinfo->n_eu_slices;
	devinfo.n_eu_sub_slices = message->devinfo->n_eu_sub_slices;
	devinfo.n_samplers = message->devinfo->n_samplers;

	if (IS_HASWELL(devinfo.devid)) {
	    _gputop_web_console_log("Adding Haswell queries\n");
	    gputop_oa_add_render_basic_counter_query_hsw();
	    advertise_query(&perf_queries[GPUTOP_PERF_QUERY_3D_BASIC]);

	    gputop_oa_add_compute_basic_counter_query_hsw();
	    gputop_oa_add_compute_extended_counter_query_hsw();
	    gputop_oa_add_memory_reads_counter_query_hsw();
	    gputop_oa_add_memory_writes_counter_query_hsw();
	    gputop_oa_add_sampler_balance_counter_query_hsw();
	} else if (IS_BROADWELL(devinfo.devid)) {
	    _gputop_web_console_log("Adding Broadwell queries\n");
	    gputop_oa_add_render_basic_counter_query_bdw();
	} else
	    assert(0);

	break;
    case GPUTOP__MESSAGE__CMD_ADD_TRACE:
	_gputop_web_console_log("AddTrace message\n");
	break;
    case GPUTOP__MESSAGE__CMD__NOT_SET:
	assert(0);
    }

    free(message);
}

void EMSCRIPTEN_KEEPALIVE
gputop_websocket_onmessage(uint8_t *data, int len)
{
#if 0
    char *str = NULL;

    asprintf(&str, "onmessage len=%d\n", len);
    _gputop_web_console_log(str);
    free(str);
    str = NULL;
#endif

    if (data[0] == 1)
	handle_perf_message(data + 8, len - 8);
    else
	handle_protobuf_message(data + 8, len - 8);
}
