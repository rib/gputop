
#include <stdint.h>
#include <stdlib.h>
#include <inttypes.h>
#include <string.h>
#include <assert.h>

#include <emscripten.h>

#include "intel_chipset.h"

#include <gputop-perf.h>
#include <gputop-web.h>

struct intel_device {
    uint32_t device;
    uint32_t subsystem_device;
    uint32_t subsystem_vendor;
};

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
static struct intel_device intel_dev;



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

   if (IS_BROADWELL(intel_dev.device) &&
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

   } else if (IS_HASWELL(intel_dev.device) &&
	      query->perf_oa_format == I915_OA_FORMAT_A45_B8_C8_HSW)
   {
       accumulate_uint32(start + 1, end + 1, accumulator); /* timestamp */

       for (i = 0; i < 61; i++)
	   accumulate_uint32(start + 3 + i, end + 3 + i, accumulator + 1 + i);
   } else
       assert(0);
}

static uint8_t *last_record = NULL;

void EMSCRIPTEN_KEEPALIVE
gputop_websocket_onmessage(uint8_t *data, int len)
{
    const struct perf_event_header *header;
    char *str = NULL;
    uint8_t *last;
    uint64_t tail;
    uint64_t last_tail;
    //int i = 0;

    asprintf(&str, "onmessage len=%d\n", len);
    _gputop_web_console_log(str);
    free(str);
    str = NULL;

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
	    //if (last)
		//current_user->sample(query, last, report);

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

}
