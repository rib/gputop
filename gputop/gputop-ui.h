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

#ifndef _GPUTOP_UI_H_
#define _GPUTOP_UI_H_

#include <stdio.h>

#include "gputop-log.h"

#ifdef SUPPORT_WEBUI
#include "gputop.pb-c.h"
#endif

extern uv_loop_t *gputop_ui_loop;

void *gputop_ui_run(void *arg);

void gputop_ui_quit_idle_cb(uv_idle_t *idle);

#ifdef GPUTOP_ENABLE_DEBUG

#define dbg(format, ...) do { \
    char *message; \
    asprintf(&message, format, ##__VA_ARGS__); \
    gputop_log(GPUTOP_LOG_LEVEL_NOTIFICATION, message, -1); \
} while(0)

#else

#define dbg(format, ...) do { } while(0)

#endif

#endif /* _GPUTOP_UI_H_ */
