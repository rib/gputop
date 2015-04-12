/*
 * GPU Top
 *
 * Copyright (C) 2014 Intel Corporation
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

#include <config.h>

#include <linux/perf_event.h>

#include <xf86drm.h>
#include <libdrm/i915_drm.h>
#include <libdrm/intel_bufmgr.h>

#include <asm/unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <stropts.h>

#include <limits.h>
#include <errno.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>

#include <uv.h>

#include "intel_chipset.h"

#include "gputop-util.h"
#include "gputop-ui.h"
#include "gputop-perf.h"

/* Samples read from the perf circular buffer */
struct oa_perf_sample {
   struct perf_event_header header;
   uint32_t raw_size;
   uint8_t raw_data[];
};
#define MAX_OA_PERF_SAMPLE_SIZE (8 +   /* perf_event_header */       \
                                 4 +   /* raw_size */                \
                                 256 + /* raw OA counter snapshot */ \
                                 4)    /* alignment padding */

#define TAKEN(HEAD, TAIL, POT_SIZE)	(((HEAD) - (TAIL)) & (POT_SIZE - 1))

/* Note: this will equate to 0 when the buffer is exactly full... */
#define REMAINING(HEAD, TAIL, POT_SIZE) (POT_SIZE - TAKEN (HEAD, TAIL, POT_SIZE))

#if defined(__i386__)
#define rmb()           __asm__ volatile("lock; addl $0,0(%%esp)" ::: "memory")
#define mb()            __asm__ volatile("lock; addl $0,0(%%esp)" ::: "memory")
#endif

#if defined(__x86_64__)
#define rmb()           __asm__ volatile("lfence" ::: "memory")
#define mb()            __asm__ volatile("mfence" ::: "memory")
#endif

/* TODO: consider using <stdatomic.h> something like:
 *
 * #define rmb() atomic_thread_fence(memory_order_seq_consume)
 * #define mb() atomic_thread_fence(memory_order_seq_cst)
 */

/* Allow building for a more recent kernel than the system headers
 * correspond too... */
#ifndef PERF_EVENT_IOC_FLUSH
#include <linux/ioctl.h>
#define PERF_EVENT_IOC_FLUSH                 _IO ('$', 8)
#endif


/* attr.config */

#define I915_PERF_OA_CTX_ID_MASK	    0xffffffff
#define I915_PERF_OA_SINGLE_CONTEXT_ENABLE  (1ULL << 32)

#define I915_PERF_OA_FORMAT_SHIFT	    33
#define I915_PERF_OA_FORMAT_MASK	    (0x7ULL << 33)

#define I915_PERF_OA_FORMAT_A13_HSW	    (0ULL << 33)
#define I915_PERF_OA_FORMAT_A29_HSW	    (1ULL << 33)
#define I915_PERF_OA_FORMAT_A13_B8_C8_HSW   (2ULL << 33)
#define I915_PERF_OA_FORMAT_B4_C8_HSW	    (4ULL << 33)
#define I915_PERF_OA_FORMAT_A45_B8_C8_HSW   (5ULL << 33)
#define I915_PERF_OA_FORMAT_B4_C8_A16_HSW   (6ULL << 33)
#define I915_PERF_OA_FORMAT_C4_B8_HSW	    (7ULL << 33)

#define I915_PERF_OA_FORMAT_A12_BDW         (0ULL<<33)
#define I915_PERF_OA_FORMAT_A12_B8_C8_BDW   (2ULL<<33)
#define I915_PERF_OA_FORMAT_A36_B8_C8_BDW   (5ULL<<33)
#define I915_PERF_OA_FORMAT_C4_B8_BDW       (7ULL<<33)

#define I915_PERF_OA_TIMER_EXPONENT_SHIFT   36
#define I915_PERF_OA_TIMER_EXPONENT_MASK    (0x3fULL << 36)

#define I915_PERF_OA_PROFILE_SHIFT          42
#define I915_PERF_OA_PROFILE_MASK           (0x3fULL << 42)
#define I915_PERF_OA_PROFILE_3D             1


/* FIXME: HACK to dig out the context id from the
 * otherwise opaque drm_intel_context struct! */
struct _drm_intel_context {
   unsigned int ctx_id;
};

struct intel_device {
    uint32_t device;
    uint32_t subsystem_device;
    uint32_t subsystem_vendor;
};

#define MAX_PERF_QUERIES 2
#define MAX_PERF_QUERY_COUNTERS 150
#define MAX_OA_QUERY_COUNTERS 100

/* Describes how to read one OA counter which might be a raw counter read
 * directly from a counter snapshot or could be a higher level counter derived
 * from one or more raw counters.
 *
 * Raw counters will have set ->report_offset to the snapshot offset and have
 * an accumulator that can consider counter overflow according to the width of
 * that counter.
 *
 * Higher level counters can currently reference up to 3 other counters + use
 * ->config for anything. They don't need an accumulator.
 *
 * The data type that will be written to *value_out by the read function can
 * be determined by ->data_type
 */
struct gputop_oa_counter
{
   struct gputop_oa_counter *reference0;
   struct gputop_oa_counter *reference1;
   struct gputop_oa_counter *reference2;
   union {
      int report_offset;
      int config;
   };

   int accumulator_index;
   void (*accumulate)(struct gputop_oa_counter *counter,
                      const uint32_t *start,
                      const uint32_t *end,
                      uint64_t *accumulator);
   gputop_counter_data_type_t data_type;
   void (*read)(struct gputop_oa_counter *counter,
                uint64_t *accumulated,
                void *value_out);
};

struct gputop_query_builder
{
    struct gputop_perf_query *query;
    int next_accumulator_index;

    int a_offset;
    int b_offset;
    int c_offset;

    struct gputop_oa_counter *gpu_core_clock;
};

static struct intel_device intel_dev;

static unsigned int page_size;
static int eu_count;
static int perf_oa_event_fd = -1;

/* An i915_oa perf event fd gives exclusive access to the OA unit that
 * will report counter snapshots for a specific counter set/profile in a
 * specific layout/format.
 *
 * These are the current profile/format being used...
 */
static int perf_profile_id;
static uint64_t perf_oa_format_id;

/* The mmaped circular buffer for collecting samples from perf */
static uint8_t *perf_oa_mmap_base;
static size_t perf_oa_buffer_size;
static struct perf_event_mmap_page *perf_oa_mmap_page;


static struct gputop_perf_query perf_queries[MAX_PERF_QUERIES];

static uv_poll_t perf_oa_event_fd_poll;

char *gputop_perf_error = NULL;
const struct gputop_perf_query *gputop_current_perf_query;
uint64_t gputop_perf_accumulator[MAX_RAW_OA_COUNTERS];

/******************************************************************************/

static uint64_t
read_file_uint64 (const char *file)
{
    char buf[32];
    int fd, n;

    fd = open(file, 0);
    if (fd < 0)
	return 0;
    n = read(fd, buf, sizeof (buf) - 1);
    close(fd);
    if (n < 0)
	return 0;

    buf[n] = '\0';
    return strtoull(buf, 0, 0);
}

static uint64_t
lookup_i915_oa_id (void)
{
    return read_file_uint64("/sys/bus/event_source/devices/i915_oa/type");
}

static long
perf_event_open (struct perf_event_attr *hw_event,
                 pid_t pid,
                 int cpu,
                 int group_fd,
                 unsigned long flags)
{
    return syscall(__NR_perf_event_open, hw_event, pid, cpu, group_fd, flags);
}

static bool
open_i915_oa_event(int counter_profile_id,
                   uint64_t report_format,
                   int period_exponent)
{
    struct perf_event_attr attr;
    int event_fd;
    void *mmap_base;

    memset(&attr, 0, sizeof (struct perf_event_attr));
    attr.size = sizeof (struct perf_event_attr);
    attr.type = lookup_i915_oa_id();

    attr.config |= (uint64_t)counter_profile_id << I915_PERF_OA_PROFILE_SHIFT;
    attr.config |= report_format;
    attr.config |= (uint64_t)period_exponent << I915_PERF_OA_TIMER_EXPONENT_SHIFT;

    attr.sample_type = PERF_SAMPLE_RAW;
    attr.sample_period = 1;

    attr.watermark = true;
    attr.wakeup_watermark = perf_oa_buffer_size / 4;

    event_fd = perf_event_open(&attr,
			       -1, /* pid */
			       0, /* cpu */
			       -1, /* group fd */
			       PERF_FLAG_FD_CLOEXEC); /* flags */
    if (event_fd == -1) {
	asprintf(&gputop_perf_error, "Error opening i915_oa perf event: %m\n");
	return false;
    }

    /* NB: A read-write mapping ensures the kernel will stop writing data when
     * the buffer is full, and will report samples as lost. */
    mmap_base = mmap(NULL,
		     perf_oa_buffer_size + page_size,
		     PROT_READ | PROT_WRITE, MAP_SHARED, event_fd, 0);
    if (mmap_base == MAP_FAILED) {
	asprintf(&gputop_perf_error, "Error mapping circular buffer, %m\n");
	close (event_fd);
	return false;
    }

    perf_oa_event_fd = event_fd;
    perf_oa_mmap_base = mmap_base;
    perf_oa_mmap_page = mmap_base;

    perf_profile_id = counter_profile_id;
    perf_oa_format_id = report_format;

    return true;
}

static int
get_eu_count(int fd, uint32_t devid)
{
    drm_i915_getparam_t gp;
    int count = 0;
    int ret;

    if (IS_HSW_GT1(devid))
	return 10;
    if (IS_HSW_GT2(devid))
	return 20;
    if (IS_HSW_GT3(devid))
	return 40;

#ifdef I915_PARAM_CMD_EU_TOTAL
    gp.param = I915_PARAM_CMD_EU_TOTAL;
    gp.value = &count;
    ret = drmIoctl(fd, DRM_IOCTL_I915_GETPARAM, &gp);
    assert(ret == 0 && count > 0);

    return count;
#else
    assert(0);
    return 0;
#endif
}

/* Handle restarting ioctl if interupted... */
static int
perf_ioctl(int fd, unsigned long request, void *arg)
{
    int ret;

    do {
	ret = ioctl(fd, request, arg);
    } while (ret == -1 && (errno == EINTR || errno == EAGAIN));
    return ret;
}

static unsigned int
read_perf_head(struct perf_event_mmap_page *mmap_page)
{
    unsigned int head = (*(volatile uint64_t *)&mmap_page->data_head);
    rmb();

    return head;
}

static void
write_perf_tail(struct perf_event_mmap_page *mmap_page,
                unsigned int tail)
{
    /* Make sure we've finished reading all the sample data we
     * we're consuming before updating the tail... */
    mb();
    mmap_page->data_tail = tail;
}

/**
 * Given pointers to starting and ending OA snapshots, calculate the deltas for each
 * counter to update the results.
 */
static void
update_counters(uint32_t *start, uint32_t *end)
{
    int i;

    memset(gputop_perf_accumulator, 0, sizeof(gputop_perf_accumulator));

#if 0
    fprintf(stderr, "Accumulating delta:\n");
    fprintf(stderr, "> Start timestamp = %" PRIu64 "\n", read_report_timestamp(brw, start));
    fprintf(stderr, "> End timestamp = %" PRIu64 "\n", read_report_timestamp(brw, end));
#endif

    for (i = 0; i < gputop_current_perf_query->n_oa_counters; i++) {
	struct gputop_oa_counter *oa_counter =
	    &gputop_current_perf_query->oa_counters[i];
	//uint64_t pre_accumulate;

	if (!oa_counter->accumulate)
	    continue;

	//pre_accumulate = query->oa.accumulator[counter->id];
	oa_counter->accumulate(oa_counter,
			       start, end,
			       gputop_perf_accumulator);
#if 0
	fprintf(stderr, "> Updated %s from %" PRIu64 " to %" PRIu64 "\n",
		counter->name, pre_accumulate,
		query->oa.accumulator[counter->id]);
#endif
    }
}

void
gputop_read_perf_samples(void)
{
    uint8_t *data = perf_oa_mmap_base + page_size;
    const uint64_t mask = perf_oa_buffer_size - 1;
    uint64_t head;
    uint64_t tail;
    uint32_t *last = NULL;
    uint8_t scratch[MAX_OA_PERF_SAMPLE_SIZE];

    if (perf_ioctl(perf_oa_event_fd, PERF_EVENT_IOC_FLUSH, 0) < 0)
	dbg("Failed to flush i915_oa perf samples");

    head = read_perf_head(perf_oa_mmap_page);
    tail = perf_oa_mmap_page->data_tail;

    /* FIXME: since we only really care about the most recent sample
     * we should first figure out how many samples we have between
     * tail and head so we can skip all but the last sample */

    //fprintf(stderr, "Handle event mask = 0x%" PRIx64
    //        " head=%" PRIu64 " tail=%" PRIu64 "\n", mask, head, tail);

    while (TAKEN(head, tail, perf_oa_buffer_size)) {
	const struct perf_event_header *header =
	    (const struct perf_event_header *)(data + (tail & mask));

	if (header->size == 0) {
	    dbg("Spurious header size == 0\n");
	    /* XXX: How should we handle this instead of exiting() */
	    exit(1);
	}

	if (header->size > (head - tail)) {
	    dbg("Spurious header size would overshoot head\n");
	    /* XXX: How should we handle this instead of exiting() */
	    exit(1);
	}

	//fprintf(stderr, "header = %p tail=%" PRIu64 " size=%d\n",
	//        header, tail, header->size);

	if ((const uint8_t *)header + header->size > data + perf_oa_buffer_size) {
	    int before;

	    if (header->size > MAX_OA_PERF_SAMPLE_SIZE) {
		dbg("Skipping spurious sample larger than expected\n");
		tail += header->size;
		continue;
	    }

	    before = data + perf_oa_buffer_size - (const uint8_t *)header;

	    memcpy(scratch, header, before);
	    memcpy(scratch + before, data, header->size - before);

	    header = (struct perf_event_header *)scratch;
	    //fprintf(stderr, "DEBUG: split\n");
	    //exit(1);
	}

	switch (header->type) {
	case PERF_RECORD_LOST: {
	    struct {
		struct perf_event_header header;
		uint64_t id;
		uint64_t n_lost;
	    } *lost = (void *)header;
	    dbg("i915_oa: Lost %" PRIu64 " events\n", lost->n_lost);
	    break;
	}

	case PERF_RECORD_THROTTLE:
	    dbg("i915_oa: Sampling has been throttled\n");
	    break;

	case PERF_RECORD_UNTHROTTLE:
	    dbg("i915_oa: Sampling has been unthrottled\n");
	    break;

	case PERF_RECORD_SAMPLE: {
	    struct oa_perf_sample *perf_sample = (struct oa_perf_sample *)header;
	    uint32_t *report = (uint32_t *)perf_sample->raw_data;

	    if (last)
		update_counters(last, report);
	    last = report;
	    break;
	}

	default:
	    dbg("i915_oa: Spurious header type = %d\n", header->type);
	}

	//fprintf(stderr, "Tail += %d\n", header->size);

	tail += header->size;
    }

    write_perf_tail(perf_oa_mmap_page, tail);
}

/******************************************************************************/

/* Type safe wrappers for reading OA counter values */

uint64_t
read_uint64_oa_counter(struct gputop_oa_counter *counter, uint64_t *accumulated)
{
   uint64_t value;

   assert(counter->data_type == GPUTOP_PERFQUERY_COUNTER_DATA_UINT64);

   counter->read(counter, accumulated, &value);

   return value;
}

uint32_t
read_uint32_oa_counter(struct gputop_oa_counter *counter, uint64_t *accumulated)
{
   uint64_t value;

   assert(counter->data_type == GPUTOP_PERFQUERY_COUNTER_DATA_UINT32);

   counter->read(counter, accumulated, &value);

   return value;
}

bool
read_bool_oa_counter(struct gputop_oa_counter *counter, uint64_t *accumulated)
{
   uint32_t value;

   assert(counter->data_type == GPUTOP_PERFQUERY_COUNTER_DATA_BOOL32);

   counter->read(counter, accumulated, &value);

   return !!value;
}

double
read_double_oa_counter(struct gputop_oa_counter *counter, uint64_t *accumulated)
{
   double value;

   assert(counter->data_type == GPUTOP_PERFQUERY_COUNTER_DATA_DOUBLE);

   counter->read(counter, accumulated, &value);

   return value;
}

float
read_float_oa_counter(struct gputop_oa_counter *counter, uint64_t *accumulated)
{
   float value;

   assert(counter->data_type == GPUTOP_PERFQUERY_COUNTER_DATA_FLOAT);

   counter->read(counter, accumulated, &value);

   return value;
}

/******************************************************************************/

/*
 * OA counter normalisation support...
 */

static void
read_accumulated_oa_counter_cb(struct gputop_oa_counter *counter,
                               uint64_t *accumulator,
                               void *value)
{
   *((uint64_t *)value) = accumulator[counter->accumulator_index];
}

static void
accumulate_uint32_cb(struct gputop_oa_counter *counter,
                     const uint32_t *report0,
                     const uint32_t *report1,
                     uint64_t *accumulator)
{
   accumulator[counter->accumulator_index] +=
      (uint32_t)(report1[counter->report_offset] -
                 report0[counter->report_offset]);
}

static void
accumulate_uint40_cb(struct gputop_oa_counter *counter,
                     const uint32_t *report0,
                     const uint32_t *report1,
                     uint64_t *accumulator)
{
    uint8_t *high_bytes0 = (uint8_t *)(report0 + 40);
    uint8_t *high_bytes1 = (uint8_t *)(report1 + 40);
    uint64_t high0 = (uint64_t)(high_bytes0[counter->report_offset - 4]) << 32;
    uint64_t high1 = (uint64_t)(high_bytes1[counter->report_offset - 4]) << 32;
    uint64_t value0 = report0[counter->report_offset] | high0;
    uint64_t value1 = report1[counter->report_offset] | high1;
    uint64_t delta;

    if (value0 > value1)
	delta = (1ULL << 40) + value1 - value0;
    else
	delta = value1 - value0;

    accumulator[counter->accumulator_index] += delta;
}

static struct gputop_oa_counter *
add_raw_oa_counter(struct gputop_query_builder *builder, int report_offset)
{
   struct gputop_oa_counter *counter =
      &builder->query->oa_counters[builder->query->n_oa_counters++];

   counter->report_offset = report_offset;
   counter->accumulator_index = builder->next_accumulator_index++;

   if (IS_BROADWELL(intel_dev.device) &&
       builder->query->perf_oa_format_id == I915_PERF_OA_FORMAT_A36_B8_C8_BDW)
   {
       if (report_offset >= 4 && report_offset <= 35)
	   counter->accumulate = accumulate_uint40_cb;
       else
	   counter->accumulate = accumulate_uint32_cb;
   } else
       counter->accumulate = accumulate_uint32_cb;

   counter->read = read_accumulated_oa_counter_cb;
   counter->data_type = GPUTOP_PERFQUERY_COUNTER_DATA_UINT64;

   return counter;
}

static void
accumulate_start_uint32_cb(struct gputop_oa_counter *counter,
			   const uint32_t *report0,
			   const uint32_t *report1,
			   uint64_t *accumulator)
{
    /* XXX: this assumes we never start with a value of 0... */
    if (accumulator[counter->accumulator_index] == 0)
	accumulator[counter->accumulator_index] = report0[counter->report_offset];
}

static void
accumulate_start_uint40_cb(struct gputop_oa_counter *counter,
			   const uint32_t *report0,
			   const uint32_t *report1,
			   uint64_t *accumulator)
{
    uint8_t *high_bytes0 = (uint8_t *)(report0 + 40);
    uint64_t high0 = (uint64_t)(high_bytes0[counter->report_offset - 4]) << 32;
    uint64_t value0 = report0[counter->report_offset] | high0;

    /* XXX: this assumes we never start with a value of 0... */
    if (accumulator[counter->accumulator_index] == 0)
	accumulator[counter->accumulator_index] = value0;
}

static struct gputop_oa_counter *
add_raw_start_oa_counter(struct gputop_query_builder *builder, int report_offset)
{
   struct gputop_oa_counter *counter =
      &builder->query->oa_counters[builder->query->n_oa_counters++];

   counter->report_offset = report_offset;
   counter->accumulator_index = builder->next_accumulator_index++;

   if (IS_BROADWELL(intel_dev.device) &&
       builder->query->perf_oa_format_id == I915_PERF_OA_FORMAT_A36_B8_C8_BDW)
   {
       if (report_offset >= 4 && report_offset <= 35)
	   counter->accumulate = accumulate_start_uint40_cb;
       else
	   counter->accumulate = accumulate_start_uint32_cb;
   } else
       counter->accumulate = accumulate_start_uint32_cb;

   counter->read = read_accumulated_oa_counter_cb;
   counter->data_type = GPUTOP_PERFQUERY_COUNTER_DATA_UINT64;

   return counter;
}

static void
accumulate_end_uint32_cb(struct gputop_oa_counter *counter,
			 const uint32_t *report0,
			 const uint32_t *report1,
			 uint64_t *accumulator)
{
    accumulator[counter->accumulator_index] = report1[counter->report_offset];
}

static void
accumulate_end_uint40_cb(struct gputop_oa_counter *counter,
			 uint32_t *report0,
			 uint32_t *report1,
			 uint64_t *accumulator)
{
    uint8_t *high_bytes1 = (uint8_t *)(report1 + 40);
    uint64_t high1 = (uint64_t)(high_bytes1[counter->report_offset - 4]) << 32;
    uint64_t value1 = report1[counter->report_offset] | high1;

    accumulator[counter->accumulator_index] = value1;
}

static struct gputop_oa_counter *
add_raw_end_oa_counter(struct gputop_query_builder *builder, int report_offset)
{
   struct gputop_oa_counter *counter =
      &builder->query->oa_counters[builder->query->n_oa_counters++];

   counter->report_offset = report_offset;
   counter->accumulator_index = builder->next_accumulator_index++;

   if (IS_BROADWELL(intel_dev.device) &&
       builder->query->perf_oa_format_id == I915_PERF_OA_FORMAT_A36_B8_C8_BDW)
   {
       if (report_offset >= 4 && report_offset <= 35)
	   counter->accumulate = accumulate_end_uint40_cb;
       else
	   counter->accumulate = accumulate_end_uint32_cb;
   } else
       counter->accumulate = accumulate_end_uint32_cb;

   counter->read = read_accumulated_oa_counter_cb;
   counter->data_type = GPUTOP_PERFQUERY_COUNTER_DATA_UINT64;

   return counter;
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
accumulate_elapsed_cb(struct gputop_oa_counter *counter,
		      const uint32_t *report0,
		      const uint32_t *report1,
		      uint64_t *accumulator)
{
   uint64_t timestamp0 = read_report_timestamp(report0);
   uint64_t timestamp1 = read_report_timestamp(report1);

   accumulator[counter->accumulator_index] += (timestamp1 - timestamp0);
}

static struct gputop_oa_counter *
add_elapsed_oa_counter(struct gputop_query_builder *builder)
{
   struct gputop_oa_counter *counter =
      &builder->query->oa_counters[builder->query->n_oa_counters++];

   counter->accumulator_index = builder->next_accumulator_index++;
   counter->accumulate = accumulate_elapsed_cb;
   counter->read = read_accumulated_oa_counter_cb;
   counter->data_type = GPUTOP_PERFQUERY_COUNTER_DATA_UINT64;

   return counter;
}

static void
accumulate_start_time_cb(struct gputop_oa_counter *counter,
			 uint32_t *report0,
			 uint32_t *report1,
			 uint64_t *accumulator)
{
    /* XXX: this assumes we never start with a value of 0... */
    if (accumulator[counter->accumulator_index] == 0)
	accumulator[counter->accumulator_index] = read_report_timestamp(report0);
}

static struct gputop_oa_counter *
add_start_time_oa_counter(struct gputop_query_builder *builder)
{
   struct gputop_oa_counter *counter =
      &builder->query->oa_counters[builder->query->n_oa_counters++];

   counter->accumulator_index = builder->next_accumulator_index++;
   counter->accumulate = accumulate_start_time_cb;
   counter->read = read_accumulated_oa_counter_cb;
   counter->data_type = GPUTOP_PERFQUERY_COUNTER_DATA_UINT64;

   return counter;
}

static void
accumulate_end_time_cb(struct gputop_oa_counter *counter,
		       uint32_t *report0,
		       uint32_t *report1,
		       uint64_t *accumulator)
{
    accumulator[counter->accumulator_index] = read_report_timestamp(report1);
}

static struct gputop_oa_counter *
add_end_time_oa_counter(struct gputop_query_builder *builder)
{
   struct gputop_oa_counter *counter =
      &builder->query->oa_counters[builder->query->n_oa_counters++];

   counter->accumulator_index = builder->next_accumulator_index++;
   counter->accumulate = accumulate_end_time_cb;
   counter->read = read_accumulated_oa_counter_cb;
   counter->data_type = GPUTOP_PERFQUERY_COUNTER_DATA_UINT64;

   return counter;
}

static void
read_frequency_cb(struct gputop_oa_counter *counter,
                  uint64_t *accumulated,
                  void *value) /* uint64 */
{
   uint64_t clk_delta = read_uint64_oa_counter(counter->reference0, accumulated);
   uint64_t time_delta = read_uint64_oa_counter(counter->reference1, accumulated);
   uint64_t *ret = value;

   if (!clk_delta) {
      *ret = 0;
      return;
   }

   *ret = (clk_delta * 1000) / time_delta;
}

static struct gputop_oa_counter *
add_avg_frequency_oa_counter(struct gputop_query_builder *builder,
                             struct gputop_oa_counter *timestamp)
{
   struct gputop_oa_counter *counter =
      &builder->query->oa_counters[builder->query->n_oa_counters++];

   assert(timestamp->data_type == GPUTOP_PERFQUERY_COUNTER_DATA_UINT64);

   counter->reference0 = builder->gpu_core_clock;
   counter->reference1 = timestamp;
   counter->read = read_frequency_cb;
   counter->data_type = GPUTOP_PERFQUERY_COUNTER_DATA_UINT64;

   return counter;
}

static void
read_oa_counter_normalized_by_gpu_duration_cb(struct gputop_oa_counter *counter,
                                              uint64_t *accumulated,
                                              void *value) /* float */
{
   uint64_t delta = read_uint64_oa_counter(counter->reference0, accumulated);
   uint64_t clk_delta = read_uint64_oa_counter(counter->reference1, accumulated);
   float *ret = value;

   if (!clk_delta) {
      *ret = 0;
      return;
   }

   *ret = ((double)delta * 100.0) / (double)clk_delta;
}

static struct gputop_oa_counter *
add_oa_counter_normalised_by_gpu_duration(struct gputop_query_builder *builder,
                                          struct gputop_oa_counter *raw)
{
   struct gputop_oa_counter *counter =
      &builder->query->oa_counters[builder->query->n_oa_counters++];

   counter->reference0 = raw;
   counter->reference1 = builder->gpu_core_clock;
   counter->read = read_oa_counter_normalized_by_gpu_duration_cb;
   counter->data_type = GPUTOP_PERFQUERY_COUNTER_DATA_FLOAT;

   return counter;
}

static void
read_hsw_samplers_busy_duration_cb(struct gputop_oa_counter *counter,
                                   uint64_t *accumulated,
                                   void *value) /* float */
{
   uint64_t sampler0_busy = read_uint64_oa_counter(counter->reference0, accumulated);
   uint64_t sampler1_busy = read_uint64_oa_counter(counter->reference1, accumulated);
   uint64_t clk_delta = read_uint64_oa_counter(counter->reference2, accumulated);
   float *ret = value;

   if (!clk_delta) {
      *ret = 0;
      return;
   }

   *ret = ((double)(sampler0_busy + sampler1_busy) * 100.0) / ((double)clk_delta * 2.0);
}

static struct gputop_oa_counter *
add_hsw_samplers_busy_duration_oa_counter(struct gputop_query_builder *builder,
                                          struct gputop_oa_counter *sampler0_busy_raw,
                                          struct gputop_oa_counter *sampler1_busy_raw)
{
   struct gputop_oa_counter *counter =
      &builder->query->oa_counters[builder->query->n_oa_counters++];

   counter->reference0 = sampler0_busy_raw;
   counter->reference1 = sampler1_busy_raw;
   counter->reference2 = builder->gpu_core_clock;
   counter->read = read_hsw_samplers_busy_duration_cb;
   counter->data_type = GPUTOP_PERFQUERY_COUNTER_DATA_FLOAT;

   return counter;
}

static void
read_hsw_slice_extrapolated_cb(struct gputop_oa_counter *counter,
                               uint64_t *accumulated,
                               void *value) /* float */
{
   uint64_t counter0 = read_uint64_oa_counter(counter->reference0, accumulated);
   uint64_t counter1 = read_uint64_oa_counter(counter->reference1, accumulated);
   int eu_count = counter->config;
   uint64_t *ret = value;

   *ret = (counter0 + counter1) * eu_count;
}

static struct gputop_oa_counter *
add_hsw_slice_extrapolated_oa_counter(struct gputop_query_builder *builder,
                                      struct gputop_oa_counter *counter0,
                                      struct gputop_oa_counter *counter1)
{
   struct gputop_oa_counter *counter =
      &builder->query->oa_counters[builder->query->n_oa_counters++];

   counter->reference0 = counter0;
   counter->reference1 = counter1;
   counter->config = eu_count;
   counter->read = read_hsw_slice_extrapolated_cb;
   counter->data_type = GPUTOP_PERFQUERY_COUNTER_DATA_UINT64;

   return counter;
}

static void
read_oa_counter_normalized_by_eu_duration_cb(struct gputop_oa_counter *counter,
                                             uint64_t *accumulated,
                                             void *value) /* float */
{
   uint64_t delta = read_uint64_oa_counter(counter->reference0, accumulated);
   uint64_t clk_delta = read_uint64_oa_counter(counter->reference1, accumulated);
   float *ret = value;

   if (!clk_delta) {
      *ret = 0;
      return;
   }

   delta /= counter->config; /* EU count */

   *ret = (double)delta * 100.0 / (double)clk_delta;
}

static struct gputop_oa_counter *
add_oa_counter_normalised_by_eu_duration(struct gputop_query_builder *builder,
                                         struct gputop_oa_counter *raw)
{
   struct gputop_oa_counter *counter =
      &builder->query->oa_counters[builder->query->n_oa_counters++];

   counter->reference0 = raw;
   counter->reference1 = builder->gpu_core_clock;
   counter->config = eu_count;
   counter->read = read_oa_counter_normalized_by_eu_duration_cb;
   counter->data_type = GPUTOP_PERFQUERY_COUNTER_DATA_FLOAT;

   return counter;
}

static void
read_av_thread_cycles_counter_cb(struct gputop_oa_counter *counter,
                                 uint64_t *accumulated,
                                 void *value) /* uint64 */
{
   uint64_t delta = read_uint64_oa_counter(counter->reference0, accumulated);
   uint64_t spawned = read_uint64_oa_counter(counter->reference1, accumulated);
   uint64_t *ret = value;

   if (!spawned) {
      *ret = 0;
      return;
   }

   *ret = delta / spawned;
}

static struct gputop_oa_counter *
add_average_thread_cycles_oa_counter(struct gputop_query_builder *builder,
                                     struct gputop_oa_counter *raw,
                                     struct gputop_oa_counter *denominator)
{
   struct gputop_oa_counter *counter =
      &builder->query->oa_counters[builder->query->n_oa_counters++];

   counter->reference0 = raw;
   counter->reference1 = denominator;
   counter->read = read_av_thread_cycles_counter_cb;
   counter->data_type = GPUTOP_PERFQUERY_COUNTER_DATA_UINT64;

   return counter;
}

static void
read_scaled_uint64_counter_cb(struct gputop_oa_counter *counter,
                              uint64_t *accumulated,
                              void *value) /* uint64 */
{
   uint64_t delta = read_uint64_oa_counter(counter->reference0, accumulated);
   uint64_t scale = counter->config;
   uint64_t *ret = value;

   *ret = delta * scale;
}

static struct gputop_oa_counter *
add_scaled_uint64_oa_counter(struct gputop_query_builder *builder,
                             struct gputop_oa_counter *input,
                             int scale)
{
   struct gputop_oa_counter *counter =
      &builder->query->oa_counters[builder->query->n_oa_counters++];

   counter->reference0 = input;
   counter->config = scale;
   counter->read = read_scaled_uint64_counter_cb;
   counter->data_type = GPUTOP_PERFQUERY_COUNTER_DATA_UINT64;

   return counter;
}

static void
read_max_of_float_counters_cb(struct gputop_oa_counter *counter,
                              uint64_t *accumulated,
                              void *value) /* float */
{
   float counter0 = read_float_oa_counter(counter->reference0, accumulated);
   float counter1 = read_float_oa_counter(counter->reference1, accumulated);
   float *ret = value;

   *ret = counter0 >= counter1 ? counter0 : counter1;
}


static struct gputop_oa_counter *
add_max_of_float_oa_counters(struct gputop_query_builder *builder,
                             struct gputop_oa_counter *counter0,
                             struct gputop_oa_counter *counter1)
{
   struct gputop_oa_counter *counter =
      &builder->query->oa_counters[builder->query->n_oa_counters++];

   counter->reference0 = counter0;
   counter->reference1 = counter1;
   counter->read = read_max_of_float_counters_cb;
   counter->data_type = GPUTOP_PERFQUERY_COUNTER_DATA_FLOAT;

   return counter;
}

static void
report_uint64_oa_counter_as_raw_uint64(struct gputop_query_builder *builder,
                                       const char *name,
                                       const char *desc,
                                       struct gputop_oa_counter *oa_counter)
{
   struct gputop_perf_query_counter *counter =
      &builder->query->counters[builder->query->n_counters++];

   counter->oa_counter = oa_counter;
   counter->name = name;
   counter->desc = desc;
   counter->type = GPUTOP_PERFQUERY_COUNTER_RAW;
   counter->data_type = GPUTOP_PERFQUERY_COUNTER_DATA_UINT64;
   counter->max = 0; /* undefined range */
}

static void
report_uint64_oa_counter_as_uint64_event(struct gputop_query_builder *builder,
                                         const char *name,
                                         const char *desc,
                                         struct gputop_oa_counter *oa_counter)
{
   struct gputop_perf_query_counter *counter =
      &builder->query->counters[builder->query->n_counters++];

   counter->oa_counter = oa_counter;
   counter->name = name;
   counter->desc = desc;
   counter->type = GPUTOP_PERFQUERY_COUNTER_EVENT;
   counter->data_type = GPUTOP_PERFQUERY_COUNTER_DATA_UINT64;
}

static void
report_float_oa_counter_as_percentage_duration(struct gputop_query_builder *builder,
                                               const char *name,
                                               const char *desc,
                                               struct gputop_oa_counter *oa_counter)
{
    struct gputop_perf_query_counter *counter =
	&builder->query->counters[builder->query->n_counters++];

    counter->oa_counter = oa_counter;
    counter->name = name;
    counter->desc = desc;
    counter->type = GPUTOP_PERFQUERY_COUNTER_DURATION_RAW;
    counter->data_type = GPUTOP_PERFQUERY_COUNTER_DATA_FLOAT;
    counter->max = 100;
}

static void
report_uint64_oa_counter_as_throughput(struct gputop_query_builder *builder,
                                       const char *name,
                                       const char *desc,
                                       struct gputop_oa_counter *oa_counter)
{
    struct gputop_perf_query_counter *counter =
	&builder->query->counters[builder->query->n_counters++];

    counter->oa_counter = oa_counter;
    counter->name = name;
    counter->desc = desc;
    counter->type = GPUTOP_PERFQUERY_COUNTER_THROUGHPUT;
    counter->data_type = GPUTOP_PERFQUERY_COUNTER_DATA_UINT64;
}

static void
report_uint64_oa_counter_as_duration(struct gputop_query_builder *builder,
                                     const char *name,
                                     const char *desc,
                                     struct gputop_oa_counter *oa_counter)
{
    struct gputop_perf_query_counter *counter =
	&builder->query->counters[builder->query->n_counters++];

    counter->oa_counter = oa_counter;
    counter->name = name;
    counter->desc = desc;
    counter->type = GPUTOP_PERFQUERY_COUNTER_DURATION_RAW;
    counter->data_type = GPUTOP_PERFQUERY_COUNTER_DATA_UINT64;
    counter->max = 0;
}

static void
add_pipeline_stage_counters(struct gputop_query_builder *builder,
                            const char *short_name,
                            const char *long_name,
                            int aggregate_active_counter,
                            int aggregate_stall_counter,
                            int n_threads_counter)
{
    struct gputop_oa_counter *active, *stall, *n_threads, *c;
    char *short_desc;
    char *long_desc;


    asprintf(&short_desc, "%s EU Active", short_name);
    asprintf(&long_desc,
	     "The percentage of time in which %s were "
	     "processed actively on the EUs.", long_name);
    active = add_raw_oa_counter(builder, aggregate_active_counter);
    c = add_oa_counter_normalised_by_eu_duration(builder, active);
    report_float_oa_counter_as_percentage_duration(builder, short_desc, long_desc, c);


    asprintf(&short_desc, "%s EU Stall", short_name);
    asprintf(&long_desc,
	     "The percentage of time in which %s were "
	     "stalled on the EUs.", long_name);
    stall = add_raw_oa_counter(builder, aggregate_stall_counter);
    c = add_oa_counter_normalised_by_eu_duration(builder, stall);
    report_float_oa_counter_as_percentage_duration(builder, short_desc, long_desc, c);


    n_threads = add_raw_oa_counter(builder, n_threads_counter);

    asprintf(&short_desc, "%s AVG Active per Thread", short_name);
    asprintf(&long_desc,
	     "The average number of cycles per hardware "
	     "thread run in which %s were processed actively "
	     "on the EUs.", long_name);
    c = add_average_thread_cycles_oa_counter(builder, active, n_threads);
    report_uint64_oa_counter_as_raw_uint64(builder, short_desc, long_desc, c);


    asprintf(&short_desc, "%s AVG Stall per Thread", short_name);
    asprintf(&long_desc,
	     "The average number of cycles per hardware "
	     "thread run in which %s were stalled "
	     "on the EUs.", long_name);
    c = add_average_thread_cycles_oa_counter(builder, stall, n_threads);
    report_uint64_oa_counter_as_raw_uint64(builder, short_desc, long_desc, c);
}

static void
hsw_add_aggregate_counters(struct gputop_query_builder *builder)
{
    struct gputop_oa_counter *raw;
    struct gputop_oa_counter *c;
    int a_offset = builder->a_offset;

    raw = add_raw_oa_counter(builder, a_offset + 41);
    c = add_oa_counter_normalised_by_gpu_duration(builder, raw);
    report_float_oa_counter_as_percentage_duration(builder,
						   "GPU Busy",
						   "The percentage of time in which the GPU has being processing GPU commands.",
						   c);

    raw = add_raw_oa_counter(builder, a_offset); /* aggregate EU active */
    c = add_oa_counter_normalised_by_eu_duration(builder, raw);
    report_float_oa_counter_as_percentage_duration(builder,
						   "EU Active",
						   "The percentage of time in which the Execution Units were actively processing.",
						   c);

    raw = add_raw_oa_counter(builder, a_offset + 1); /* aggregate EU stall */
    c = add_oa_counter_normalised_by_eu_duration(builder, raw);
    report_float_oa_counter_as_percentage_duration(builder,
						   "EU Stall",
						   "The percentage of time in which the Execution Units were stalled.",
						   c);

    add_pipeline_stage_counters(builder,
				"VS",
				"vertex shaders",
				a_offset + 2, /* aggregate active */
				a_offset + 3, /* aggregate stall */
				a_offset + 5); /* n threads loaded */

    /* Not currently supported by Mesa... */
#if 0
    add_pipeline_stage_counters(builder,
				"HS",
				"hull shaders",
				a_offset + 7, /* aggregate active */
				a_offset + 8, /* aggregate stall */
				a_offset + 10); /* n threads loaded */

    add_pipeline_stage_counters(builder,
				"DS",
				"domain shaders",
				a_offset + 12, /* aggregate active */
				a_offset + 13, /* aggregate stall */
				a_offset + 15); /* n threads loaded */

    add_pipeline_stage_counters(builder,
				"CS",
				"compute shaders",
				a_offset + 17, /* aggregate active */
				a_offset + 18, /* aggregate stall */
				a_offset + 20); /* n threads loaded */
#endif

    add_pipeline_stage_counters(builder,
				"GS",
				"geometry shaders",
				a_offset + 22, /* aggregate active */
				a_offset + 23, /* aggregate stall */
				a_offset + 25); /* n threads loaded */

    add_pipeline_stage_counters(builder,
				"PS",
				"pixel shaders",
				a_offset + 27, /* aggregate active */
				a_offset + 28, /* aggregate stall */
				a_offset + 30); /* n threads loaded */

    raw = add_raw_oa_counter(builder, a_offset + 32); /* hiz fast z passing */
    raw = add_raw_oa_counter(builder, a_offset + 33); /* hiz fast z failing */

    raw = add_raw_oa_counter(builder, a_offset + 42); /* vs bottleneck */
    raw = add_raw_oa_counter(builder, a_offset + 43); /* gs bottleneck */
}

static void
hsw_add_basic_oa_counter_query(void)
{
    struct gputop_query_builder builder;
    struct gputop_perf_query *query = &perf_queries[0];
    struct gputop_oa_counter *elapsed;
    struct gputop_oa_counter *c;
    int a_offset = 3; /* A0 */
    int b_offset = a_offset + 45; /* B0 */

    query->name = "Gen7 Basic Observability Architecture Counters";
    query->counters = xmalloc0(sizeof(struct gputop_perf_query_counter) *
			       MAX_PERF_QUERY_COUNTERS);
    query->n_counters = 0;
    query->oa_counters = xmalloc0(sizeof(struct gputop_oa_counter) *
				  MAX_OA_QUERY_COUNTERS);
    query->n_oa_counters = 0;
    query->perf_profile_id = 0; /* default profile */
    query->perf_oa_format_id = I915_PERF_OA_FORMAT_A45_B8_C8_HSW;

    builder.query = query;
    builder.next_accumulator_index = 0;

    builder.a_offset = a_offset;
    builder.b_offset = b_offset;
    builder.c_offset = -1;

    /* Can be referenced by other counters... */
    builder.gpu_core_clock = add_raw_oa_counter(&builder, b_offset);

    elapsed = add_elapsed_oa_counter(&builder);
    report_uint64_oa_counter_as_duration(&builder,
					 "GPU Time Elapsed",
					 "Time elapsed on the GPU during the measurement.",
					 elapsed);

    c = add_avg_frequency_oa_counter(&builder, elapsed);
    report_uint64_oa_counter_as_uint64_event(&builder,
					     "AVG GPU Core Frequency",
					     "Average GPU Core Frequency in the measurement.",
					     c);

    hsw_add_aggregate_counters(&builder);

    assert(query->n_counters < MAX_PERF_QUERY_COUNTERS);
    assert(query->n_oa_counters < MAX_OA_QUERY_COUNTERS);
}

static void
hsw_add_3d_oa_counter_query(void)
{
    struct gputop_query_builder builder;
    struct gputop_perf_query *query = &perf_queries[I915_PERF_OA_PROFILE_3D];
    int a_offset;
    int b_offset;
    int c_offset;
    struct gputop_oa_counter *elapsed;
    struct gputop_oa_counter *raw;
    struct gputop_oa_counter *c;
    struct gputop_oa_counter *sampler0_busy_raw;
    struct gputop_oa_counter *sampler1_busy_raw;
    struct gputop_oa_counter *sampler0_bottleneck;
    struct gputop_oa_counter *sampler1_bottleneck;
    struct gputop_oa_counter *sampler0_texels;
    struct gputop_oa_counter *sampler1_texels;
    struct gputop_oa_counter *sampler0_l1_misses;
    struct gputop_oa_counter *sampler1_l1_misses;
    struct gputop_oa_counter *sampler_l1_misses;

    query->name = "Gen7 3D Observability Architecture Counters";
    query->counters = xmalloc0(sizeof(struct gputop_perf_query_counter) *
			       MAX_PERF_QUERY_COUNTERS);
    query->n_counters = 0;
    query->oa_counters = xmalloc0(sizeof(struct gputop_oa_counter) *
				  MAX_OA_QUERY_COUNTERS);
    query->n_oa_counters = 0;
    query->perf_profile_id = I915_PERF_OA_PROFILE_3D;
    query->perf_oa_format_id = I915_PERF_OA_FORMAT_A45_B8_C8_HSW;

    builder.query = query;
    builder.next_accumulator_index = 0;

    /* A counters offset = 12  bytes / 0x0c (45 A counters)
     * B counters offset = 192 bytes / 0xc0 (8  B counters)
     * C counters offset = 224 bytes / 0xe0 (8  C counters)
     *
     * Note: we index into the snapshots/reports as arrays of uint32 values
     * relative to the A/B/C offset since different report layouts can vary how
     * many A/B/C counters but with relative addressing it should be possible to
     * re-use code for describing the counters available with different report
     * layouts.
     */

    builder.a_offset = a_offset = 3;
    builder.b_offset = b_offset = a_offset + 45;
    builder.c_offset = c_offset = b_offset + 8;

    /* Can be referenced by other counters... */
    builder.gpu_core_clock = add_raw_oa_counter(&builder, c_offset + 2);

    elapsed = add_elapsed_oa_counter(&builder);
    report_uint64_oa_counter_as_duration(&builder,
					 "GPU Time Elapsed",
					 "Time elapsed on the GPU during the measurement.",
					 elapsed);

    raw = add_start_time_oa_counter(&builder);
    report_uint64_oa_counter_as_uint64_event(&builder,
					     "Start Timestamp",
					     "Start Timestamp",
					     raw);
    raw = add_end_time_oa_counter(&builder);
    report_uint64_oa_counter_as_uint64_event(&builder,
					     "End Timestamp",
					     "End Timestamp",
					     raw);

    raw = add_raw_start_oa_counter(&builder, c_offset + 2);
    report_uint64_oa_counter_as_uint64_event(&builder,
					     "Start Clock",
					     "Start Clock",
					     raw);
    raw = add_raw_end_oa_counter(&builder, c_offset + 2);
    report_uint64_oa_counter_as_uint64_event(&builder,
					     "End Clock",
					     "End Clock",
					     raw);

    c = add_avg_frequency_oa_counter(&builder, elapsed);
    report_uint64_oa_counter_as_uint64_event(&builder,
					     "AVG GPU Core Frequency",
					     "Average GPU Core Frequency in the measurement.",
					     c);

    hsw_add_aggregate_counters(&builder);

    raw = add_raw_oa_counter(&builder, a_offset + 35);
    report_uint64_oa_counter_as_uint64_event(&builder,
					     "Early Depth Test Fails",
					     "The total number of pixels dropped on early depth test.",
					     raw);
    /* XXX: caveat: it's 2x real No. when PS has 2 output colors */
    raw = add_raw_oa_counter(&builder, a_offset + 36);
    report_uint64_oa_counter_as_uint64_event(&builder,
					     "Samples Killed in PS",
					     "The total number of samples or pixels dropped in pixel shaders.",
					     raw);
    raw = add_raw_oa_counter(&builder, a_offset + 37);
    report_uint64_oa_counter_as_uint64_event(&builder,
					     "Alpha Test Fails",
					     "The total number of pixels dropped on post-PS alpha test.",
					     raw);
    raw = add_raw_oa_counter(&builder, a_offset + 38);
    report_uint64_oa_counter_as_uint64_event(&builder,
					     "Late Stencil Test Fails",
					     "The total number of pixels dropped on post-PS stencil test.",
					     raw);
    raw = add_raw_oa_counter(&builder, a_offset + 39);
    report_uint64_oa_counter_as_uint64_event(&builder,
					     "Late Depth Test Fails",
					     "The total number of pixels dropped on post-PS depth test.",
					     raw);
    raw = add_raw_oa_counter(&builder, a_offset + 40);
    report_uint64_oa_counter_as_uint64_event(&builder,
					     "Samples Written",
					     "The total number of samples or pixels written to all render targets.",
					     raw);

    raw = add_raw_oa_counter(&builder, c_offset + 5);
    /* I.e. assuming even work distribution across threads... */
    c = add_scaled_uint64_oa_counter(&builder, raw, eu_count * 4);
    report_uint64_oa_counter_as_uint64_event(&builder,
					     "Samples Blended",
					     "The total number of blended samples or pixels written to all render targets.",
					     c);

    /* XXX: XML implies explicit sub-slice availability check, but surely we can assume we have a slice 0? */
    sampler0_busy_raw = add_raw_oa_counter(&builder, b_offset + 0);
    c = add_oa_counter_normalised_by_gpu_duration(&builder, sampler0_busy_raw);
    report_float_oa_counter_as_percentage_duration(&builder,
						   "Sampler 0 Busy",
						   "The percentage of time in which sampler 0 was busy.",
						   c);
    /* XXX: XML implies explicit sub-slice availability check: might just have one sampler? */
    sampler1_busy_raw = add_raw_oa_counter(&builder, b_offset + 1);
    c = add_oa_counter_normalised_by_gpu_duration(&builder, sampler1_busy_raw);
    report_float_oa_counter_as_percentage_duration(&builder,
						   "Sampler 1 Busy",
						   "The percentage of time in which sampler 1 was busy.",
						   c);

    c = add_hsw_samplers_busy_duration_oa_counter(&builder,
						  sampler0_busy_raw,
						  sampler1_busy_raw);
    report_float_oa_counter_as_percentage_duration(&builder,
						   "Samplers Busy",
						   "The percentage of time in which samplers were busy.",
						   c);

    raw = add_raw_oa_counter(&builder, b_offset + 2);
    sampler0_bottleneck = add_oa_counter_normalised_by_gpu_duration(&builder, raw);
    report_float_oa_counter_as_percentage_duration(&builder,
						   "Sampler 0 Bottleneck",
						   "The percentage of time in which sampler 0 was a bottleneck.",
						   sampler0_bottleneck);
    raw = add_raw_oa_counter(&builder, b_offset + 3);
    sampler1_bottleneck = add_oa_counter_normalised_by_gpu_duration(&builder, raw);
    report_float_oa_counter_as_percentage_duration(&builder,
						   "Sampler 1 Bottleneck",
						   "The percentage of time in which sampler 1 was a bottleneck.",
						   sampler1_bottleneck);

    c = add_max_of_float_oa_counters(&builder, sampler0_bottleneck, sampler1_bottleneck);
    report_float_oa_counter_as_percentage_duration(&builder,
						   "Sampler Bottleneck",
						   "The percentage of time in which samplers were bottlenecks.",
						   c);
    raw = add_raw_oa_counter(&builder, b_offset + 4);
    sampler0_texels = add_scaled_uint64_oa_counter(&builder, raw, 4);
    report_uint64_oa_counter_as_uint64_event(&builder,
					     "Sampler 0 Texels LOD0", /* XXX LODO? */
					     "The total number of texels lookups in LOD0 in sampler 0 unit.",
					     sampler0_texels);
    raw = add_raw_oa_counter(&builder, b_offset + 5);
    sampler1_texels = add_scaled_uint64_oa_counter(&builder, raw, 4);
    report_uint64_oa_counter_as_uint64_event(&builder,
					     "Sampler 1 Texels LOD0", /* XXX LODO? */
					     "The total number of texels lookups in LOD0 in sampler 1 unit.",
					     sampler1_texels);

    /* TODO find a test case to try and sanity check the numbers we're getting */
    c = add_hsw_slice_extrapolated_oa_counter(&builder, sampler0_texels, sampler1_texels);
    report_uint64_oa_counter_as_uint64_event(&builder,
					     "Sampler Texels LOD0",
					     "The total number of texels lookups in LOD0 in all sampler units.",
					     c);

    raw = add_raw_oa_counter(&builder, b_offset + 6);
    sampler0_l1_misses = add_scaled_uint64_oa_counter(&builder, raw, 2);
    report_uint64_oa_counter_as_uint64_event(&builder,
					     "Sampler 0 Cache Misses",
					     "The total number of misses in L1 sampler caches.",
					     sampler0_l1_misses);
    raw = add_raw_oa_counter(&builder, b_offset + 7);
    sampler1_l1_misses = add_scaled_uint64_oa_counter(&builder, raw, 2);
    report_uint64_oa_counter_as_uint64_event(&builder,
					     "Sampler 1 Cache Misses",
					     "The total number of misses in L1 sampler caches.",
					     sampler1_l1_misses);
    sampler_l1_misses = add_hsw_slice_extrapolated_oa_counter(&builder, sampler0_l1_misses, sampler1_l1_misses);
    report_uint64_oa_counter_as_uint64_event(&builder,
					     "Sampler Cache Misses",
					     "The total number of misses in L1 sampler caches.",
					     sampler_l1_misses);

    c = add_scaled_uint64_oa_counter(&builder, sampler_l1_misses, 64);
    report_uint64_oa_counter_as_throughput(&builder,
					   "L3 Sampler Throughput",
					   "The total number of GPU memory bytes transferred between samplers and L3 caches.",
					   c);

    raw = add_raw_oa_counter(&builder, c_offset + 1);
    c = add_scaled_uint64_oa_counter(&builder, raw, 64);
    report_uint64_oa_counter_as_throughput(&builder,
					   "GTI Fixed Pipe Throughput",
					   "The total number of GPU memory bytes transferred between Fixed Pipeline (Command Dispatch, Input Assembly and Stream Output) and GTI.",
					   c);

    raw = add_raw_oa_counter(&builder, c_offset + 0);
    c = add_scaled_uint64_oa_counter(&builder, raw, 64);
    report_uint64_oa_counter_as_throughput(&builder,
					   "GTI Depth Throughput",
					   "The total number of GPU memory bytes transferred between depth caches and GTI.",
					   c);
    raw = add_raw_oa_counter(&builder, c_offset + 3);
    c = add_scaled_uint64_oa_counter(&builder, raw, 64);
    report_uint64_oa_counter_as_throughput(&builder,
					   "GTI RCC Throughput",
					   "The total number of GPU memory bytes transferred between render color caches and GTI.",
					   c);
    raw = add_raw_oa_counter(&builder, c_offset + 4);
    c = add_scaled_uint64_oa_counter(&builder, raw, 64);
    report_uint64_oa_counter_as_throughput(&builder,
					   "GTI L3 Throughput",
					   "The total number of GPU memory bytes transferred between L3 caches and GTI.",
					   c);
    raw = add_raw_oa_counter(&builder, c_offset + 6);
    c = add_scaled_uint64_oa_counter(&builder, raw, 128);
    report_uint64_oa_counter_as_throughput(&builder,
					   "GTI Read Throughput",
					   "The total number of GPU memory bytes read from GTI.",
					   c);
    raw = add_raw_oa_counter(&builder, c_offset + 7);
    c = add_scaled_uint64_oa_counter(&builder, raw, 64);
    report_uint64_oa_counter_as_throughput(&builder,
					   "GTI Write Throughput",
					   "The total number of GPU memory bytes written to GTI.",
					   c);

    assert(query->n_counters < MAX_PERF_QUERY_COUNTERS);
    assert(query->n_oa_counters < MAX_OA_QUERY_COUNTERS);
}

static void
bdw_add_aggregate_counters(struct gputop_query_builder *builder)
{
    struct gputop_oa_counter *raw;
    struct gputop_oa_counter *c;
    int a_offset = builder->a_offset;

    raw = add_raw_oa_counter(builder, a_offset);
    c = add_oa_counter_normalised_by_gpu_duration(builder, raw);
    report_float_oa_counter_as_percentage_duration(builder,
						   "GPU Busy",
						   "The percentage of time in which the GPU has being processing GPU commands.",
						   c);

    raw = add_raw_oa_counter(builder, a_offset + 7); /* aggregate EU active */
    c = add_oa_counter_normalised_by_eu_duration(builder, raw);
    report_float_oa_counter_as_percentage_duration(builder,
						   "EU Active",
						   "The percentage of time in which the Execution Units were actively processing.",
						   c);

    raw = add_raw_oa_counter(builder, a_offset + 8); /* aggregate EU stall */
    c = add_oa_counter_normalised_by_eu_duration(builder, raw);
    report_float_oa_counter_as_percentage_duration(builder,
						   "EU Stall",
						   "The percentage of time in which the Execution Units were stalled.",
						   c);

    add_pipeline_stage_counters(builder,
				"VS",
				"vertex shaders",
				a_offset + 13, /* aggregate active */
				a_offset + 14, /* aggregate stall */
				a_offset + 1); /* n threads loaded */

    /* Not currently supported by Mesa... */
#if 0
    add_pipeline_stage_counters(builder,
				"HS",
				"hull shaders",
				a_offset + 7, /* aggregate active */
				a_offset + 8, /* aggregate stall */
				a_offset + 10); /* n threads loaded */

    add_pipeline_stage_counters(builder,
				"DS",
				"domain shaders",
				a_offset + 12, /* aggregate active */
				a_offset + 13, /* aggregate stall */
				a_offset + 15); /* n threads loaded */

    add_pipeline_stage_counters(builder,
				"CS",
				"compute shaders",
				a_offset + 17, /* aggregate active */
				a_offset + 18, /* aggregate stall */
				a_offset + 20); /* n threads loaded */
#endif

#if 0
    add_pipeline_stage_counters(builder,
				"GS",
				"geometry shaders",
				a_offset + , /* aggregate active */
				a_offset + , /* aggregate stall */
				a_offset + 5); /* n threads loaded */
#endif

    add_pipeline_stage_counters(builder,
				"PS",
				"pixel shaders",
				a_offset + 19, /* aggregate active */
				a_offset + 20, /* aggregate stall */
				a_offset + 6); /* n threads loaded */

    raw = add_raw_oa_counter(builder, a_offset + 22); /* hiz fast z failing */
#warning "TODO: HiZ failing needs scale by 4 to report"
}


static void
bdw_add_3d_oa_counter_query(void)
{
    struct gputop_query_builder builder;
    struct gputop_perf_query *query = &perf_queries[I915_PERF_OA_PROFILE_3D];
    struct gputop_oa_counter *elapsed;
    struct gputop_oa_counter *c;
    int a_offset = 4; /* A0 */
    int b_offset = 48; /* B0 */

    query->name = "Gen8 Basic Observability Architecture Counters";
    query->counters = xmalloc0(sizeof(struct gputop_perf_query_counter) *
			       MAX_PERF_QUERY_COUNTERS);
    query->n_counters = 0;
    query->oa_counters = xmalloc0(sizeof(struct gputop_oa_counter) *
				  MAX_OA_QUERY_COUNTERS);
    query->n_oa_counters = 0;
    query->perf_profile_id = I915_PERF_OA_PROFILE_3D;
    query->perf_oa_format_id = I915_PERF_OA_FORMAT_A36_B8_C8_BDW;

    builder.query = query;
    builder.next_accumulator_index = 0;

    builder.a_offset = a_offset;
    builder.b_offset = b_offset;
    builder.c_offset = 56;

    /* Can be referenced by other counters... */
    builder.gpu_core_clock = add_raw_oa_counter(&builder, 3);

    elapsed = add_elapsed_oa_counter(&builder);
    report_uint64_oa_counter_as_duration(&builder,
					 "GPU Time Elapsed",
					 "Time elapsed on the GPU during the measurement.",
					 elapsed);

    c = add_avg_frequency_oa_counter(&builder, elapsed);
    report_uint64_oa_counter_as_uint64_event(&builder,
					     "AVG GPU Core Frequency",
					     "Average GPU Core Frequency in the measurement.",
					     c);

    bdw_add_aggregate_counters(&builder);

    assert(query->n_counters < MAX_PERF_QUERY_COUNTERS);
    assert(query->n_oa_counters < MAX_OA_QUERY_COUNTERS);
}

static uint32_t
read_device_param(int id, const char *param)
{
    char *name;
    int ret = asprintf(&name, "/sys/class/drm/renderD%u/"
		       "device/%s", id, param);
    uint32_t value;

    assert(ret != -1);

    value = read_file_uint64(name);
    free(name);

    return value;
}

static int
open_render_node(struct intel_device *dev)
{
    char *name;
    int i, fd;

    for (i = 128; i < (128 + 16); i++) {
	int ret;

	ret = asprintf(&name, "/dev/dri/renderD%u", i);
	assert(ret != -1);

	fd = open(name, O_RDWR);
	free(name);

	if (fd == -1)
	    continue;

	if (read_device_param(i, "vendor") != 0x8086) {
	    close(fd);
	    fd = -1;
	    continue;
	}

	dev->device = read_device_param(i, "device");
	dev->subsystem_device = read_device_param(i, "subsystem_device");
	dev->subsystem_vendor = read_device_param(i, "subsystem_vendor");

	return fd;
    }

    return fd;
}

static bool
initialize(void)
{
    int drm_fd = open_render_node(&intel_dev);
    if (drm_fd < 0) {
	gputop_perf_error = strdup("Failed to open render node");
	return false;
    }

    /* NB: eu_count needs to be initialized before declaring counters */
    eu_count = get_eu_count(drm_fd, intel_dev.device);
    page_size = sysconf(_SC_PAGE_SIZE);
    perf_oa_buffer_size = 32 * page_size;

    close(drm_fd);

    if (IS_HASWELL(intel_dev.device)) {
	hsw_add_basic_oa_counter_query();
	hsw_add_3d_oa_counter_query();
    } else if (IS_BROADWELL(intel_dev.device))
	bdw_add_3d_oa_counter_query();

    return true;
}

static void
perf_ready_cb(uv_poll_t *poll, int status, int events)
{
    gputop_read_perf_samples();
}

bool
gputop_perf_open(gputop_perf_query_type_t query_type)
{
    int period_exponent;

    assert(perf_oa_event_fd < 0);

    if (gputop_perf_error)
	free(gputop_perf_error);

    if (!eu_count && !initialize())
	return false;

    gputop_current_perf_query = &perf_queries[query_type];

    /* The timestamp for HSW+ increments every 80ns
     *
     * The period_exponent gives a sampling period as follows:
     *   sample_period = 80ns * 2^(period_exponent + 1)
     *
     * The overflow period for Haswell can be calculated as:
     *
     * 2^32 / (n_eus * max_gen_freq * 2)
     * (E.g. 40 EUs @ 1GHz = ~53ms)
     *
     * We currently sample ~ every 10 milliseconds...
     */
    period_exponent = 16;

    if (!open_i915_oa_event(gputop_current_perf_query->perf_profile_id,
			    gputop_current_perf_query->perf_oa_format_id,
			    period_exponent))
    {
	return false;
    }

    uv_poll_init(gputop_ui_loop, &perf_oa_event_fd_poll, perf_oa_event_fd);
    uv_poll_start(&perf_oa_event_fd_poll, UV_READABLE, perf_ready_cb);

    return true;
}

void
gputop_perf_close(void)
{
    if (!gputop_current_perf_query)
	return;

    if (perf_oa_event_fd > 0) {
	uv_poll_stop(&perf_oa_event_fd_poll);

	if (perf_oa_mmap_base) {
	    munmap(perf_oa_mmap_base, perf_oa_buffer_size + page_size);
	    perf_oa_mmap_base = NULL;
	}

	close(perf_oa_event_fd);
	perf_oa_event_fd = -1;
    }

    gputop_current_perf_query = NULL;
}
