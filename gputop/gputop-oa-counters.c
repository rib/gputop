#include <gputop-oa-counters.h>

void
gputop_oa_accumulate_uint32(const uint32_t *report0,
                  const uint32_t *report1,
                  uint64_t *accumulator)
{
   *accumulator += (uint32_t)(*report1 - *report0);
}

void
gputop_oa_accumulate_uint40(int a_index,
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
gputop_oa_accumulate_reports(struct gputop_perf_query *query,
		       const uint8_t *report0,
		       const uint8_t *report1)
{
    uint64_t *accumulator = query->accumulator;
    const uint32_t *start = (const uint32_t *)report0;
    const uint32_t *end = (const uint32_t *)report1;
    int idx = 0;
    int i;

    assert(report0 != report1);

    switch (query->perf_oa_format) {
    case I915_OA_FORMAT_A32u40_A4u32_B8_C8:
	gputop_oa_accumulate_uint32(start + 1, end + 1, accumulator + idx++); /* timestamp */
	gputop_oa_accumulate_uint32(start + 3, end + 3, accumulator + idx++); /* clock */

	/* 32x 40bit A counters... */
	for (i = 0; i < 32; i++)
	    gputop_oa_accumulate_uint40(i, start, end, accumulator + idx++);

	/* 4x 32bit A counters... */
	for (i = 0; i < 4; i++)
	    gputop_oa_accumulate_uint32(start + 36 + i, end + 36 + i,
            accumulator + idx++);

	/* 8x 32bit B counters + 8x 32bit C counters... */
	for (i = 0; i < 16; i++)
	    gputop_oa_accumulate_uint32(start + 48 + i, end + 48 + i,
            accumulator + idx++);
	break;

    case I915_OA_FORMAT_A45_B8_C8:
	gputop_oa_accumulate_uint32(start + 1, end + 1, accumulator); /* timestamp */

	for (i = 0; i < 61; i++)
	    gputop_oa_accumulate_uint32(start + 3 + i, end + 3 + i,
            accumulator + 1 + i);
	break;
    default:
	assert(0);
    }
}
