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
#include <poll.h>

#include <uv.h>
#include <dirent.h>

#include "intel_chipset.h"

#include "gputop-util.h"
#include "gputop-list.h"
#include "gputop-ui.h"
#include "gputop-perf.h"
#include "gputop-oa-counters.h"

#include "oa-hsw.h"
#include "oa-bdw.h"
#include "oa-chv.h"
#include "oa-skl.h"


/* Samples read() from i915 perf */
struct oa_sample {
   struct i915_perf_record_header header;
   uint8_t oa_report[];
};

#define MAX_I915_PERF_OA_SAMPLE_SIZE (8 +   /* i915_perf_record_header */ \
				      256)  /* raw OA counter snapshot */


#define TAKEN(HEAD, TAIL, POT_SIZE)    (((HEAD) - (TAIL)) & (POT_SIZE - 1))

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
#define PERF_RECORD_DEVICE      14
#endif
#ifndef PERF_FLAG_FD_CLOEXEC
#define PERF_FLAG_FD_CLOEXEC   (1UL << 3) /* O_CLOEXEC */
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
    void (*sample)(struct gputop_perf_stream *stream, uint8_t *start, uint8_t *end);
};

bool gputop_fake_mode = false;

static struct perf_oa_user *current_user;

static struct intel_device intel_dev;

static unsigned int page_size;

struct gputop_hash_table *queries;
struct array *perf_oa_supported_query_guids;
struct gputop_perf_query *gputop_current_perf_query;
struct gputop_perf_stream *gputop_current_perf_stream;

static int drm_fd = -1;

static gputop_list_t ctx_handles_list = {
    .prev = &ctx_handles_list,
    .next= &ctx_handles_list
};

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

bool gputop_add_ctx_handle(int ctx_fd, uint32_t ctx_id)
{
    struct ctx_handle *handle = xmalloc0(sizeof(*handle));
    if (!handle) {
        return false;
    }
    handle->id = ctx_id;
    handle->fd = ctx_fd;

    gputop_list_insert(&ctx_handles_list, &handle->link);

    return true;
}

bool gputop_remove_ctx_handle(uint32_t ctx_id)
{
    struct ctx_handle *ctx;
    gputop_list_for_each(ctx, &ctx_handles_list, link) {
        if (ctx->id == ctx_id) {
            gputop_list_remove(&ctx->link);
            free(ctx);
            return true;
        }
    }
    return false;
}

struct ctx_handle *lookup_ctx_handle(uint32_t ctx_id)
{
    struct ctx_handle *ctx = NULL;
    gputop_list_for_each(ctx, &ctx_handles_list, link) {
        if (ctx->id == ctx_id) {
            break;
        }
    }
    return ctx;
}

/* Handle restarting ioctl if interrupted... */
static int
perf_ioctl(int fd, unsigned long request, void *arg)
{
    int ret;

    do {
	ret = ioctl(fd, request, arg);
    } while (ret == -1 && (errno == EINTR || errno == EAGAIN));
    return ret;
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

    if (stream->ready_cb)
        stream->ready_cb(stream);
}

static void
perf_fake_ready_cb(uv_timer_t *poll)
{
    struct gputop_perf_stream *stream = poll->data;

    if (stream->ready_cb)
        stream->ready_cb(stream);
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

	switch(stream->type) {
	case GPUTOP_STREAM_PERF:
	    if (stream->fd > 0) {
		uv_poll_stop(&stream->fd_poll);

		if (stream->perf.mmap_page) {
		    munmap(stream->perf.mmap_page, stream->perf.buffer_size + page_size);
		    stream->perf.mmap_page = NULL;
		    stream->perf.buffer = NULL;
		    stream->perf.buffer_size = 0;
		}

		if (stream->perf.header_buf.offsets) {
		    free(stream->perf.header_buf.offsets);
		    stream->perf.header_buf.offsets = NULL;
		}

		close(stream->fd);
		stream->fd = -1;

		fprintf(stderr, "closed perf stream\n");
	    }

	    break;
	case GPUTOP_STREAM_I915_PERF:
            if (stream->fd == -1) {
                uv_timer_stop(&stream->fd_timer);

                if (stream->oa.bufs[0])
		    free(stream->oa.bufs[0]);
		if (stream->oa.bufs[1])
		    free(stream->oa.bufs[1]);
                fprintf(stderr, "closed i915 fake perf stream\n");
            }
	    if (stream->fd > 0) {
		uv_poll_stop(&stream->fd_poll);

		if (stream->oa.bufs[0])
		    free(stream->oa.bufs[0]);
		if (stream->oa.bufs[1])
		    free(stream->oa.bufs[1]);

		close(stream->fd);
		stream->fd = -1;

		fprintf(stderr, "closed i915 perf stream\n");
	    }

	    break;
	}

	if (stream->user.destroy_cb)
	    stream->user.destroy_cb(stream);
	free(stream);
	fprintf(stderr, "freed gputop-perf stream\n");
    }
}

uint64_t
get_time(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000000000 + (uint64_t)t.tv_nsec;
}

struct gputop_perf_stream *
gputop_open_i915_perf_oa_query(struct gputop_perf_query *query,
			       int period_exponent,
			       size_t perf_buffer_size,
			       void (*ready_cb)(struct gputop_perf_stream *),
			       bool overwrite,
			       char **error)
{
    struct gputop_perf_stream *stream;
    struct i915_perf_open_param param;
    int stream_fd = -1;

    if (!gputop_fake_mode) {
	uint64_t properties[DRM_I915_PERF_PROP_MAX * 2];
	int p = 0;

	memset(&param, 0, sizeof(param));

	param.flags = 0;
	param.flags |= I915_PERF_FLAG_FD_CLOEXEC;
	param.flags |= I915_PERF_FLAG_FD_NONBLOCK;

	properties[p++] = DRM_I915_PERF_SAMPLE_OA_PROP;
	properties[p++] = true;

	properties[p++] = DRM_I915_PERF_OA_METRICS_SET_PROP;
	properties[p++] = query->perf_oa_metrics_set;

	properties[p++] = DRM_I915_PERF_OA_FORMAT_PROP;
	properties[p++] = query->perf_oa_format;

	properties[p++] = DRM_I915_PERF_OA_EXPONENT_PROP;
	properties[p++] = period_exponent;

	if (query->per_ctx_mode) {
	    struct ctx_handle *ctx;

	    // TODO: (matt-auld)
	    // Currently we don't support selectable contexts, so we just use the
	    // first one which is avaivable to us. Though this would only really
	    // make sense if we could make the list of contexts visible to the user.
	    // Maybe later the per_ctx_mode could become the context handle...
	    ctx = gputop_list_first(&ctx_handles_list, struct ctx_handle, link);
	    if (!ctx)
	      return NULL;

	    properties[p++] = DRM_I915_PERF_CTX_HANDLE_PROP;
	    properties[p++] = ctx->id;
	}

	param.properties = (uint64_t)properties;
	param.n_properties = p / 2;

	stream_fd = perf_ioctl(drm_fd, I915_IOCTL_PERF_OPEN, &param);
        if (stream_fd == -1) {
	    asprintf(error, "Error opening i915 perf OA event: %m\n");
	    return NULL;
        }
    }

    stream = xmalloc0(sizeof(*stream));
    stream->type = GPUTOP_STREAM_I915_PERF;
    stream->ref_count = 1;
    stream->query = query;
    stream->ready_cb = ready_cb;

    stream->fd = stream_fd;

    if (gputop_fake_mode) {
        stream->start_time = get_time();
        stream->prev_clocks = (uint32_t)get_time();
        stream->period = 80 * (2 << period_exponent);
        stream->prev_timestamp = (uint32_t)get_time();
    }

    /* We double buffer the samples we read from the kernel so
     * we can maintain a stream->last pointer for calculating
     * counter deltas */
    stream->oa.buf_sizes = MAX_I915_PERF_OA_SAMPLE_SIZE * 100;
    stream->oa.bufs[0] = xmalloc0(stream->oa.buf_sizes);
    stream->oa.bufs[1] = xmalloc0(stream->oa.buf_sizes);

    stream->overwrite = overwrite;
    if (overwrite) {
#warning "TODO: support flight-recorder mode"
	assert(0);
    }

    stream->fd_poll.data = stream;
    stream->fd_timer.data = stream;

    if (gputop_fake_mode)
    {
	uv_timer_init(gputop_ui_loop, &stream->fd_timer);
        uv_timer_start(&stream->fd_timer, perf_fake_ready_cb, 1000, 1000);
    }
    else
    {
	uv_poll_init(gputop_ui_loop, &stream->fd_poll, stream->fd);
	uv_poll_start(&stream->fd_poll, UV_READABLE, perf_ready_cb);
    }


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
	asprintf(error, "Error opening perf tracepoint event: %m\n");
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
    stream->type = GPUTOP_STREAM_PERF;
    stream->ref_count = 1;
    stream->fd = event_fd;
    stream->perf.buffer = mmap_base + page_size;
    stream->perf.buffer_size = perf_buffer_size;
    stream->perf.mmap_page = (void *)mmap_base;

    sample_size =
	sizeof(struct perf_event_header) +
	8 /* _TIME */ +
	trace_struct_size; /* _RAW */

    expected_max_samples = (stream->perf.buffer_size / sample_size) * 1.2;

    memset(&stream->perf.header_buf, 0, sizeof(stream->perf.header_buf));

    stream->overwrite = overwrite;
    if (overwrite) {
	stream->perf.header_buf.len = expected_max_samples;
	stream->perf.header_buf.offsets =
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
	asprintf(error, "Error opening perf event: %m\n");
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
    stream->type = GPUTOP_STREAM_PERF;
    stream->ref_count = 1;
    stream->fd = event_fd;
    stream->perf.buffer = mmap_base + page_size;
    stream->perf.buffer_size = perf_buffer_size;
    stream->perf.mmap_page = (void *)mmap_base;

    sample_size =
	sizeof(struct perf_event_header) +
	8; /* _TIME */
    expected_max_samples = (stream->perf.buffer_size / sample_size) * 1.2;

    memset(&stream->perf.header_buf, 0, sizeof(stream->perf.header_buf));

    stream->overwrite = overwrite;
    if (overwrite) {
	stream->perf.header_buf.len = expected_max_samples;
	stream->perf.header_buf.offsets =
	    xmalloc(sizeof(uint32_t) * expected_max_samples);
    }

    stream->fd_poll.data = stream;
    uv_poll_init(gputop_ui_loop, &stream->fd_poll, stream->fd);
    uv_poll_start(&stream->fd_poll, UV_READABLE, ready_cb);

    return stream;
}

static void
init_dev_info(int drm_fd, uint32_t devid)
{
    int threads_per_eu = 7;

    gputop_devinfo.devid = devid;
    if (gputop_fake_mode) {
	    gputop_devinfo.n_eus = 10;
	    gputop_devinfo.n_eu_slices = 1;
	    gputop_devinfo.n_eu_sub_slices = 1;
	    gputop_devinfo.slice_mask = 0x1;
	    gputop_devinfo.subslice_mask = 0x1;
    } else if (IS_HASWELL(devid)) {
	if (IS_HSW_GT1(devid)) {
	    gputop_devinfo.n_eus = 10;
	    gputop_devinfo.n_eu_slices = 1;
	    gputop_devinfo.n_eu_sub_slices = 1;
	    gputop_devinfo.slice_mask = 0x1;
	    gputop_devinfo.subslice_mask = 0x1;
	} else if (IS_HSW_GT2(devid)) {
	    gputop_devinfo.n_eus = 20;
	    gputop_devinfo.n_eu_slices = 1;
	    gputop_devinfo.n_eu_sub_slices = 2;
	    gputop_devinfo.slice_mask = 0x1;
	    gputop_devinfo.subslice_mask = 0x3;
	} else if (IS_HSW_GT3(devid)) {
	    gputop_devinfo.n_eus = 40;
	    gputop_devinfo.n_eu_slices = 2;
	    gputop_devinfo.n_eu_sub_slices = 4;
	    gputop_devinfo.slice_mask = 0x3;
	    gputop_devinfo.subslice_mask = 0xf;
	}
	gputop_devinfo.gen = 7;
    } else {
	i915_getparam_t gp;
	int ret;
	int n_eus = 0;
	int slice_mask = 0;
	int ss_mask = 0;
	int s_max;
	int ss_max;
	uint64_t subslice_mask = 0;
	int s;

	if (IS_BROADWELL(devid)) {
	    s_max = 2;
	    ss_max = 3;
	    gputop_devinfo.gen = 8;
	} else if (IS_CHERRYVIEW(devid)) {
	    s_max = 1;
	    ss_max = 2;
	    gputop_devinfo.gen = 8;
	} else if (IS_SKYLAKE(devid)) {
	    s_max = 3;
	    ss_max = 3;
	    gputop_devinfo.gen = 9;
	}

	gp.param = I915_PARAM_EU_TOTAL;
	gp.value = &n_eus;
	ret = perf_ioctl(drm_fd, I915_IOCTL_GETPARAM, &gp);
	assert(ret == 0 && n_eus > 0);

	gp.param = I915_PARAM_SLICE_MASK;
	gp.value = &slice_mask;
	ret = perf_ioctl(drm_fd, I915_IOCTL_GETPARAM, &gp);
	assert(ret == 0 && slice_mask);

	gp.param = I915_PARAM_SUBSLICE_MASK;
	gp.value = &ss_mask;
	ret = perf_ioctl(drm_fd, I915_IOCTL_GETPARAM, &gp);
	assert(ret == 0 && ss_mask);

	gputop_devinfo.n_eus = n_eus;
	gputop_devinfo.n_eu_slices = __builtin_popcount(slice_mask);
	gputop_devinfo.slice_mask = slice_mask;

	/* Note: some of the metrics we have (as described in XML)
	 * are conditional on a $SubsliceMask variable which is
	 * expected to also reflect the slice mask by packing
	 * together subslice masks for each slice in one value...
	 */
	for (s = 0; s < s_max; s++) {
	    if (slice_mask & (1<<s)) {
		subslice_mask |= ss_mask << (ss_max * s);
	    }
	}
	gputop_devinfo.subslice_mask = subslice_mask;
	gputop_devinfo.n_eu_sub_slices = __builtin_popcount(subslice_mask);
    }

    gputop_devinfo.eu_threads_count =
	gputop_devinfo.n_eus * threads_per_eu;
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

static bool
perf_stream_data_pending(struct gputop_perf_stream *stream)
{
    uint64_t head = read_perf_head(stream->perf.mmap_page);
    uint64_t tail = stream->perf.mmap_page->data_tail;

    return !!TAKEN(head, tail, stream->perf.buffer_size);
}

static bool
i915_perf_stream_data_pending(struct gputop_perf_stream *stream)
{
    struct pollfd pollfd = { stream->fd, POLLIN, 0 };
    int ret;
    if (gputop_fake_mode) {
        uint64_t elapsed_time = get_time() - stream->start_time;
        if (elapsed_time / stream->period - stream->gen_so_far > 0)
            return true;
        else
            return false;
    } else {
        while ((ret = poll(&pollfd, 1, 0)) < 0 && errno == EINTR)
	    ;

        if (ret == 1 && pollfd.revents & POLLIN)
	    return true;
        else
	    return false;
    }
}

bool
gputop_stream_data_pending(struct gputop_perf_stream *stream)
{
    switch (stream->type) {
    case GPUTOP_STREAM_PERF:
	return perf_stream_data_pending(stream);
    case GPUTOP_STREAM_I915_PERF:
	return i915_perf_stream_data_pending(stream);
    }

    assert(0);

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
    struct gputop_perf_header_buf *hdr_buf  = &stream->perf.header_buf;
    uint8_t *data = stream->perf.buffer;
    const uint64_t mask = stream->perf.buffer_size - 1;
    uint64_t perf_head;
    uint64_t perf_tail;
    uint32_t buf_head;
    uint32_t buf_tail;
    uint32_t n_new = 0;

    perf_head = read_perf_head(stream->perf.mmap_page);

    //if (hdr_buf->head == hdr_buf->tail)
	//perf_tail = hdr_buf->last_perf_head;
    //else
	perf_tail = stream->perf.mmap_page->data_tail;

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
    printf("> size = %lu\n", stream->perf.buffer_size);
    printf("> tail_ptr = %p\n", &stream->perf.mmap_page->data_tail);
    printf("> head=%"PRIu64"\n", perf_head);
    printf("> tail=%"PRIu64"\n", (uint64_t)stream->perf.mmap_page->data_tail);
    printf("> TAKEN=%"PRIu64"\n", (uint64_t)TAKEN(perf_head, stream->perf.mmap_page->data_tail, stream->perf.buffer_size));
    printf("> records:\n");
#endif

    while (TAKEN(perf_head, perf_tail, stream->perf.buffer_size)) {
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
	if ((const uint8_t *)header + header->size > data + stream->perf.buffer_size)
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
		    buf_tail_offset += stream->perf.buffer_size;

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
    write_perf_tail(stream->perf.mmap_page, perf_head);

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
	float percentage = (hdr_buf->offsets[(hdr_buf->head - 1) % hdr_buf->len] / (float)stream->perf.buffer_size) * 100.0f;
	printf("> %d%% full\n", (int)percentage);
    } else
	printf("> 100%% full\n");

    printf("> n records = %u\n", (hdr_buf->head - hdr_buf->tail));
#endif
}

void
gputop_i915_perf_print_records(struct gputop_perf_stream *stream,
			       uint8_t *buf,
			       int len)
{
    const struct i915_perf_record_header *header;
    int offset = 0;

    printf("records:\n");

    for (offset = 0; offset < len; offset += header->size) {
	header = (const struct i915_perf_record_header *)(buf + offset);

	if (header->size == 0) {
	    printf("Spurious header size == 0\n");
	    return;
	}
	printf("- header size = %d\n", header->size);

	switch (header->type) {

	case DRM_I915_PERF_RECORD_OA_BUFFER_OVERFLOW:
	    printf("- OA buffer overflow\n");
	    break;
	case DRM_I915_PERF_RECORD_OA_REPORT_LOST:
	    printf("- OA report lost\n");
	    break;
	case DRM_I915_PERF_RECORD_SAMPLE: {
	    printf("- Sample\n");
	    break;
	}

	default:
	    printf("- Spurious header type = %d\n", header->type);
	}
    }
}


static void
read_perf_samples(struct gputop_perf_stream *stream)
{
    dbg("FIXME: read core perf samples");
}


struct report_layout
{
    struct i915_perf_record_header header;
    uint32_t rep_id;
    uint32_t timest;
    uint32_t context_id;
    uint32_t clock_ticks;
    uint32_t counter_40_lsb[32];
    uint32_t agg_counter[4];
    uint8_t counter_40_msb[32];
    uint32_t bool_custom_counters[16];
};

// Function that generates fake Broadwell report metrics
int
fake_read(struct gputop_perf_stream *stream, uint8_t *buf, int buf_length)
{
    struct report_layout *report = (struct report_layout *)buf;
    struct i915_perf_record_header header;
    uint32_t timestamp, elapsed_clocks;
    int i;
    uint64_t counter;
    uint64_t elapsed_time = get_time() - stream->start_time;
    uint32_t records_to_gen;

    header.type = DRM_I915_PERF_RECORD_SAMPLE;
    header.pad = 0;
    header.size = sizeof(struct report_layout);

    // Calculate the minimum between records required (in relation to the time elapsed)
    // and the maximum number of records that can bit in the buffer.
    if (elapsed_time / stream->period - stream->gen_so_far < buf_length / header.size)
        records_to_gen = elapsed_time / stream->period - stream->gen_so_far;
    else
        records_to_gen = buf_length / header.size;

    for (i = 0; i < records_to_gen; i++) {
        int j;
        uint32_t counter_lsb;
        uint8_t counter_msb;

        // Header
        report->header = header;

        // Reason / Report ID
        report->rep_id = 1 << 19;

        // Timestamp
        timestamp = stream->period / 80 + stream->prev_timestamp;
        stream->prev_timestamp = timestamp;
        report->timest = timestamp;

        // GPU Clock Ticks
        elapsed_clocks = stream->period / 2 + stream->prev_clocks;
        stream->prev_clocks = elapsed_clocks;
        report->clock_ticks = elapsed_clocks;

        counter = elapsed_clocks * gputop_devinfo.n_eus;
        counter_msb = (counter >> 32) & 0xFF;
        counter_lsb = (uint32_t)counter;

        // Populate the 40 bit counters
        for (j = 0; j < 32; j++) {
            report->counter_40_lsb[j] = counter_lsb;
            report->counter_40_msb[j] = counter_msb;
        }

        // Populate the next 4 Counters
        for (j = 0; j < 4; j++)
            report->agg_counter[j] = counter_lsb;

        // Populate the final 16 boolean & custom counters
        counter = elapsed_clocks * 2;
        counter_lsb = (uint32_t)counter;
        for (j = 0; j < 16; j++)
            report->bool_custom_counters[j] = counter_lsb;

        stream->gen_so_far++;
        report++;
    }
    return header.size * records_to_gen;
}

static void
read_i915_perf_samples(struct gputop_perf_stream *stream)
{
    bool flip = false;

    do {
	int offset = 0;
	uint8_t *buf;
	int count;

	/* We double buffer reads so we can safely keep a pointer to
	 * our last sample for calculating deltas */
	if (flip) {
	    stream->oa.buf_idx = !stream->oa.buf_idx;
	    flip = false;
	}

	buf = stream->oa.bufs[stream->oa.buf_idx];
        if (gputop_fake_mode)
            count = fake_read(stream, buf, stream->oa.buf_sizes);
        else
	    count = read(stream->fd, buf, stream->oa.buf_sizes);

	if (count < 0) {
	    if (errno == EINTR)
		continue;
	    else if (errno == EAGAIN)
		break;
	    else {
		dbg("Error reading i915 OA event stream %m");
		break;
	    }
	}

	if (count == 0)
	    break;

	while (offset < count) {
	    const struct i915_perf_record_header *header =
		(const struct i915_perf_record_header *)(buf + offset);

	    if (header->size == 0) {
		dbg("Spurious header size == 0\n");
		/* XXX: How should we handle this instead of exiting() */
		exit(1);
	    }

	    offset += header->size;

	    switch (header->type) {

	    case DRM_I915_PERF_RECORD_OA_BUFFER_OVERFLOW:
		dbg("i915 perf: OA buffer overflow\n");
		break;
	    case DRM_I915_PERF_RECORD_OA_REPORT_LOST:
		dbg("i915 perf: OA report lost\n");
		break;

	    case DRM_I915_PERF_RECORD_SAMPLE: {
		struct oa_sample *sample = (struct oa_sample *)header;
		uint8_t *report = sample->oa_report;
		uint32_t reason = (((uint32_t*)report)[0] >> OAREPORT_REASON_SHIFT) &
			OAREPORT_REASON_MASK;

		/* On GEN8+ the hardware is able to generate reports with a
		 * reason field. So for example when capturing perdiodic counter
		 * snapshots the reason field would be TIMER. This is _not_ the
		 * case when capturing snapshots by inserting MI_RPC commands
		 * into the CS, instead this field is overridden with the u32
		 * id passed with the MI_RPC. But within GPUTOP we should
		 * currently only expect either CTX_SWITCH or TIMER.
		 */
		if (gputop_devinfo.gen >= 8) {
			if (!(reason & (OAREPORT_REASON_CTX_SWITCH |
					OAREPORT_REASON_TIMER))) {
			    dbg("i915 perf: Unexpected OA sample reason value %"
				PRIu32 "\n", reason);
			}
		}

		if (stream->oa.last) {
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
		    if (stream->query->per_ctx_mode && gputop_devinfo.gen >= 8) {
			if (!(reason & OAREPORT_REASON_CTX_SWITCH))
			    current_user->sample(stream, stream->oa.last, report);
		    } else {
			current_user->sample(stream, stream->oa.last, report);
		    }
		}

		stream->oa.last = report;

		/* Make sure the next read won't clobber stream->last by
		 * switching buffers next time we read */
		flip = true;
		break;
	    }

	    default:
		dbg("i915 perf: Spurious header type = %d\n", header->type);
	    }
	}
    } while(1);
}

void
gputop_perf_read_samples(struct gputop_perf_stream *stream)
{
    switch (stream->type) {
    case GPUTOP_STREAM_PERF:
	read_perf_samples(stream);
	return;
    case GPUTOP_STREAM_I915_PERF:
	read_i915_perf_samples(stream);
	return;
    }

    assert(0);
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
gputop_enumerate_queries_via_sysfs (void)
{
    DIR *drm_dir, *metrics_dir;
    struct dirent *entry1, *entry2, *entry3, *entry4;
    struct stat sb;
    int mjr, mnr;
    char buffer[128];
    int name_max;
    int entry_size;

    if (fstat(drm_fd, &sb)) {
        gputop_log(GPUTOP_LOG_LEVEL_HIGH, "Failed to stat DRM fd\n", -1);
        return false;
    }

    mjr = major(sb.st_rdev);
    mnr = minor(sb.st_rdev);

    snprintf(buffer, sizeof(buffer), "/sys/dev/char/%d:%d/device/drm", mjr,
        mnr);

    drm_dir = opendir(buffer);

    if (drm_dir == NULL)
        assert(0);

    name_max = pathconf(buffer, _PC_NAME_MAX);

    if (name_max == -1)
        name_max = 255;

    entry_size = offsetof(struct dirent, d_name) + name_max + 1;
    entry1 = alloca(entry_size);
    entry3 = alloca(entry_size);

    while ((readdir_r(drm_dir, entry1, &entry2) == 0) && entry2 != NULL) {
        if (entry2->d_type == DT_DIR &&
            strncmp(entry2->d_name, "card", 4) == 0)
        {
            snprintf(buffer, sizeof(buffer),
                "/sys/dev/char/%d:%d/device/drm/%s/metrics", mjr,
                mnr, entry2->d_name);

            metrics_dir = opendir(buffer);

            if (metrics_dir == NULL) {
                closedir(drm_dir);
                return false;
            }

            while ((readdir_r(metrics_dir, entry3, &entry4) == 0) &&
                   entry4 != NULL)
            {
                struct gputop_perf_query *query;
                struct gputop_hash_entry *queries_entry;

                if (entry4->d_type != DT_DIR || entry4->d_name[0] == '.')
                    continue;

                queries_entry =
                    gputop_hash_table_search(queries, entry4->d_name);

                if (queries_entry == NULL)
                    continue;

                query = (struct gputop_perf_query*)queries_entry->data;

                snprintf(buffer, sizeof(buffer),
                    "/sys/dev/char/%d:%d/device/drm/%s/metrics/%s/id",
                        mjr, mnr, entry2->d_name, entry4->d_name);

                query->perf_oa_metrics_set = read_file_uint64(buffer);
                array_append(perf_oa_supported_query_guids, &query->guid);
            }
            closedir (metrics_dir);
        }
    }

    closedir(drm_dir);

    return true;
}

bool
gputop_perf_initialize(void)
{
    if (gputop_devinfo.n_eus)
	return true;
    if (getenv("GPUTOP_FAKE_MODE") && strcmp(getenv("GPUTOP_FAKE_MODE"), "1") == 0) {
        gputop_fake_mode = true;
        intel_dev.device = 0;
    }
    else {
	drm_fd = open_render_node(&intel_dev);
	if (drm_fd < 0) {
	    gputop_log(GPUTOP_LOG_LEVEL_HIGH, "Failed to open render node", -1);
	    return false;
	}
    }

    /* NB: eu_count needs to be initialized before declaring counters */
    init_dev_info(drm_fd, intel_dev.device);
    page_size = sysconf(_SC_PAGE_SIZE);

    queries = gputop_hash_table_create(NULL, gputop_key_hash_string,
                                       gputop_key_string_equal);
    perf_oa_supported_query_guids = array_new(sizeof(char*), 1);

    if (gputop_fake_mode) {
	gputop_oa_add_queries_bdw(&gputop_devinfo);
    } else if (IS_HASWELL(intel_dev.device)) {
	gputop_oa_add_queries_hsw(&gputop_devinfo);
    } else if (IS_BROADWELL(intel_dev.device)) {
	gputop_oa_add_queries_bdw(&gputop_devinfo);
    } else if (IS_CHERRYVIEW(intel_dev.device)) {
	gputop_oa_add_queries_chv(&gputop_devinfo);
    } else if (IS_SKYLAKE(intel_dev.device)) {
	gputop_oa_add_queries_skl(&gputop_devinfo);
    } else
	assert(0);

    return gputop_enumerate_queries_via_sysfs();
}

static void
free_perf_oa_queries(struct gputop_hash_entry *entry)
{
    free(entry->data);
}

void
gputop_perf_free(void)
{
    gputop_hash_table_destroy(queries, free_perf_oa_queries);
    array_free(perf_oa_supported_query_guids);
}

/**
 * Given pointers to starting and ending OA snapshots, calculate the deltas for each
 * counter to update the results.
 */
static void
overview_sample_cb(struct gputop_perf_stream *stream, uint8_t *start, uint8_t *end)
{
    gputop_oa_accumulate_reports(stream->query, start, end);
}

void
gputop_perf_accumulator_clear(struct gputop_perf_stream *stream)
{
    struct gputop_perf_query *query = stream->query;

    memset(query->accumulator, 0, sizeof(query->accumulator));

    stream->oa.last = NULL;
}

static struct perf_oa_user overview_user = {
    .sample = overview_sample_cb,
};

bool
gputop_i915_perf_oa_overview_open(struct gputop_perf_query *query,
                                  bool enable_per_ctx)
{
    int period_exponent;
    char *error = NULL;

    assert(gputop_current_perf_query == NULL);

    if (!gputop_perf_initialize())
	return false;

    current_user = &overview_user;
    gputop_current_perf_query = query;

    gputop_current_perf_query->per_ctx_mode = enable_per_ctx;

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
	gputop_open_i915_perf_oa_query(gputop_current_perf_query,
				       period_exponent,
				       32 * page_size,
				       gputop_perf_read_samples,
				       false,
				       &error);

    if (!gputop_current_perf_stream) {
	gputop_log(GPUTOP_LOG_LEVEL_HIGH, error, -1);
	free(error);

	gputop_current_perf_query = NULL;
	return false;
    }

    gputop_perf_accumulator_clear(gputop_current_perf_stream);

    return true;
}

void
gputop_i915_perf_oa_overview_close(void)
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
trace_sample_cb(struct gputop_perf_stream *stream, uint8_t *start, uint8_t *end)
{
    struct gputop_perf_query *query = stream->query;
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
gputop_i915_perf_oa_trace_open(struct gputop_perf_query *query,
                               bool enable_per_ctx)
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
    gputop_current_perf_query = query;

    gputop_current_perf_query->per_ctx_mode = enable_per_ctx;

    /* The timestamp for HSW+ increments every 80ns
     *
     * The period_exponent gives a sampling period as follows:
     *   sample_period = 80ns * 2^(period_exponent + 1)
     *
     * Sample ~ every 1 millisecond...
     */
    period_exponent = 11;

    gputop_current_perf_stream =
	gputop_open_i915_perf_oa_query(gputop_current_perf_query,
				       period_exponent,
				       32 * page_size,
				       gputop_perf_read_samples,
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
gputop_i915_perf_oa_trace_close(void)
{
    if (!gputop_current_perf_query)
	return;

    gputop_perf_stream_unref(gputop_current_perf_stream);

    free(gputop_perf_trace_buffer);
    gputop_current_perf_query = NULL;
    gputop_current_perf_stream = NULL;
}
