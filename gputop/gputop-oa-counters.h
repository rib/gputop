#include <stdint.h>
#include <assert.h>
#include "gputop-perf.h"

void gputop_oa_accumulate_uint32(const uint32_t *report0,
            const uint32_t *report1, uint64_t *accumulator);

void gputop_oa_accumulate_uint40(int a_index, const uint32_t *report0,
            const uint32_t *report1, uint64_t *accumulator);

void gputop_oa_accumulate_reports(struct gputop_perf_query *query,
            const uint8_t *report0, const uint8_t *report1);
