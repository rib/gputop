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

/* NB: We use a portable stdatomic.h, so we don't depend on a recent compiler...
 */
#include "stdatomic.h"

#include "util/list.h"

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
#define MAX_QUERY_COUNTERS 1000

struct intel_query_info
{
    struct list_head link;

    unsigned id;
    unsigned n_counters;
    unsigned max_queries;
    unsigned n_active_queries;
    unsigned max_counter_data_len;
    unsigned caps_mask;

    struct intel_counter counters[MAX_QUERY_COUNTERS];

    char name[128];
};

struct gl_perf_query
{
    struct list_head link;
    struct intel_query_info *info;
    unsigned handle;
    uint8_t data[]; /* len == query_info->max_counter_data_len */
};

struct winsys_context
{
    int ref;

    GLXContext glx_ctx;
    /* TODO: Add EGL support */

    struct winsys_surface *read_wsurface;
    struct winsys_surface *draw_wsurface;

    bool gl_initialised;

    struct list_head queries;
    struct intel_query_info *current_query;

    pthread_rwlock_t query_obj_cache_lock;
    struct list_head query_obj_cache;

    bool try_create_new_context_failed;
    bool is_debug_context;
    bool khr_debug_enabled;

    GLint scissor_x;
    GLint scissor_y;
    GLsizei scissor_width;
    GLsizei scissor_height;
    bool scissor_enabled;

};

struct winsys_surface
{
    struct winsys_context *wctx;

    /* Ignore pixmaps for now */
    GLXWindow glx_window;
    /* TODO: Add EGL support */

    /* not pending until glEndPerfQueryINTEL is called... */
    struct gl_perf_query *open_query_obj;

    struct list_head pending_queries;

    /* Finished queries, waiting to be picked up by the server thread */
    pthread_rwlock_t finished_queries_lock;
    struct list_head finished_queries;
};

extern bool gputop_gl_has_intel_performance_query_ext;

extern pthread_rwlock_t gputop_gl_lock;
extern struct array *gputop_gl_contexts;
extern struct array *gputop_gl_surfaces;

extern bool gputop_gl_force_debug_ctx_enabled;

extern atomic_bool gputop_gl_monitoring_enabled;
extern atomic_bool gputop_gl_khr_debug_enabled;

extern atomic_bool gputop_gl_scissor_test_enabled;

/* The number of query objects to delete if GL monitoring is disabled...
 *
 * We aim to delete all query objects when switching between GL
 * performance queries as a way to tell OpenGL will relinquish any
 * exclusive state associated with the previous query that might block
 * us from opening a new query or starting to collect system wide
 * metrics.
 *
 * The deletion of query objects is handled with swap-buffers which
 * which depends on being called by the application. The server
 * thread can check this atomic counter as a way to report
 * to the user that we're still busy waiting for GL.
 */
extern atomic_int gputop_gl_n_queries;


#endif /* _GPUTOP_GL_H_ */
