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

#include <config.h>

#include <linux/perf_event.h>

#include <i915_drm.h>

#include <asm/unistd.h>
#include <sys/sysmacros.h>
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

#include "dev/gen_device_info.h"

#include "gputop-util.h"
#include "gputop-sysutil.h"
#include "gputop-mainloop.h"
#include "gputop-log.h"
#include "gputop-perf.h"
#include "gputop-oa-metrics.h"
#include "gputop-cpu.h"

#include "gputop-gens-metrics.h"

#include "util/bitscan.h"
#include "util/macros.h"
#include "util/ralloc.h"

/* Samples read() from i915 perf */
struct oa_sample {
    struct drm_i915_perf_record_header header;
    uint8_t oa_report[];
};

#define MAX_I915_PERF_OA_SAMPLE_SIZE (8 +   /* drm_i915_perf_record_header */ \
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

bool gputop_fake_mode = false;
static bool gputop_disable_oaconfig = false;

static struct intel_device intel_dev;

static unsigned int page_size;

struct gputop_gen *gen_metrics;
struct array *gputop_perf_oa_supported_metric_set_uuids;
static struct perf_oa_user *gputop_perf_current_user;
static struct gputop_devinfo gputop_devinfo;

static int drm_fd = -1;
static int drm_card = -1;

static struct list_head ctx_handles_list;

/******************************************************************************/

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

static bool
sysfs_card_read(const char *file, uint64_t *value)
{
    char buf[512];

    snprintf(buf, sizeof(buf), "/sys/class/drm/card%d/%s", drm_card, file);

    return gputop_read_file_uint64(buf, value);
}

static bool
kernel_has_dynamic_config_support(int fd)
{
    if (gputop_disable_oaconfig)
	return false;

    list_for_each_entry(struct gputop_metric_set, metric_set,
                        &gen_metrics->metric_sets, link) {
	struct drm_i915_perf_oa_config config;
	char config_path[256];
	uint32_t mux_regs[] = { 0x9888 /* NOA_WRITE */, 0x0 };
	uint64_t config_id;

	snprintf(config_path, sizeof(config_path), "metrics/%s/id",
		 metric_set->hw_config_guid);

	if (sysfs_card_read(config_path, &config_id) && config_id != 1)
	    continue;

	memset(&config, 0, sizeof(config));

	memcpy(config.uuid, metric_set->hw_config_guid, sizeof(config.uuid));

	config.n_mux_regs = 1;
	config.mux_regs_ptr = (uintptr_t) mux_regs;

	if (ioctl(fd, DRM_IOCTL_I915_PERF_REMOVE_CONFIG, &config_id) < 0 &&
	    errno == ENOENT)
	    return true;
    }

    return false;
}

static const struct gputop_metric_set *
get_test_metric_set(void)
{
    list_for_each_entry(struct gputop_metric_set, metric_set,
                        &gen_metrics->metric_sets, link) {
        if (!strcmp(metric_set->symbol_name, "TestOa"))
            return metric_set;
    }

    return NULL;
}

static bool
kernel_supports_open_property(uint64_t prop, uint64_t value)
{
        const struct gputop_metric_set *metric_set = get_test_metric_set();
    struct drm_i915_perf_open_param param;
    uint64_t properties[DRM_I915_PERF_PROP_MAX * 2];
    int p = 0, stream_fd;

    if (gputop_fake_mode || !metric_set)
        return false;

    memset(&param, 0, sizeof(param));

    param.flags = 0;
    param.flags |= I915_PERF_FLAG_FD_CLOEXEC;
    param.flags |= I915_PERF_FLAG_FD_NONBLOCK;

    properties[p++] = DRM_I915_PERF_PROP_SAMPLE_OA;
    properties[p++] = true;

    properties[p++] = DRM_I915_PERF_PROP_OA_METRICS_SET;
    properties[p++] = metric_set->perf_oa_metrics_set;

    properties[p++] = DRM_I915_PERF_PROP_OA_FORMAT;
    properties[p++] = metric_set->perf_oa_format;

    properties[p++] = DRM_I915_PERF_PROP_OA_EXPONENT;
    properties[p++] = 5;

    properties[p++] = DRM_I915_PERF_PROP_CTX_HANDLE;
    properties[p++] = 999; /* invalid on purpose */

    properties[p++] = prop;
    properties[p++] = value;

    param.properties_ptr = (uintptr_t)properties;
    param.num_properties = p / 2;

    stream_fd = perf_ioctl(drm_fd, DRM_IOCTL_I915_PERF_OPEN, &param);
    assert(stream_fd == -1);

    return errno == ENOENT;

}

bool
gputop_perf_kernel_has_i915_oa_cpu_timestamps(void)
{
    return kernel_supports_open_property(DRM_I915_PERF_PROP_SAMPLE_SYSTEM_TS,
                                         true);
}

bool
gputop_perf_kernel_has_i915_oa_gpu_timestamps(void)
{
    return kernel_supports_open_property(DRM_I915_PERF_PROP_SAMPLE_GPU_TS,
                                         true);
}

bool gputop_add_ctx_handle(int ctx_fd, uint32_t ctx_id)
{
    struct ctx_handle *handle = xmalloc0(sizeof(*handle));
    if (!handle) {
	return false;
    }
    handle->id = ctx_id;
    handle->fd = ctx_fd;

    list_addtail(&handle->link, &ctx_handles_list);

    return true;
}

bool gputop_remove_ctx_handle(uint32_t ctx_id)
{
    list_for_each_entry(struct ctx_handle, ctx, &ctx_handles_list, link) {
	if (ctx->id == ctx_id) {
	    list_del(&ctx->link);
	    free(ctx);
	    return true;
	}
    }
    return false;
}

struct ctx_handle *get_first_available_ctx(char **error)
{
    struct ctx_handle *ctx = NULL;

    ctx = list_first_entry(&ctx_handles_list, struct ctx_handle, link);
    if (!ctx) {
	int ret = asprintf(error, "Error unable to find a context\n");
	(void) ret;
    }

    return ctx;
}

struct ctx_handle *lookup_ctx_handle(uint32_t ctx_id)
{
    list_for_each_entry(struct ctx_handle, ctx, &ctx_handles_list, link) {
	if (ctx->id == ctx_id)
            return ctx;
    }
    return NULL;
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

/* Stream closing is split up to allow for the closure of
 * uv poll or timer handles to happen via the mainloop,
 * via uv_close() before we finish up here... */
static void
finish_stream_close(struct gputop_perf_stream *stream)
{
    switch(stream->type) {
    case GPUTOP_STREAM_PERF:
	if (stream->fd > 0) {

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

	    server_dbg("closed perf stream\n");
	}

	break;
    case GPUTOP_STREAM_I915_PERF:
	for (int i = 0; i < ARRAY_SIZE(stream->oa.bufs); i++) {
	    if (stream->oa.bufs[i]) {
		free(stream->oa.bufs[i]);
		stream->oa.bufs[i] = NULL;
	    }
	}
	if (stream->fd == -1)
	    server_dbg("closed i915 fake perf stream\n");
	else if (stream->fd > 0) {
	    close(stream->fd);
	    stream->fd = -1;
	    server_dbg("closed i915 perf stream\n");
	}

	break;
    case GPUTOP_STREAM_CPU:
	free(stream->cpu.stats_buf);
	stream->cpu.stats_buf = NULL;
	server_dbg("closed cpu stats stream\n");
	break;
    }

    stream->closed = true;
    stream->on_close_cb(stream);
}

static void
stream_handle_closed_cb(uv_handle_t *handle)
{
    struct gputop_perf_stream *stream = handle->data;

    if (--(stream->n_closing_uv_handles) == 0)
	finish_stream_close(stream);
}

void
gputop_perf_stream_close(struct gputop_perf_stream *stream,
			 void (*on_close_cb)(struct gputop_perf_stream *stream))
{
    stream->on_close_cb = on_close_cb;

    /* First we close any libuv handles before closing anything else in
     * stream_handle_closed_cb()...
     */
    switch(stream->type) {
    case GPUTOP_STREAM_PERF:
	if (stream->fd >= 0) {
	    uv_close((uv_handle_t *)&stream->fd_poll, stream_handle_closed_cb);
	    stream->n_closing_uv_handles++;
	}
	break;
    case GPUTOP_STREAM_I915_PERF:
	if (stream->fd == -1) {
	    uv_close((uv_handle_t *)&stream->fd_timer, stream_handle_closed_cb);
	    stream->n_closing_uv_handles++;

	}
	if (stream->fd >= 0) {
	    uv_close((uv_handle_t *)&stream->fd_poll, stream_handle_closed_cb);
	    stream->n_closing_uv_handles++;
	}
	break;
    case GPUTOP_STREAM_CPU:
        uv_close((uv_handle_t *)&stream->cpu.sample_timer, stream_handle_closed_cb);
        stream->n_closing_uv_handles++;
	break;
    }

    if (!stream->n_closing_uv_handles)
	finish_stream_close(stream);
}

void
gputop_perf_stream_unref(struct gputop_perf_stream *stream)
{
    if (--(stream->ref_count) == 0) {
	/* gputop_perf_stream_close() must have been called before the
	 * last reference is dropped... */
	assert(stream->closed);

	if (stream->user.destroy_cb)
	    stream->user.destroy_cb(stream);
        if (stream->user.data)
            free(stream->user.data);
        stream->user.data = NULL;

	free(stream);
	server_dbg("freed gputop-perf stream\n");
    }
}

struct gputop_perf_stream *
gputop_open_i915_perf_oa_stream(struct gputop_metric_set *metric_set,
				int period_exponent,
				struct ctx_handle *ctx,
                                bool cpu_timestamps,
                                bool gpu_timestamps,
                                void (*ready_cb)(struct gputop_perf_stream *),
				bool overwrite,
				char **error)
{
    struct gputop_perf_stream *stream;
    struct drm_i915_perf_open_param param;
    int stream_fd = -1;
    int oa_stream_fd = drm_fd;

    if (!gputop_fake_mode) {
	uint64_t properties[DRM_I915_PERF_PROP_MAX * 2];
	int p = 0;

	memset(&param, 0, sizeof(param));

	param.flags = 0;
	param.flags |= I915_PERF_FLAG_FD_CLOEXEC;
	param.flags |= I915_PERF_FLAG_FD_NONBLOCK;

	properties[p++] = DRM_I915_PERF_PROP_SAMPLE_OA;
	properties[p++] = true;

	properties[p++] = DRM_I915_PERF_PROP_OA_METRICS_SET;
	properties[p++] = metric_set->perf_oa_metrics_set;

	properties[p++] = DRM_I915_PERF_PROP_OA_FORMAT;
	properties[p++] = metric_set->perf_oa_format;

	properties[p++] = DRM_I915_PERF_PROP_OA_EXPONENT;
	properties[p++] = period_exponent;

	if (ctx) {
	    properties[p++] = DRM_I915_PERF_PROP_CTX_HANDLE;
	    properties[p++] = ctx->id;

	    // N.B The file descriptor that was used to create the context,
	    // _must_ be same as the one we use to open the per-context stream.
	    // Since in the kernel we lookup the intel_context based on the ctx
	    // id and the fd that was used to open the stream, so if there is a
	    // mismatch between the file descriptors for the stream and the
	    // context creation then the kernel will simply fail with the
	    // lookup.
	    oa_stream_fd = ctx->fd;
	    dbg("opening per context i915 perf stream: fd = %d, ctx=%u\n",
		ctx->fd, ctx->id);
	}

        if (cpu_timestamps) {
            properties[p++] = DRM_I915_PERF_PROP_SAMPLE_SYSTEM_TS;
            properties[p++] = true;
        }

        if (gpu_timestamps) {
            properties[p++] = DRM_I915_PERF_PROP_SAMPLE_GPU_TS;
            properties[p++] = true;
        }

	param.properties_ptr = (uintptr_t)properties;
	param.num_properties = p / 2;

	stream_fd = perf_ioctl(oa_stream_fd, DRM_IOCTL_I915_PERF_OPEN, &param);
	if (stream_fd == -1) {
	    int ret = asprintf(error, "Error opening i915 perf OA event: %m\n");
	    (void) ret;
	    return NULL;
	}
    }

    stream = xmalloc0(sizeof(*stream));
    stream->type = GPUTOP_STREAM_I915_PERF;
    stream->ref_count = 1;
    stream->metric_set = metric_set;
    stream->ready_cb = ready_cb;
    stream->per_ctx_mode = ctx != NULL;

    stream->fd = stream_fd;

    if (gputop_fake_mode) {
	stream->start_time = gputop_get_time();
	stream->prev_clocks = gputop_get_time();
	stream->period = 80 * (2 << period_exponent);
	stream->prev_timestamp = gputop_get_time();
    }

    /* We double buffer the samples we read from the kernel so
     * we can maintain a stream->last pointer for calculating
     * counter deltas */
    stream->oa.buf_sizes = (MAX_I915_PERF_OA_SAMPLE_SIZE +
                            (cpu_timestamps ? 8 : 0) +
                            (gpu_timestamps ? 8 : 0)) * 100;
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
	uv_timer_init(gputop_mainloop, &stream->fd_timer);
	uv_timer_start(&stream->fd_timer, perf_fake_ready_cb, 1000, 1000);
    }
    else
    {
	uv_poll_init(gputop_mainloop, &stream->fd_poll, stream->fd);
	uv_poll_start(&stream->fd_poll, UV_READABLE, perf_ready_cb);
    }


    return stream;
}

struct gputop_perf_stream *
gputop_perf_open_tracepoint(int pid,
			    int cpu,
			    uint64_t id,
			    size_t trace_struct_size,
			    size_t perf_buffer_size,
			    void (*ready_cb)(struct gputop_perf_stream *),
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
    attr.type = PERF_TYPE_TRACEPOINT;
    attr.config = id;

    attr.sample_type = PERF_SAMPLE_RAW | PERF_SAMPLE_TIME;
    attr.sample_period = 1;
    //attr.wakeup_events = 1;
    attr.watermark = true;
    attr.wakeup_watermark = perf_buffer_size / 4;
    attr.clockid = CLOCK_MONOTONIC;
    attr.use_clockid = true;

    event_fd = perf_event_open(&attr,
			       pid,
			       cpu,
			       -1, /* group fd */
			       PERF_FLAG_FD_CLOEXEC); /* flags */
    if (event_fd == -1) {
	int ret = asprintf(error, "Error opening perf tracepoint event: %m\n");
	(void) ret;
	return NULL;
    }

    /* NB: A read-write mapping ensures the kernel will stop writing data when
     * the buffer is full, and will report samples as lost. */
    mmap_base = mmap(NULL,
		     perf_buffer_size + page_size,
		     PROT_READ | PROT_WRITE, MAP_SHARED, event_fd, 0);
    if (mmap_base == MAP_FAILED) {
	int ret = asprintf(error, "Error mapping circular buffer, %m\n");
	(void) ret;
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
    stream->ready_cb = ready_cb;

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
    uv_poll_init(gputop_mainloop, &stream->fd_poll, stream->fd);
    uv_poll_start(&stream->fd_poll, UV_READABLE, perf_ready_cb);

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
	int ret = asprintf(error, "Error opening perf event: %m\n");
	(void) ret;
	return NULL;
    }

    /* NB: A read-write mapping ensures the kernel will stop writing data when
     * the buffer is full, and will report samples as lost. */
    mmap_base = mmap(NULL,
		     perf_buffer_size + page_size,
		     PROT_READ | PROT_WRITE, MAP_SHARED, event_fd, 0);
    if (mmap_base == MAP_FAILED) {
	int ret = asprintf(error, "Error mapping circular buffer, %m\n");
	(void) ret;
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
    uv_poll_init(gputop_mainloop, &stream->fd_poll, stream->fd);
    uv_poll_start(&stream->fd_poll, UV_READABLE, ready_cb);

    return stream;
}

static void
log_cpu_stats_cb(uv_timer_t *timer)
{
    struct gputop_perf_stream *stream = timer->data;

    if (stream->cpu.stats_buf_pos < stream->cpu.stats_buf_len) {
	struct cpu_stat *stats = stream->cpu.stats_buf + stream->cpu.stats_buf_pos;
	int n_cpus = gputop_cpu_count();

	gputop_cpu_read_stats(stats, n_cpus);
	stream->cpu.stats_buf_pos += n_cpus;
    }

    if (stream->cpu.stats_buf_pos >= stream->cpu.stats_buf_len) {
	stream->cpu.stats_buf_full = true;
	if (stream->overwrite)
	    stream->cpu.stats_buf_pos = 0;
    }
}

struct gputop_perf_stream *
gputop_perf_open_cpu_stats(bool overwrite, uint64_t sample_period_ms)
{
    struct gputop_perf_stream *stream;
    int n_cpus = gputop_cpu_count();

    stream = xmalloc0(sizeof(*stream));
    stream->type = GPUTOP_STREAM_CPU;
    stream->ref_count = 1;

    stream->cpu.stats_buf_len = MAX(10, 1000 / sample_period_ms);
    stream->cpu.stats_buf = xmalloc(stream->cpu.stats_buf_len *
				    sizeof(struct cpu_stat) * n_cpus);
    stream->cpu.stats_buf_pos = 0;

    stream->overwrite = overwrite;

    stream->cpu.sample_timer.data = stream;
    uv_timer_init(gputop_mainloop, &stream->cpu.sample_timer);

    uv_timer_start(&stream->cpu.sample_timer,
		   log_cpu_stats_cb,
		   sample_period_ms,
		   sample_period_ms);

    return stream;
}

static void
devinfo_build_topology(const struct gen_device_info *devinfo,
		       struct gputop_devtopology *topology)
{
    int s, ss, eug;
    int slice_stride, subslice_stride;

    topology->max_slices = devinfo->num_slices;
    topology->max_subslices = devinfo->num_subslices[0];
    if (devinfo->is_haswell)
	topology->max_eus_per_subslice = 10;
    else
	topology->max_eus_per_subslice = 8; // TODO.

    subslice_stride = DIV_ROUND_UP(topology->max_eus_per_subslice, 8);
    slice_stride = subslice_stride * topology->max_subslices;

    for (s = 0; s < devinfo->num_slices; s++) {
	topology->slices_mask[0] |= 1U << s;

	for (ss = 0; ss < devinfo->num_subslices[s]; ss++) {
	    /* Assuming we have never more than 8 subslices. */
	    topology->subslices_mask[s] |= 1U << ss;

	    for (eug = 0; eug < subslice_stride; eug++) {
		topology->eus_mask[s * slice_stride + ss * subslice_stride + eug] =
		    (((1UL << topology->max_eus_per_subslice) - 1) >> (eug * 8)) & 0xff;
	    }
	}
    }
}

static bool
fill_topology_from_masks(struct gputop_devtopology *topology,
                         uint32_t s_mask, uint32_t ss_mask,
                         uint32_t n_eus)
{
    int n_slices = 0, n_subslices = 0;
    int s, ss, eug;

    memset(topology->slices_mask, 0, sizeof(topology->slices_mask));
    topology->slices_mask[0] = s_mask;
    topology->max_slices = util_last_bit(s_mask);
    n_slices = __builtin_popcount(s_mask);

    memset(topology->subslices_mask, 0, sizeof(topology->subslices_mask));
    for (s = 0; s < util_last_bit(s_mask); s++) {
	int subslice_stride = DIV_ROUND_UP(ss_mask, 8);

	for (ss = 0; ss < subslice_stride; ss++) {
	    topology->subslices_mask[s * subslice_stride + ss] =
		(ss_mask >> (ss * 8)) & 0xff;
	}
    }
    topology->max_subslices = util_last_bit(ss_mask);
    n_subslices = __builtin_popcount(ss_mask);

    int subslice_stride =
	DIV_ROUND_UP(n_eus / (topology->max_slices * topology->max_subslices), 8);
    int slice_stride = subslice_stride * topology->max_subslices;
    int n_eus_per_subslice = n_eus / (n_slices * n_subslices);

    int subslice_slice_stride = DIV_ROUND_UP(topology->max_subslices, 8);

    topology->max_eus_per_subslice = 8 * DIV_ROUND_UP(n_eus_per_subslice, 8);

    memset(topology->eus_mask, 0, sizeof(topology->eus_mask));
    for (s = 0; s < topology->max_slices; s++) {
	for (ss = 0; ss < topology->max_subslices; ss++) {
	    if (topology->subslices_mask[s * subslice_slice_stride + ss / 8] & (1UL << (ss % 8))) {
		for (eug = 0; eug < subslice_stride; eug++) {
		    topology->eus_mask[s * slice_stride + ss * subslice_stride + eug] =
			(((1UL << n_eus_per_subslice) - 1) >> (eug * 8)) & 0xff;
		}
	    }
	}
    }

    return true;
}

static bool
gputop_override_topology(struct gputop_devtopology *topology)
{
    long s_mask = 0, ss_mask = 0, n_eus = 0;

    const char *s_s_mask = getenv("GPUTOP_TOPOLOGY_OVERRIDE");
    if (!s_s_mask)
        return false;
    s_mask = strtol(s_s_mask, NULL, 0);

    const char *s_ss_mask = strstr(s_s_mask, ",");
    if (!s_ss_mask)
        goto invalid;
    s_ss_mask++;
    ss_mask = strtol(s_ss_mask, NULL, 0);

    const char *s_n_eus = strstr(s_ss_mask, ",");
    if (!s_n_eus)
        goto invalid;
    s_n_eus++;
    n_eus = strtol(s_n_eus, NULL, 0);

    if (s_mask == 0 || ss_mask == 0 || n_eus == 0)
        goto invalid;

    fprintf(stderr, "Using topology override: slice_mask=%li subslice=%li n_eus=%li\n",
            s_mask, ss_mask, n_eus);
    fill_topology_from_masks(topology, s_mask, ss_mask, n_eus);

    return true;

 invalid:
    fprintf(stderr, "Invalid topology override: slice_mask=%li subslice=%li n_eus=%li\n",
            s_mask, ss_mask, n_eus);
    return false;
}

static bool
i915_query_old_slice_masks(int fd, struct gputop_devtopology *topology)
{
    drm_i915_getparam_t gp;
    int s_mask = 0, ss_mask = 0, n_eus = 0;

    gp.param = I915_PARAM_SLICE_MASK;
    gp.value = &s_mask;
    if (perf_ioctl(fd, DRM_IOCTL_I915_GETPARAM, &gp))
	return false;

    gp.param = I915_PARAM_SUBSLICE_MASK;
    gp.value = &ss_mask;
    if (perf_ioctl(fd, DRM_IOCTL_I915_GETPARAM, &gp))
	return false;

    gp.param = I915_PARAM_EU_TOTAL;
    gp.value = &n_eus;
    if (perf_ioctl(fd, DRM_IOCTL_I915_GETPARAM, &gp))
	return false;

    fill_topology_from_masks(topology, s_mask, ss_mask, n_eus);

    return true;
}

struct gputop_query_topology_info {
    struct drm_i915_query_topology_info base;
    char data[512];
};

static void
i915_query_topology(int fd, struct gputop_devtopology *topology)
{
    struct drm_i915_query query = {};
    struct gputop_query_topology_info topo_info = {};
    struct drm_i915_query_item item = {
	.query_id = DRM_I915_QUERY_TOPOLOGY_INFO,
        .length = sizeof(topo_info),
        .data_ptr = (uintptr_t) &topo_info,
    };
    int ret;

    query.num_items = 1;
    query.items_ptr = (uintptr_t) &item;

    ret = perf_ioctl(fd, DRM_IOCTL_I915_QUERY, &query);
    assert(ret == 0);
    assert(item.length > 0);

    topology->max_slices = topo_info.base.max_slices;
    topology->max_subslices = topo_info.base.max_subslices;
    topology->max_eus_per_subslice = topo_info.base.max_eus_per_subslice;

    assert(sizeof(topology->slices_mask) >= topo_info.base.subslice_offset);
    memcpy(topology->slices_mask, topo_info.data, topo_info.base.subslice_offset);

    assert(sizeof(topology->subslices_mask) >= (topo_info.base.eu_offset -
                                                topo_info.base.subslice_offset));
    memcpy(topology->subslices_mask, &topo_info.data[topo_info.base.subslice_offset],
           topo_info.base.eu_offset - topo_info.base.subslice_offset);

    assert(sizeof(topology->eus_mask) >= (item.length - topo_info.base.eu_offset));
    memcpy(topology->eus_mask, &topo_info.data[topo_info.base.eu_offset],
           item.length - topo_info.base.eu_offset);
}

static bool
i915_has_query_info(int fd)
{
    struct drm_i915_query_item item = { .query_id = DRM_I915_QUERY_TOPOLOGY_INFO, };
    struct drm_i915_query query = { .num_items = 1, .items_ptr = (uintptr_t) &item };

    return perf_ioctl(fd, DRM_IOCTL_I915_QUERY, &query) == 0 && item.length > 0;
}


static void
i915_query_engines(int fd, struct gputop_devtopology *topology)
{
    DIR *engines_dir;
    struct dirent *entry;
    char path[512];

    assert(ARRAY_SIZE(topology->engines) >= I915_ENGINE_CLASS_VIDEO_ENHANCE + 1);

    snprintf(path, sizeof(path), "/sys/class/drm/card%d/engines", drm_card);
    engines_dir = opendir(path);
    if (!engines_dir)
        return;

    while ((entry = readdir(engines_dir))) {
	if (entry->d_type != DT_DIR)
            continue;

        uint64_t engine_class = 0;
        snprintf(path, sizeof(path), "engines/%s/class", entry->d_name);
        if (!sysfs_card_read(path, &engine_class))
            continue;
        if (engine_class >= ARRAY_SIZE(topology->engines))
            continue;

        topology->engines[engine_class]++;
    }

    closedir(engines_dir);
}

static bool
init_dev_info(int fd, uint32_t devid, const struct gen_device_info *devinfo)
{
    struct gputop_devtopology *topology = &gputop_devinfo.topology;

    memset(&gputop_devinfo, 0, sizeof(gputop_devinfo));
    gputop_devinfo.devid = devid;

#define SET_NAMES(g, _devname, _prettyname) do {                        \
	strncpy(g.devname, _devname, sizeof(g.devname));                \
	strncpy(g.prettyname, _prettyname, sizeof(g.prettyname));       \
    } while (0)

    gputop_devinfo.gen = devinfo->gen;
    gputop_devinfo.timestamp_frequency = devinfo->timestamp_frequency;
    topology->n_threads_per_eu = devinfo->num_thread_per_eu;

    if (gputop_fake_mode) {
        fill_topology_from_masks(topology, 0x1, 0x1, 8);
        gputop_devinfo.gt_min_freq = 500;
        gputop_devinfo.gt_max_freq = 1100;
    } else {
        drm_i915_getparam_t gp;
	int revision, timestamp_frequency;

	gp.param = I915_PARAM_REVISION;
	gp.value = &revision;
	perf_ioctl(fd, DRM_IOCTL_I915_GETPARAM, &gp);
	gputop_devinfo.revision = revision;

	/* This might not be available on all kernels, save the value
	 * only if the ioctl succeeds.
	 */
	gp.param = I915_PARAM_CS_TIMESTAMP_FREQUENCY;
	gp.value = &timestamp_frequency;
	if (perf_ioctl(fd, DRM_IOCTL_I915_GETPARAM, &gp) == 0)
	    gputop_devinfo.timestamp_frequency = timestamp_frequency;

        if (!gputop_override_topology(topology)) {
            if (i915_has_query_info(fd)) {
                i915_query_topology(fd, topology);
                i915_query_engines(fd, topology);
            } else if (!i915_query_old_slice_masks(fd, topology))
                devinfo_build_topology(devinfo, topology);
        }

	assert(drm_card >= 0);
	if (!sysfs_card_read("gt_min_freq_mhz", &gputop_devinfo.gt_min_freq))
	    fprintf(stderr, "Unable to read GT min frequency\n");
	if (!sysfs_card_read("gt_max_freq_mhz", &gputop_devinfo.gt_max_freq))
	    fprintf(stderr, "Unable to read GT max frequency\n");
	gputop_devinfo.gt_min_freq *= 1000000;
	gputop_devinfo.gt_max_freq *= 1000000;
    }

    if (devinfo->is_haswell) {
	SET_NAMES(gputop_devinfo, "hsw", "Haswell");
	gen_metrics = gputop_oa_get_metrics_hsw(&gputop_devinfo);
    } else if (devinfo->is_broadwell) {
	SET_NAMES(gputop_devinfo, "bdw", "Broadwell");
	gen_metrics = gputop_oa_get_metrics_bdw(&gputop_devinfo);
    } else if (devinfo->is_cherryview) {
	SET_NAMES(gputop_devinfo, "chv", "Cherryview");
	gen_metrics = gputop_oa_get_metrics_chv(&gputop_devinfo);
    } else if (devinfo->is_skylake) {
	switch (devinfo->gt) {
	case 2:
	    SET_NAMES(gputop_devinfo, "sklgt2", "Skylake GT2");
	    gen_metrics = gputop_oa_get_metrics_sklgt2(&gputop_devinfo);
	    break;
	case 3:
	    SET_NAMES(gputop_devinfo, "sklgt3", "Skylake GT3");
	    gen_metrics = gputop_oa_get_metrics_sklgt3(&gputop_devinfo);
	    break;
	case 4:
	    SET_NAMES(gputop_devinfo, "sklgt4", "Skylake GT4");
	    gen_metrics = gputop_oa_get_metrics_sklgt4(&gputop_devinfo);
	    break;
	default:
	    fprintf(stderr, "Unsupported GT%u Skylake System\n", devinfo->gt);
	    return false;
	}
    } else if (devinfo->is_broxton) {
	SET_NAMES(gputop_devinfo, "bxt", "Broxton");
	gen_metrics = gputop_oa_get_metrics_bxt(&gputop_devinfo);
    } else if (devinfo->is_kabylake) {
	switch (devinfo->gt) {
	case 2:
	    SET_NAMES(gputop_devinfo, "kblgt2", "Kabylake GT2");
	    gen_metrics = gputop_oa_get_metrics_kblgt2(&gputop_devinfo);
	    break;
	case 3:
	    SET_NAMES(gputop_devinfo, "kblgt3", "Kabylake GT3");
	    gen_metrics = gputop_oa_get_metrics_kblgt3(&gputop_devinfo);
	    break;
	default:
	    fprintf(stderr, "Unsupported GT%u Kabylake System\n", devinfo->gt);
	    return false;
	}
    } else if (devinfo->is_geminilake) {
	SET_NAMES(gputop_devinfo, "glk", "Geminilake");
	gen_metrics = gputop_oa_get_metrics_glk(&gputop_devinfo);
    } else if (devinfo->is_coffeelake) {
	switch (devinfo->gt) {
	case 2:
	    SET_NAMES(gputop_devinfo, "cflgt2", "Coffeelake GT2");
	    gen_metrics = gputop_oa_get_metrics_cflgt2(&gputop_devinfo);
	    break;
	case 3:
	    SET_NAMES(gputop_devinfo, "cflgt3", "Coffeelake GT3");
	    gen_metrics = gputop_oa_get_metrics_cflgt3(&gputop_devinfo);
	    break;
	default:
	    fprintf(stderr, "Unsupported GT%u Coffeelake System\n", devinfo->gt);
	    return false;
	}
    } else if (devinfo->is_cannonlake) {
        SET_NAMES(gputop_devinfo, "cnl", "Cannonlake");
	gen_metrics = gputop_oa_get_metrics_cnl(&gputop_devinfo);
    } else if (devinfo->is_elkhartlake) {
        SET_NAMES(gputop_devinfo, "EHL", "Elkhartlake");
	gen_metrics = gputop_oa_get_metrics_lkf(&gputop_devinfo);
    } else if (devinfo->gen == 11) {
        SET_NAMES(gputop_devinfo, "icl", "Icelake");
	gen_metrics = gputop_oa_get_metrics_icl(&gputop_devinfo);
    } else {
	fprintf(stderr, "Unknown System\n");
	return false;
    }

    if (gputop_fake_mode)
	SET_NAMES(gputop_devinfo, "bdw", "Fake Broadwell Intel device");
    else {
      gputop_devinfo.has_dynamic_configs =
	kernel_has_dynamic_config_support(fd);
    }

    return true;

#undef SET_NAMES
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
	uint64_t elapsed_time = gputop_get_time() - stream->start_time;
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
    case GPUTOP_STREAM_CPU:
	if (stream->cpu.stats_buf_pos == 0 && !stream->cpu.stats_buf_full)
	    return false;
	else
	    return true;
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
 *          XXX: Note: we do this after checking for new records so we
 *          don't have to worry about the corner case of eating more
 *          than we previously knew about.
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
    const struct drm_i915_perf_record_header *header;
    int offset = 0;

    printf("records:\n");

    for (offset = 0; offset < len; offset += header->size) {
	header = (const struct drm_i915_perf_record_header *)(buf + offset);

	if (header->size == 0) {
	    printf("Spurious header size == 0\n");
	    return;
	}
	printf("- header size = %d\n", header->size);

	switch (header->type) {

	case DRM_I915_PERF_RECORD_OA_BUFFER_LOST:
	    printf("- OA buffer error - all records lost\n");
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
    struct drm_i915_perf_record_header header;
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
gputop_perf_fake_read(struct gputop_perf_stream *stream,
		      uint8_t *buf, int buf_length)
{
    struct report_layout *report = (struct report_layout *)buf;
    struct drm_i915_perf_record_header header;
    uint32_t timestamp, elapsed_clocks;
    int i;
    uint64_t counter;
    uint64_t elapsed_time = gputop_get_time() - stream->start_time;
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
    do {
	int offset = 0;
	int buf_idx;
	uint8_t *buf;
	int count;

	/* We double buffer reads so we can safely keep a pointer to
	 * our last sample for calculating deltas */
	buf_idx = !stream->oa.last_buf_idx;
	buf = stream->oa.bufs[buf_idx];

	if (gputop_fake_mode)
	    count = gputop_perf_fake_read(stream, buf, stream->oa.buf_sizes);
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
	    const struct drm_i915_perf_record_header *header =
		(const struct drm_i915_perf_record_header *)(buf + offset);

	    if (header->size == 0) {
		dbg("Spurious header size == 0\n");
		/* XXX: How should we handle this instead of exiting() */
		exit(1);
	    }

	    offset += header->size;

	    switch (header->type) {

	    case DRM_I915_PERF_RECORD_OA_BUFFER_LOST:
		dbg("i915 perf: OA buffer error - all records lost\n");
		break;
	    case DRM_I915_PERF_RECORD_OA_REPORT_LOST:
		dbg("i915 perf: OA report lost\n");
		break;

	    case DRM_I915_PERF_RECORD_SAMPLE: {
		struct oa_sample *sample = (struct oa_sample *)header;
		uint8_t *report = sample->oa_report;

		if (stream->oa.last)
		    gputop_perf_current_user->sample(stream, stream->oa.last, report);

		stream->oa.last = report;

		/* track which buffer oa.last points into so our next read
		 * won't clobber it... */
		stream->oa.last_buf_idx = buf_idx;
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
    case GPUTOP_STREAM_CPU:
	assert(0);
	return;
    }

    assert(0);
}

static int
get_card_for_fd(int fd)
{
    struct stat sb;
    int mjr, mnr;
    char buffer[128];
    DIR *drm_dir;
    struct dirent *entry;
    int retval = -1;

    if (fstat(fd, &sb)) {
	gputop_log(GPUTOP_LOG_LEVEL_HIGH, "Failed to stat DRM fd\n", -1);
	return -1;
    }

    mjr = major(sb.st_rdev);
    mnr = minor(sb.st_rdev);

    snprintf(buffer, sizeof(buffer), "/sys/dev/char/%d:%d/device/drm", mjr, mnr);

    drm_dir = opendir(buffer);
    assert(drm_dir != NULL);

    while ((entry = readdir(drm_dir))) {
	if (entry->d_type == DT_DIR && strncmp(entry->d_name, "card", 4) == 0) {
	    retval = strtoull(entry->d_name + 4, NULL, 10);
	    break;
	}
    }

    closedir(drm_dir);

    return retval;
}

static uint32_t
read_device_param(const char *stem, int id, const char *param)
{
    char *name;
    int ret = asprintf(&name, "/sys/class/drm/%s%u/device/%s", stem, id, param);
    uint64_t value;
    bool success;

    assert(ret != -1);

    success = gputop_read_file_uint64(name, &value);
    free(name);

    return success ? value : 0;
}

static int
find_intel_render_node(void)
{
    for (int i = 128; i < (128 + 16); i++) {
	if (read_device_param("renderD", i, "vendor") == 0x8086)
	    return i;
    }

    return -1;
}

static int
open_render_node(struct intel_device *dev)
{
    char *name;
    int ret;
    int fd;

    int render = find_intel_render_node();
    if (render < 0)
	return -1;

    ret = asprintf(&name, "/dev/dri/renderD%u", render);
    assert(ret != -1);

    fd = open(name, O_RDWR);
    free(name);

    if (fd == -1)
	return -1;

    dev->device = read_device_param("renderD", render, "device");
    dev->subsystem_device = read_device_param("renderD",
					      render, "subsystem_device");
    dev->subsystem_vendor = read_device_param("renderD",
					      render, "subsystem_vendor");

    return fd;
}

static void
gputop_reload_userspace_metrics(int fd)
{
    if (!gputop_devinfo.has_dynamic_configs)
	return;

    list_for_each_entry(struct gputop_metric_set, metric_set,
                        &gen_metrics->metric_sets, link) {
	struct drm_i915_perf_oa_config config;
	char config_path[256];
	uint64_t config_id;
	int ret;

	snprintf(config_path, sizeof(config_path), "metrics/%s/id",
		 metric_set->hw_config_guid);

	if (sysfs_card_read(config_path, &config_id)) {
	    if (config_id > 1)
		perf_ioctl(fd, DRM_IOCTL_I915_PERF_REMOVE_CONFIG, &config_id);
	    else if (config_id == 1)
		continue; /* Leave the test config untouched */
	}

	memset(&config, 0, sizeof(config));

	memcpy(config.uuid, metric_set->hw_config_guid, sizeof(config.uuid));

	config.n_mux_regs = metric_set->n_mux_regs;
	config.mux_regs_ptr = (uintptr_t) metric_set->mux_regs;

	config.n_boolean_regs = metric_set->n_b_counter_regs;
	config.boolean_regs_ptr = (uintptr_t) metric_set->b_counter_regs;

	config.n_flex_regs = metric_set->n_flex_regs;
	config.flex_regs_ptr = (uintptr_t) metric_set->flex_regs;

	ret = perf_ioctl(fd, DRM_IOCTL_I915_PERF_ADD_CONFIG, &config);
	if (ret < 0)
	    fprintf(stderr, "Failed to load %s (%s) metrics set in kernel: %s\n",
		    metric_set->symbol_name, metric_set->hw_config_guid, strerror(errno));
    }
}

static bool
gputop_enumerate_metrics_via_sysfs(void)
{
    DIR *metrics_dir;
    struct dirent *entry;
    char buffer[128];
    uint64_t paranoid = 0;

    if (!gputop_read_file_uint64("/proc/sys/dev/i915/perf_stream_paranoid", &paranoid)) {
        fprintf(stderr, "Kernel is missing i915 perf support\n");
        return false;
    }

    if (getuid() != 0 && paranoid) {
        fprintf(stderr, "Warning, i915 perf is in paranoid mode\n"
                "You might want to run :\n"
                "\tsudo sysctl dev.i915.perf_stream_paranoid=0\n");
    }

    assert(drm_card >= 0);
    snprintf(buffer, sizeof(buffer), "/sys/class/drm/card%d/metrics", drm_card);

    metrics_dir = opendir(buffer);
    if (metrics_dir == NULL)
	return false;

    while ((entry = readdir(metrics_dir))) {
	struct gputop_metric_set *metric_set;
	struct hash_entry *metrics_entry;

	if (entry->d_type != DT_DIR || entry->d_name[0] == '.')
	    continue;

	metrics_entry =
	    _mesa_hash_table_search(gen_metrics->metric_sets_map, entry->d_name);

	if (metrics_entry == NULL)
	    continue;

	metric_set = (struct gputop_metric_set*)metrics_entry->data;

	snprintf(buffer, sizeof(buffer), "metrics/%s/id",
		 metric_set->hw_config_guid);

	if (sysfs_card_read(buffer, &metric_set->perf_oa_metrics_set)) {
	    array_append(gputop_perf_oa_supported_metric_set_uuids,
			 &metric_set->hw_config_guid);
	}
    }
    closedir(metrics_dir);

    return true;
}

// function that hard-codes the guids specific for the broadwell configuration
bool
gputop_enumerate_metrics_fake(void)
{
    static const char *fake_bdw_guids[] = {
        "b541bd57-0e0f-4154-b4c0-5858010a2bf7",
        "35fbc9b2-a891-40a6-a38d-022bb7057552",
        "233d0544-fff7-4281-8291-e02f222aff72",
        "2b255d48-2117-4fef-a8f7-f151e1d25a2c",
        "f7fd3220-b466-4a4d-9f98-b0caf3f2394c",
        "e99ccaca-821c-4df9-97a7-96bdb7204e43",
        "27a364dc-8225-4ecb-b607-d6f1925598d9",
        "857fc630-2f09-4804-85f1-084adfadd5ab",
        "343ebc99-4a55-414c-8c17-d8e259cf5e20",
        "78490af2-10fa-430b-ae3c-94ec04d5214e",
        "c0abdd97-3b13-4cad-814c-bd178804e02c",
        "ad665281-a7cf-483a-bd10-0e07c43f61c7",
        "930a15aa-4300-4fce-a9ba-edb0b9e880be",
        "d7793b26-b5e3-4f0f-ad78-7ebc9d0b4c7d",
        "da0f7875-1143-4d73-a39c-9128a951c46a",
        "52c186e4-39e3-4534-87cd-41bd47763df9",
        "edcb8c31-764d-451a-9ecd-c9c89fb54f8d",
        "729fc3f4-ccff-4902-be6b-f1a22cc92c02",
        "29598975-4785-43ab-a981-1dfa58d0e835",
        "8fb61ba2-2fbb-454c-a136-2dec5a8a595e",
        "e1743ca0-7fc8-410b-a066-de7bbb9280b7",
        "0a9eb7be-feee-4275-a139-6d9cedf0fdb0",
        "d6de6f55-e526-4f79-a6a6-d7315c09044e",
        "e713f347-953e-4d8c-b02f-6be31df2db2b",
    };

    struct gputop_metric_set *metric_set;
    struct hash_entry *metrics_entry;

    int i;

    for (i = 0; i < ARRAY_SIZE(fake_bdw_guids); i++){
	metrics_entry = _mesa_hash_table_search(gen_metrics->metric_sets_map,
                                                fake_bdw_guids[i]);
	metric_set = (struct gputop_metric_set*)metrics_entry->data;
	metric_set->perf_oa_metrics_set = i;
	array_append(gputop_perf_oa_supported_metric_set_uuids, &metric_set->hw_config_guid);
    }

    return true;
}

bool
gputop_perf_initialize(void)
{
    struct gen_device_info devinfo;

    list_inithead(&ctx_handles_list);

    if (gputop_devinfo.topology.max_slices)
	return true;

    if (getenv("GPUTOP_FAKE_MODE") && strcmp(getenv("GPUTOP_FAKE_MODE"), "1") == 0) {
	gputop_fake_mode = true;
	intel_dev.device = 5654; // broadwell specific id
    } else {
	drm_fd = open_render_node(&intel_dev);
	if (drm_fd < 0) {
	    gputop_log(GPUTOP_LOG_LEVEL_HIGH, "Failed to open render node", -1);
	    return false;
	}
	drm_card = get_card_for_fd(drm_fd);
    }

    if (getenv("GPUTOP_DISABLE_OACONFIG") && strcmp(getenv("GPUTOP_DISABLE_OACONFIG"), "1") == 0)
	gputop_disable_oaconfig = true;

    /* NB: eu_count needs to be initialized before declaring counters */
    page_size = sysconf(_SC_PAGE_SIZE);

    gen_metrics = NULL;
    gputop_perf_oa_supported_metric_set_uuids = array_new(sizeof(char*), 1);

    if (!gen_get_device_info_from_pci_id(intel_dev.device, &devinfo)) {
	gputop_log(GPUTOP_LOG_LEVEL_HIGH, "Failed to recognize device id\n", -1);
	return false;
    }

    if (init_dev_info(drm_fd, intel_dev.device, &devinfo)) {
	if (gputop_fake_mode)
	    return gputop_enumerate_metrics_fake();
	else {
	    gputop_reload_userspace_metrics(drm_fd);
	    return gputop_enumerate_metrics_via_sysfs();
	}
    } else
	return false;
}

void
gputop_perf_free(void)
{
    ralloc_free(gen_metrics);
    gen_metrics = NULL;
    array_free(gputop_perf_oa_supported_metric_set_uuids);
}

const struct gputop_devinfo *
gputop_perf_get_devinfo(void)
{
    return &gputop_devinfo;
}
