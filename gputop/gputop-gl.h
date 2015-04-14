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

#ifndef _GPUTOP_GL_H_
#define _GPUTOP_GL_H_

#include <GL/glx.h>
#include <GL/glxext.h>

#include "gputop-list.h"

struct intel_counter
{
    uint64_t max_raw_value;

    unsigned id;
    unsigned type;

    unsigned data_offset;
    unsigned data_size;
    unsigned data_type;

    char name[64];
    char description[256];
};
#define MAX_QUERY_COUNTERS 100

struct intel_query_info
{
    unsigned id;
    unsigned n_counters;
    unsigned max_queries;
    unsigned n_active_queries;
    unsigned max_counter_data_len;
    unsigned caps_mask;

    struct intel_counter counters[MAX_QUERY_COUNTERS];

    char name[128];
};

struct frame_query
{
    pthread_rwlock_t lock;

    unsigned oa_query;
    uint8_t *oa_data;
    unsigned oa_data_len;

    unsigned pipeline_stats_query;
    uint8_t *pipeline_stats_data;
    unsigned pipeline_stats_data_len;
};
/* We pipeline up to three per-frame queries so we don't
 * need to block waiting for one frame to finish before
 * we can start a query for the next frame and we can
 * lock a frame while we read the counters for display
 * without blocking the GL thread.
 */
#define MAX_FRAME_QUERIES 3

struct winsys_context
{
    _Atomic int ref;

    GLXContext glx_ctx;
    /* TODO: Add EGL support */

    struct winsys_surface *read_wsurface;
    struct winsys_surface *draw_wsurface;

    bool gl_initialised;

    struct intel_query_info pipeline_stats_query_info;
    struct intel_query_info oa_query_info;

    bool try_create_new_context_failed;
    bool is_debug_context;
    bool khr_debug_enabled;
};

struct winsys_surface
{
    struct winsys_context *wctx;

    /* Ignore pixmaps for now */
    GLXWindow glx_window;
    /* TODO: Add EGL support */

    struct frame_query frames[MAX_FRAME_QUERIES];
    _Atomic int started_frames;
    _Atomic int finished_frames;

    /* One or more frames have associated monitors that
     * will need to be deleted monitoring is disabled...
     */
    bool has_monitors;
};

extern bool gputop_has_intel_performance_query_ext;
extern bool gputop_has_khr_debug_ext;

extern pthread_rwlock_t gputop_gl_lock;
extern struct array *gputop_gl_contexts;
extern struct array *gputop_gl_surfaces;

extern bool gputop_gl_force_debug_ctx_enabled;

extern _Atomic bool gputop_gl_monitoring_enabled;
extern _Atomic bool gputop_gl_khr_debug_enabled;

/* The number of monitors to delete if monitoring is disabled...
 *
 * We aim to delete all monitors when monitoring is disable in the
 * hope that OpenGL will relinquish exclusive access to the kernel
 * perf interface used to collect counter metrics so that gputop
 * can switch from per-gl-context profiling to system wide
 * profiling.
 *
 * When monitoring is disabled then the monitors associated with
 * each surface are only destroyed on the next swap-buffers
 * request for each surface so we don't have much control over
 * when exactly that will be.
 *
 * The UI thread can check this atomic counter as a way to report
 * to the user that we're still busy waiting for GL before system
 * wide monitoring can be enabled.
 */
extern _Atomic int gputop_gl_n_monitors;


#endif /* _GPUTOP_GL_H_ */
