/*
 * GPU Top
 *
 * Copyright (C) 2014,2015 Intel Corporation
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

#include <stdint.h>
#include <inttypes.h>
#include <unistd.h>
#include <libgen.h>

#include <locale.h>

#include <uv.h>

/* NB: We use a portable stdatomic.h, so we don't depend on a recent compiler...
 */
#include "stdatomic.h"

#include "gputop-perf.h"
#include "gputop-mainloop.h"
#include "gputop-util.h"
#include "gputop-sysutil.h"
#include "gputop-server.h"
#include "gputop-log.h"

static uv_timer_t fake_timer;
static pthread_t server_thread_id;

uv_loop_t *gputop_mainloop;



static void
exit_fake_mode_cb(uv_timer_t *timer)
{
    exit(0);
}

void
gputop_mainloop_quit_idle_cb(uv_idle_t *idle)
{
    fprintf(stderr, "%s\n", (char *)idle->data);
    fprintf(stderr, "\n");

    _exit(EXIT_FAILURE);
}

static void *
run(void *arg)
{
    gputop_mainloop = uv_loop_new();

    if (!gputop_server_run())
	_exit(EXIT_FAILURE);

    if (gputop_fake_mode && gputop_get_bool_env("GPUTOP_TRAVIS_MODE")) {
        uv_timer_init(gputop_mainloop, &fake_timer);
        uv_timer_start(&fake_timer, exit_fake_mode_cb, 10000, 10000);
    }

    uv_run(gputop_mainloop, UV_RUN_DEFAULT);

    gputop_perf_free();

    return 0;
}

__attribute__((constructor)) void
gputop_init(void)
{
    pthread_attr_t attrs;

    gputop_perf_initialize();

    pthread_attr_init(&attrs);
    pthread_create(&server_thread_id, &attrs, run, NULL);
}
