/*
 * GPU Top
 *
 * Copyright (C) 2017 Intel Corporation
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

#ifndef __GPUTOP_NETWORK_H__
#define __GPUTOP_NETWORK_H__

#include <stdbool.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

/* This interface should be implemented by the embedder. */

typedef struct _gputop_connection_t gputop_connection_t;

typedef void (*gputop_on_ready_cb_t)(gputop_connection_t *conn,
                                     void *user_data);
typedef void (*gputop_on_data_cb_t)(gputop_connection_t *conn,
                                    const void *data, size_t len,
                                    void *user_data);
typedef void (*gputop_on_close_cb_t)(gputop_connection_t *conn,
                                     const char *error,
                                     void *user_data);

gputop_connection_t *gputop_connect(const char *host, int port,
                                    gputop_on_ready_cb_t ready_cb,
                                    gputop_on_data_cb_t data_cb,
                                    gputop_on_close_cb_t close_cb,
                                    void *user_data);

void gputop_connection_send(gputop_connection_t *conn,
                            const void *data, size_t len);

void gputop_connection_close(gputop_connection_t *conn);

bool gputop_connection_connected(gputop_connection_t *conn);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* __GPUTOP_NETWORK_H__ */
