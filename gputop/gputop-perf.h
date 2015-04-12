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

#ifndef _GPUTOP_PERF_H_
#define _GPUTOP_PERF_H_

typedef enum {
    GPUTOP_PERFQUERY_COUNTER_DATA_UINT64,
    GPUTOP_PERFQUERY_COUNTER_DATA_UINT32,
    GPUTOP_PERFQUERY_COUNTER_DATA_DOUBLE,
    GPUTOP_PERFQUERY_COUNTER_DATA_FLOAT,
    GPUTOP_PERFQUERY_COUNTER_DATA_BOOL32,
} gputop_counter_data_type_t;

typedef enum {
    GPUTOP_PERFQUERY_COUNTER_RAW,
    GPUTOP_PERFQUERY_COUNTER_DURATION_RAW,
    GPUTOP_PERFQUERY_COUNTER_DURATION_NORM,
    GPUTOP_PERFQUERY_COUNTER_EVENT,
    GPUTOP_PERFQUERY_COUNTER_THROUGHPUT,
    GPUTOP_PERFQUERY_COUNTER_TIMESTAMP,
} gputop_counter_type_t;

struct gputop_oa_counter;

struct gputop_perf_query_counter
{
   const char *name;
   const char *desc;
   gputop_counter_type_t type;
   gputop_counter_data_type_t data_type;
   uint64_t max;

   struct gputop_oa_counter *oa_counter;
};

struct gputop_perf_query
{
   const char *name;
   struct gputop_perf_query_counter *counters;
   int n_counters;

   int perf_oa_metrics_set;
   int perf_oa_format;
   int perf_raw_size;

   struct gputop_oa_counter *oa_counters;
   int n_oa_counters;
};


#define MAX_RAW_OA_COUNTERS 62

extern const struct gputop_perf_query *gputop_current_perf_query;
extern char *gputop_perf_error;

typedef enum gputop_perf_query_type
{
    GPUTOP_PERF_QUERY_BASIC,
    GPUTOP_PERF_QUERY_3D_BASIC,
} gputop_perf_query_type_t;

bool gputop_perf_overview_open(gputop_perf_query_type_t query_type);
void gputop_perf_overview_close(void);

void gputop_perf_accumulator_clear(void);
void gputop_perf_accumulate(const struct gputop_perf_query *query,
			    const void *report0,
			    const void *report1,
			    uint64_t *accumulator);

void gputop_perf_read_samples(void);
extern uint64_t gputop_perf_accumulator[MAX_RAW_OA_COUNTERS];

bool gputop_perf_trace_open(gputop_perf_query_type_t query_type);
void gputop_perf_trace_start(void);
void gputop_perf_trace_stop(void);
void gputop_perf_trace_close(void);

extern int gputop_perf_trace_buffer_size;
extern uint8_t *gputop_perf_trace_buffer;
extern bool gputop_perf_trace_empty;
extern bool gputop_perf_trace_full;
extern uint8_t *gputop_perf_trace_head;
extern int gputop_perf_n_samples;

uint64_t read_uint64_oa_counter(struct gputop_oa_counter *counter, uint64_t *accumulated);
uint32_t read_uint32_oa_counter(struct gputop_oa_counter *counter, uint64_t *accumulated);
bool read_bool_oa_counter(struct gputop_oa_counter *counter, uint64_t *accumulated);
double read_double_oa_counter(struct gputop_oa_counter *counter, uint64_t *accumulated);
float read_float_oa_counter(struct gputop_oa_counter *counter, uint64_t *accumulated);

uint64_t read_report_timestamp(const uint32_t *report);

#endif /* _GPUTOP_PERF_H_ */
