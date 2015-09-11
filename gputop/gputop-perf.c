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
#include <i915_oa_drm.h>

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
#include <inttypes.h>

#include <uv.h>

#include "intel_chipset.h"

#include "gputop-util.h"
#include "gputop-list.h"
#include "gputop-ui.h"
#include "gputop-perf.h"

#include "oa-hsw.h"
#include "oa-bdw.h"

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

/* Allow building for a more recent kernel than the system headers
 * correspond too... */
#ifndef PERF_RECORD_DEVICE
#define PERF_RECORD_DEVICE                   13
#endif

/* attr.config */

struct intel_device {
    uint32_t device;
    uint32_t subsystem_device;
    uint32_t subsystem_vendor;
};

struct gputop_devinfo gputop_devinfo;

/* E.g. for tracing vs rolling view */
struct perf_oa_user {
    void (*sample)(struct gputop_perf_query *query, uint8_t *start, uint8_t *end);
};

static struct perf_oa_user *current_user;

static struct intel_device intel_dev;

static unsigned int page_size;

struct gputop_perf_query perf_queries[MAX_PERF_QUERIES];
struct gputop_perf_query *gputop_current_perf_query;
struct gputop_perf_stream *gputop_current_perf_stream;

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

static void
perf_ready_cb(uv_poll_t *poll, int status, int events)
{
    struct gputop_perf_stream *stream = poll->data;

    gputop_perf_read_samples(stream);
}

void
gputop_perf_stream_ref(struct gputop_perf_stream *stream)
{
    stream->ref_count++;
}

void
gputop_perf_stream_unref(struct gputop_perf_stream *stream)
{
    if (--(stream->ref_count) == 0) {
	if (stream->fd > 0) {
	    uv_poll_stop(&stream->fd_poll);

	    if (stream->mmap_page) {
		munmap(stream->mmap_page, stream->buffer_size + page_size);
		stream->mmap_page = NULL;
		stream->buffer = NULL;
		stream->buffer_size = 0;
	    }

	    if (stream->header_buf.offsets) {
		free(stream->header_buf.offsets);
		stream->header_buf.offsets = NULL;
	    }

	    close(stream->fd);
	    stream->fd = -1;

	    fprintf(stderr, "closed i915_oa perf stream\n");
	}

	if (stream->user.destroy_cb)
	    stream->user.destroy_cb(stream);
	free(stream);
	fprintf(stderr, "freed i915_oa perf stream\n");
    }
}

struct gputop_perf_stream *
gputop_perf_open_i915_oa_query(struct gputop_perf_query *query,
			       int period_exponent,
			       size_t perf_buffer_size,
			       void (*ready_cb)(uv_poll_t *poll, int status, int events),
			       bool overwrite,
			       char **error)
{
    struct gputop_perf_stream *stream;
    i915_oa_attr_t oa_attr;
    struct perf_event_attr attr;
    int event_fd;
    uint8_t *mmap_base;
    int expected_max_samples;

    memset(&attr, 0, sizeof(attr));
    attr.size = sizeof(attr);
    attr.type = lookup_i915_oa_id();

    attr.sample_type = PERF_SAMPLE_RAW;
    attr.sample_period = 1;

    attr.watermark = true;
    attr.wakeup_watermark = perf_buffer_size / 4;

    memset(&oa_attr, 0, sizeof(oa_attr));
    oa_attr.size = sizeof(oa_attr);

    oa_attr.format = query->perf_oa_format;
    oa_attr.metrics_set = query->perf_oa_metrics_set;
    oa_attr.timer_exponent = period_exponent;

    attr.config = (uint64_t)&oa_attr;

    event_fd = perf_event_open(&attr,
			       -1, /* pid */
			       0, /* cpu */
			       -1, /* group fd */
			       PERF_FLAG_FD_CLOEXEC); /* flags */
    if (event_fd == -1) {
	asprintf(error, "Error opening i915_oa perf event: %m\n");
	return NULL;
    }

    /* NB: A read-write mapping ensures the kernel will stop writing data when
     * the buffer is full, and will report samples as lost. */
    mmap_base = mmap(NULL,
		     perf_buffer_size + page_size,
		     PROT_READ | PROT_WRITE, MAP_SHARED, event_fd, 0);
    if (mmap_base == MAP_FAILED) {
	asprintf(error, "Error mapping circular buffer, %m\n");
	close (event_fd);
	return NULL;
    }

    stream = xmalloc0(sizeof(*stream));
    stream->ref_count = 1;
    stream->query = query;
    stream->fd = event_fd;
    stream->buffer = mmap_base + page_size;
    stream->buffer_size = perf_buffer_size;
    stream->mmap_page = (void *)mmap_base;

    expected_max_samples =
	(stream->buffer_size / MAX_OA_PERF_SAMPLE_SIZE) * 1.2;

    memset(&stream->header_buf, 0, sizeof(stream->header_buf));

    stream->overwrite = overwrite;
    if (overwrite) {
	stream->header_buf.len = expected_max_samples;
	stream->header_buf.offsets =
	    xmalloc(sizeof(uint32_t) * expected_max_samples);
    }

    stream->fd_poll.data = stream;
    uv_poll_init(gputop_ui_loop, &stream->fd_poll, stream->fd);
    uv_poll_start(&stream->fd_poll, UV_READABLE, ready_cb);

    return stream;
}

struct gputop_perf_stream *
gputop_perf_open_trace(int pid,
		       int cpu,
		       const char *system,
		       const char *event,
		       size_t trace_struct_size,
		       size_t perf_buffer_size,
		       void (*ready_cb)(uv_poll_t *poll, int status, int events),
		       bool overwrite,
		       char **error)
{
    struct gputop_perf_stream *stream;
    struct perf_event_attr attr;
    int event_fd;
    uint8_t *mmap_base;
    int expected_max_samples;
    char *filename = NULL;
    int id = 0;
    size_t sample_size = 0;

    asprintf(&filename, "/sys/kernel/debug/tracing/events/%s/%s/id", system, event);
    if (filename) {
	struct stat st;

	if (stat(filename, &st) < 0) {
	    int err = errno;

	    free(filename);
	    filename = NULL;

	    if (err == EPERM) {
		asprintf(error, "Permission denied to open tracepoint %s:%s"
			 " (Linux tracepoints require root privileges)",
			 system, event);
		return NULL;
	    } else {
		asprintf(error, "Failed to open tracepoint %s:%s: %s",
			 system, event,
			 strerror(err));
		return NULL;
	    }
	}
    }

    id = read_file_uint64(filename);
    free(filename);
    filename = NULL;

    memset(&attr, 0, sizeof(attr));
    attr.size = sizeof(attr);
    attr.type = PERF_TYPE_TRACEPOINT;
    attr.config = id;

    attr.sample_type = PERF_SAMPLE_RAW | PERF_SAMPLE_TIME;
    attr.sample_period = 1;

    attr.watermark = true;
    attr.wakeup_watermark = perf_buffer_size / 4;

    event_fd = perf_event_open(&attr,
			       pid,
			       cpu,
			       -1, /* group fd */
			       PERF_FLAG_FD_CLOEXEC); /* flags */
    if (event_fd == -1) {
	asprintf(error, "Error opening i915_oa perf event: %m\n");
	return NULL;
    }

    /* NB: A read-write mapping ensures the kernel will stop writing data when
     * the buffer is full, and will report samples as lost. */
    mmap_base = mmap(NULL,
		     perf_buffer_size + page_size,
		     PROT_READ | PROT_WRITE, MAP_SHARED, event_fd, 0);
    if (mmap_base == MAP_FAILED) {
	asprintf(error, "Error mapping circular buffer, %m\n");
	close (event_fd);
	return NULL;
    }

    stream = xmalloc0(sizeof(*stream));
    stream->ref_count = 1;
    stream->fd = event_fd;
    stream->buffer = mmap_base + page_size;
    stream->buffer_size = perf_buffer_size;
    stream->mmap_page = (void *)mmap_base;

    sample_size =
	sizeof(struct perf_event_header) +
	8 /* _TIME */ +
	trace_struct_size; /* _RAW */

    expected_max_samples = (stream->buffer_size / sample_size) * 1.2;

    memset(&stream->header_buf, 0, sizeof(stream->header_buf));

    stream->overwrite = overwrite;
    if (overwrite) {
	stream->header_buf.len = expected_max_samples;
	stream->header_buf.offsets =
	    xmalloc(sizeof(uint32_t) * expected_max_samples);
    }

    stream->fd_poll.data = stream;
    uv_poll_init(gputop_ui_loop, &stream->fd_poll, stream->fd);
    uv_poll_start(&stream->fd_poll, UV_READABLE, ready_cb);

    return stream;
}

struct gputop_perf_stream *
gputop_perf_open_generic_counter(int pid,
				 int cpu,
				 uint64_t type,
				 uint64_t config,
				 size_t perf_buffer_size,
				 void (*ready_cb)(uv_poll_t *poll, int status, int events),
				 bool overwrite,
				 char **error)
{
    struct gputop_perf_stream *stream;
    struct perf_event_attr attr;
    int event_fd;
    uint8_t *mmap_base;
    int expected_max_samples;
    size_t sample_size = 0;

    memset(&attr, 0, sizeof(attr));
    attr.size = sizeof(attr);
    attr.type = type;
    attr.config = config;

    attr.sample_type = PERF_SAMPLE_READ | PERF_SAMPLE_TIME;
    attr.sample_period = 1;

    attr.watermark = true;
    attr.wakeup_watermark = perf_buffer_size / 4;

    event_fd = perf_event_open(&attr,
			       pid,
			       cpu,
			       -1, /* group fd */
			       PERF_FLAG_FD_CLOEXEC); /* flags */
    if (event_fd == -1) {
	asprintf(error, "Error opening i915_oa perf event: %m\n");
	return NULL;
    }

    /* NB: A read-write mapping ensures the kernel will stop writing data when
     * the buffer is full, and will report samples as lost. */
    mmap_base = mmap(NULL,
		     perf_buffer_size + page_size,
		     PROT_READ | PROT_WRITE, MAP_SHARED, event_fd, 0);
    if (mmap_base == MAP_FAILED) {
	asprintf(error, "Error mapping circular buffer, %m\n");
	close (event_fd);
	return NULL;
    }

    stream = xmalloc0(sizeof(*stream));
    stream->ref_count = 1;
    stream->fd = event_fd;
    stream->buffer = mmap_base + page_size;
    stream->buffer_size = perf_buffer_size;
    stream->mmap_page = (void *)mmap_base;

    sample_size =
	sizeof(struct perf_event_header) +
	8; /* _TIME */
    expected_max_samples = (stream->buffer_size / sample_size) * 1.2;

    memset(&stream->header_buf, 0, sizeof(stream->header_buf));

    stream->overwrite = overwrite;
    if (overwrite) {
	stream->header_buf.len = expected_max_samples;
	stream->header_buf.offsets =
	    xmalloc(sizeof(uint32_t) * expected_max_samples);
    }

    stream->fd_poll.data = stream;
    uv_poll_init(gputop_ui_loop, &stream->fd_poll, stream->fd);
    uv_poll_start(&stream->fd_poll, UV_READABLE, ready_cb);

    return stream;
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

static void
init_dev_info(int drm_fd, uint32_t devid)
{
    gputop_devinfo.devid = devid;

    if (IS_HSW_GT1(devid)) {
	gputop_devinfo.n_eus = 10;
	gputop_devinfo.n_eu_slices = 1;
	gputop_devinfo.n_eu_sub_slices = 1;
	gputop_devinfo.n_samplers = 1;
    } else if (IS_HSW_GT2(devid)) {
	gputop_devinfo.n_eus = 20;
	gputop_devinfo.n_eu_slices = 1;
	gputop_devinfo.n_eu_sub_slices = 2;
	gputop_devinfo.n_samplers = 2;
    } else if (IS_HSW_GT3(devid)) {
	gputop_devinfo.n_eus = 40;
	gputop_devinfo.n_eu_slices = 2;
	gputop_devinfo.n_eu_sub_slices = 4;
	gputop_devinfo.n_samplers = 4;
    } else {
#ifdef I915_PARAM_EU_TOTAL
	i915_getparam_t gp;
	int ret;
	int n_eus;

	gp.param = I915_PARAM_EU_TOTAL;
	gp.value = &n_eus;
	ret = perf_ioctl(drm_fd, I915_IOCTL_GETPARAM, &gp);
	assert(ret == 0 && n_eus> 0);

	gputop_devinfo.n_eus = n_eus;

#warning "XXX: BDW: initialize gputop_devinfo.n_eu_slices + n_samplers - though not currently needed"

#else
	assert(0);
#endif
    }
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

/* Perf supports a flight recorder mode whereby it won't stop writing
 * samples once the buffer is full and will instead overwrite old
 * samples.
 *
 * The difficulty with this mode is that because samples don't have a
 * uniform size, once the head gets trampled we can no longer parse
 * *any* samples since the location of each sample depends of the
 * length of the previous.
 *
 * Since we are paranoid about wasting memory bandwidth - as such a
 * common gpu bottleneck - we would rather not resort to copying
 * samples into another buffer, especially to implement a tracing
 * feature where higher sampler frequencies are interesting.
 *
 * To simplify things to handle the main case we care about where
 * the perf circular buffer is full of samples (as opposed to
 * lots of throttle or status records) we can define a fixed number
 * of pointers to track, given the size of the perf buffer and
 * known size for samples. These can be tracked in a circular
 * buffer with fixed size records where overwriting the head isn't
 * a problem.
 */

/*
 * For each update of this buffer we:
 *
 * 1) Check what new records have been added:
 *
 *    * if buf->last_perf_head uninitialized, set it to the perf tail
 *    * foreach new record from buf->last_perf_head to the current perf head:
 *        - check there's room for a new header offset, but if not:
 *            - report an error
 *            - move the tail forward (loosing a record)
 *        - add a header offset to buf->offsets[buf->head]
 *        - buf->head++;
 *        - recognise when the perf head wraps and mark the buffer 'full'
 *
 * 2) Optionally parse any of the new records (i.e. before we update
 *    tail)
 *
 *    Typically we aren't processing the records while tracing, but
 *    beware that if anything does need passing on the fly then it
 *    needs to be done before we update the tail pointer below.
 *
 * 3) If buf 'full'; check how much of perf's tail has been eaten:
 *
 *    * move buf->tail forward to the next offset that is ahead of
 *      perf's (head + header->size)
 *        XXX: we can assert() that we don't overtake buf->head. That
 *        shouldn't be possible if we aren't enabling perf's
 *        overwriting/flight recorder mode.
 *	  XXX: Note: we do this after checking for new records so we
 *	  don't have to worry about the corner case of eating more
 *	  than we previously knew about.
 *
 * 4) Set perf's tail to perf's head (i.e. consume everything so that
 *    perf won't block when wrapping around and overwriting old
 *    samples.)
 */
void
gputop_perf_update_header_offsets(struct gputop_perf_stream *stream)
{
    struct gputop_perf_header_buf *hdr_buf  = &stream->header_buf;
    uint8_t *data = stream->buffer;
    const uint64_t mask = stream->buffer_size - 1;
    uint64_t perf_head;
    uint64_t perf_tail;
    uint32_t buf_head;
    uint32_t buf_tail;
    uint32_t n_new = 0;

    perf_head = read_perf_head(stream->mmap_page);

    //if (hdr_buf->head == hdr_buf->tail)
	//perf_tail = hdr_buf->last_perf_head;
    //else
	perf_tail = stream->mmap_page->data_tail;

#if 0
    if (perf_tail > perf_head) {
	dbg("Unexpected perf tail > head condition\n");
	return;
    }
#endif

    if (perf_head == perf_tail)
	return;

    //hdr_buf->last_perf_head = perf_head;

    buf_head = hdr_buf->head;
    buf_tail = hdr_buf->tail;

#if 1
    printf("perf records:\n");
    printf("> fd = %d\n", stream->fd);
    printf("> size = %d\n", stream->buffer_size);
    printf("> tail_ptr = %p\n", &stream->mmap_page->data_tail);
    printf("> head=%"PRIu64"\n", perf_head);
    printf("> tail=%"PRIu64"\n", stream->mmap_page->data_tail);
    printf("> TAKEN=%"PRIu64"\n", (uint64_t)TAKEN(perf_head, stream->mmap_page->data_tail, stream->buffer_size));
    printf("> records:\n");
#endif

    while (TAKEN(perf_head, perf_tail, stream->buffer_size)) {
	uint64_t perf_offset = perf_tail & mask;
	const struct perf_event_header *header =
	    (const struct perf_event_header *)(data + perf_offset);

	n_new++;

	if (header->size == 0) {
	    dbg("Spurious header size == 0\n");
	    /* XXX: How should we handle this instead of exiting() */
	    break;
	    //exit(1);
	}

	if (header->size > (perf_head - perf_tail)) {
	    dbg("Spurious header size would overshoot head\n");
	    /* XXX: How should we handle this instead of exiting() */
	    break;
	    //exit(1);
	}

	/* Once perf wraps, the buffer is full of data and perf starts
	 * to eat its tail, overwriting old data. */
	if ((const uint8_t *)header + header->size > data + stream->buffer_size)
	    hdr_buf->full = true;

	if ((buf_head - buf_tail) == hdr_buf->len)
	    buf_tail++;

	/* Checking what tail records have been being overwritten by this
	 * new record...
	 *
	 * NB: A record may be split at the end of the buffer
	 * NB: A large record may trample multiple smaller records
	 * NB: it's possible no records have been trampled
	 */
	if (hdr_buf->full) {
	    while (1) {
		uint32_t buf_tail_offset = hdr_buf->offsets[buf_tail % hdr_buf->len];

		/* To simplify checking for an overlap, invariably ensure the
		 * buf_tail_offset is ahead of perf, even if it means using a
		 * fake offset beyond the bounds of the buffer... */
		if (buf_tail_offset < perf_offset)
		    buf_tail_offset += stream->buffer_size;

		if ((perf_offset + header->size) < buf_tail_offset) /* nothing eaten */
		    break;

		buf_tail++;
	    }
	}

	hdr_buf->offsets[buf_head++ % hdr_buf->len] = perf_offset;

	perf_tail += header->size;
    }

    /* Consume all perf records so perf wont be blocked from
     * overwriting old samples... */
    write_perf_tail(stream->mmap_page, perf_head);

    hdr_buf->head = buf_head;
    hdr_buf->tail = buf_tail;

#if 1
    printf("headers update:\n");
    printf("n new records = %d\n", n_new);
    printf("buf len = %d\n", hdr_buf->len);
    printf("perf head = %"PRIu64"\n", perf_head);
    printf("perf tail = %"PRIu64"\n", perf_tail);
    printf("buf head = %"PRIu32"\n", hdr_buf->head);
    printf("buf tail = %"PRIu32"\n", hdr_buf->tail);

    if (!hdr_buf->full) {
	float percentage = (hdr_buf->offsets[(hdr_buf->head - 1) % hdr_buf->len] / (float)stream->buffer_size) * 100.0f;
	printf("> %d%% full\n", (int)percentage);
    } else
	printf("> 100%% full\n");

    printf("> n records = %u\n", (hdr_buf->head - hdr_buf->tail));
#endif
}

void
gputop_perf_print_records(struct gputop_perf_stream *stream,
			  uint64_t head,
			  uint64_t tail,
			  bool sample_data)
{
    uint8_t *data = stream->buffer;
    const uint64_t mask = stream->buffer_size - 1;
    uint8_t scratch[MAX_OA_PERF_SAMPLE_SIZE];

    /* FIXME: since we only really care about the most recent sample
     * we should first figure out how many samples we have between
     * tail and head so we can skip all but the last sample */

    //fprintf(stderr, "Handle event mask = 0x%" PRIx64
    //        " head=%" PRIu64 " tail=%" PRIu64 "\n", mask, head, tail);

    printf("perf records:\n");
    printf("> head=%"PRIu64"\n", head);
    printf("> tail=%"PRIu64"\n", tail);
    printf("> TAKEN=%"PRIu64"\n", (uint64_t)TAKEN(head, tail, stream->buffer_size));
    printf("> records:\n");

    while (TAKEN(head, tail, stream->buffer_size)) {
	const struct perf_event_header *header =
	    (const struct perf_event_header *)(data + (tail & mask));

	printf("   - header size = %d\n", header->size);

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

	if ((const uint8_t *)header + header->size > data + stream->buffer_size) {
	    int before;

	    if (header->size > MAX_OA_PERF_SAMPLE_SIZE) {
		printf("   - Skipping spurious sample larger than expected\n");
		tail += header->size;
		continue;
	    }

	    before = data + stream->buffer_size - (const uint8_t *)header;

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
	    printf("   - Lost %" PRIu64 " events\n", lost->n_lost);
	    break;
	}

	case PERF_RECORD_THROTTLE:
	    printf("   - Sampling has been throttled\n");
	    break;

	case PERF_RECORD_UNTHROTTLE:
	    printf("   - Sampling has been unthrottled\n");
	    break;

	case PERF_RECORD_DEVICE: {
	    struct i915_oa_event {
		struct perf_event_header header;
		i915_oa_event_header_t oa_header;
	    } *oa_event = (void *)header;

	    switch (oa_event->oa_header.type) {
	    case I915_OA_RECORD_BUFFER_OVERFLOW:
		printf("   - i915_oa: OA buffer overflow\n");
		break;
	    case I915_OA_RECORD_REPORT_LOST:
		printf("   - i915_oa: OA report lost\n");
		break;
	    }
	    break;
	}

	case PERF_RECORD_SAMPLE: {
	    //struct oa_perf_sample *perf_sample = (struct oa_perf_sample *)header;
	    //uint8_t *report = (uint8_t *)perf_sample->raw_data;

	    printf("   - Sample\n");

	    if (sample_data) {

	    }
	    break;
	}

	default:
	    printf("   - Spurious header type = %d\n", header->type);
	}

	//fprintf(stderr, "Tail += %d\n", header->size);
	tail += header->size;
    }
}

void
gputop_perf_read_samples(struct gputop_perf_stream *stream)
{
    struct gputop_perf_query *query = stream->query;
    uint8_t *data = stream->buffer;
    const uint64_t mask = stream->buffer_size - 1;
    uint64_t head;
    uint64_t tail;
    uint8_t *last = NULL;
    uint64_t last_tail;
    uint8_t scratch[MAX_OA_PERF_SAMPLE_SIZE];

    if (fsync(stream->fd) < 0)
	dbg("Failed to flush i915_oa perf samples");

    head = read_perf_head(stream->mmap_page);
    tail = last_tail = stream->mmap_page->data_tail;

    /* FIXME: since we only really care about the most recent sample
     * we should first figure out how many samples we have between
     * tail and head so we can skip all but the last sample */

    //fprintf(stderr, "Handle event mask = 0x%" PRIx64
    //        " head=%" PRIu64 " tail=%" PRIu64 "\n", mask, head, tail);

    while (TAKEN(head, tail, stream->buffer_size)) {
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

	if ((const uint8_t *)header + header->size > data + stream->buffer_size) {
	    int before;

	    if (header->size > MAX_OA_PERF_SAMPLE_SIZE) {
		dbg("Skipping spurious sample larger than expected\n");
		tail += header->size;
		continue;
	    }

	    before = data + stream->buffer_size - (const uint8_t *)header;

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

	case PERF_RECORD_DEVICE: {
	    struct i915_oa_event {
		struct perf_event_header header;
		i915_oa_event_header_t oa_header;
	    } *oa_event = (void *)header;

	    switch (oa_event->oa_header.type) {
	    case I915_OA_RECORD_BUFFER_OVERFLOW:
		dbg("i915_oa: OA buffer overflow\n");
		break;
	    case I915_OA_RECORD_REPORT_LOST:
		dbg("i915_oa: OA report lost\n");
		break;
	    }
	    break;
	}

	case PERF_RECORD_SAMPLE: {
	    struct oa_perf_sample *perf_sample = (struct oa_perf_sample *)header;
	    uint8_t *report = (uint8_t *)perf_sample->raw_data;

	    if (last)
		current_user->sample(query, last, report);

	    last = report;
	    last_tail = tail;
	    break;
	}

	default:
	    dbg("i915_oa: Spurious header type = %d\n", header->type);
	}

	//fprintf(stderr, "Tail += %d\n", header->size);
	tail += header->size;
    }

    /* XXX: we don't progress the tail past the last report so that we
     * can pick up where we left off later. Note: this means there's a
     * slim chance we might parse some _LOST, _THROTTLE and
     * _UNTHROTTLE records twice, but currently they are benign
     * anyway. */
    write_perf_tail(stream->mmap_page, last_tail);
}

/******************************************************************************/

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

uint64_t
read_report_timestamp(const uint32_t *report)
{
   uint64_t timestamp = report[1];

   /* The least significant timestamp bit represents 80ns */
   timestamp *= 80;

   return timestamp;
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

bool
gputop_perf_initialize(void)
{
    int drm_fd;

    if (intel_dev.device)
	return true;

    drm_fd = open_render_node(&intel_dev);
    if (drm_fd < 0) {
	gputop_log(GPUTOP_LOG_LEVEL_HIGH, "Failed to open render node", -1);
	return false;
    }

    /* NB: eu_count needs to be initialized before declaring counters */
    init_dev_info(drm_fd, intel_dev.device);
    page_size = sysconf(_SC_PAGE_SIZE);

    close(drm_fd);

    if (IS_HASWELL(intel_dev.device)) {
	gputop_oa_add_render_basic_counter_query_hsw();
	gputop_oa_add_compute_basic_counter_query_hsw();
	gputop_oa_add_compute_extended_counter_query_hsw();
	gputop_oa_add_memory_reads_counter_query_hsw();
	gputop_oa_add_memory_writes_counter_query_hsw();
	gputop_oa_add_sampler_balance_counter_query_hsw();
    } else if (IS_BROADWELL(intel_dev.device)) {
	gputop_oa_add_render_basic_counter_query_bdw();
    } else
	assert(0);

    return true;
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
    int idx = 0;
    int i;

    switch (query->perf_oa_format) {
    case I915_OA_FORMAT_A32u40_A4u32_B8_C8:
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

    case I915_OA_FORMAT_A45_B8_C8:
	accumulate_uint32(start + 1, end + 1, accumulator); /* timestamp */

	for (i = 0; i < 61; i++)
	    accumulate_uint32(start + 3 + i, end + 3 + i, accumulator + 1 + i);
	break;
    default:
	assert(0);
    }
}

/**
 * Given pointers to starting and ending OA snapshots, calculate the deltas for each
 * counter to update the results.
 */
static void
overview_sample_cb(struct gputop_perf_query *query, uint8_t *start, uint8_t *end)
{
    gputop_perf_accumulate(query, start, end);
}

void
gputop_perf_accumulator_clear(struct gputop_perf_query *query)
{
    memset(query->accumulator, 0, sizeof(query->accumulator));
}

static struct perf_oa_user overview_user = {
    .sample = overview_sample_cb,
};

bool
gputop_perf_overview_open(gputop_perf_query_type_t query_type)
{
    int period_exponent;
    char *error = NULL;

    assert(gputop_current_perf_query == NULL);

    if (!gputop_perf_initialize())
	return false;

    current_user = &overview_user;

    gputop_current_perf_query = &perf_queries[query_type];

    gputop_perf_accumulator_clear(gputop_current_perf_query);

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

    gputop_current_perf_stream =
	gputop_perf_open_i915_oa_query(gputop_current_perf_query,
				       period_exponent,
				       32 * page_size,
				       perf_ready_cb,
				       false,
				       &error);
    if (!gputop_current_perf_stream) {
	gputop_log(GPUTOP_LOG_LEVEL_HIGH, error, -1);
	free(error);

	gputop_current_perf_query = NULL;
	return false;
    }

    return true;
}

void
gputop_perf_overview_close(void)
{
    if (!gputop_current_perf_query)
	return;

    gputop_perf_stream_unref(gputop_current_perf_stream);

    gputop_current_perf_query = NULL;
    gputop_current_perf_stream = NULL;
}

int gputop_perf_trace_buffer_size;
uint8_t *gputop_perf_trace_buffer;
bool gputop_perf_trace_empty;
bool gputop_perf_trace_full;
uint8_t *gputop_perf_trace_head;
int gputop_perf_n_samples = 0;

static void
trace_sample_cb(struct gputop_perf_query *query, uint8_t *start, uint8_t *end)
{
    int sample_size = query->perf_raw_size;

    if (gputop_perf_trace_empty) {
	memcpy(gputop_perf_trace_head, start, sample_size);
	gputop_perf_trace_head += sample_size;
	gputop_perf_trace_empty = false;
    }

    memcpy(gputop_perf_trace_head, end, sample_size);

    gputop_perf_trace_head += sample_size;
    if (gputop_perf_trace_head >= (gputop_perf_trace_buffer + gputop_perf_trace_buffer_size)) {
	gputop_perf_trace_head = gputop_perf_trace_buffer;
	gputop_perf_trace_full = true;
    }

    if (!gputop_perf_trace_full)
	gputop_perf_n_samples++;
}

static struct perf_oa_user trace_user = {
    .sample = trace_sample_cb,
};

bool
gputop_perf_oa_trace_open(gputop_perf_query_type_t query_type)
{
    int period_exponent;
    double duration = 5.0; /* seconds */
    uint64_t period_ns;
    uint64_t n_samples;
    char *error = NULL;

    assert(gputop_current_perf_query == NULL);

    if (!gputop_perf_initialize())
	return false;

    current_user = &trace_user;

    gputop_current_perf_query = &perf_queries[query_type];

    /* The timestamp for HSW+ increments every 80ns
     *
     * The period_exponent gives a sampling period as follows:
     *   sample_period = 80ns * 2^(period_exponent + 1)
     *
     * Sample ~ every 1 millisecond...
     */
    period_exponent = 11;

    gputop_current_perf_stream =
	gputop_perf_open_i915_oa_query(gputop_current_perf_query,
				       period_exponent,
				       32 * page_size,
				       perf_ready_cb,
				       false,
				       &error);
    if (!gputop_current_perf_stream) {
	gputop_log(GPUTOP_LOG_LEVEL_HIGH, error, -1);
	free(error);

	gputop_current_perf_query = NULL;
	return false;
    }

    period_ns = 80 * (2 << period_exponent);
    n_samples = (duration  * 1000000000.0) / period_ns;
    n_samples *= 1.25; /* a bit of leeway */

    gputop_perf_trace_buffer_size = n_samples * gputop_current_perf_query->perf_raw_size;
    gputop_perf_trace_buffer = xmalloc0(gputop_perf_trace_buffer_size);
    gputop_perf_trace_head = gputop_perf_trace_buffer;
    gputop_perf_trace_empty = true;
    gputop_perf_trace_full = false;

    return true;
}

void
gputop_perf_oa_trace_close(void)
{
    if (!gputop_current_perf_query)
	return;

    gputop_perf_stream_unref(gputop_current_perf_stream);

    gputop_current_perf_query = NULL;
    gputop_current_perf_stream = NULL;
}
