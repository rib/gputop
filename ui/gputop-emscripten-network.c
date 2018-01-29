/*
 * GPU Top
 *
 * Copyright (C) 2018 Intel Corporation
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

#include "gputop-network.h"

#include <emscripten/emscripten.h>

struct _gputop_connection_t {
    int websocket;
    bool connected;

    gputop_on_ready_cb_t ready_cb;
    gputop_on_data_cb_t data_cb;
    gputop_on_close_cb_t close_cb;
    void *user_data;
};

static void
gputop_connection_free(gputop_connection_t *conn)
{
    free(conn);
}

EMSCRIPTEN_KEEPALIVE static void
gputop_emscripten_network_on_message(gputop_connection_t *conn,
                                     const void *data, size_t length)
{
    conn->data_cb(conn, data, length, conn->user_data);
}

EMSCRIPTEN_KEEPALIVE static void
gputop_emscripten_network_on_close(gputop_connection_t *conn,
                                   int code,
                                   const char *message)
{
    conn->connected = false;
    conn->close_cb(conn, code != 1000 ? message : NULL, conn->user_data);
    gputop_connection_free(conn);
}

EMSCRIPTEN_KEEPALIVE static void
gputop_emscripten_network_on_open(gputop_connection_t *conn)
{
    conn->connected = true;
    conn->ready_cb(conn, conn->user_data);
}

gputop_connection_t *
gputop_connect(const char *host, int port,
               gputop_on_ready_cb_t ready_cb,
               gputop_on_data_cb_t data_cb,
               gputop_on_close_cb_t close_cb,
               void *user_data)
{
    gputop_connection_t *conn = calloc(1, sizeof(gputop_connection_t));

    conn->ready_cb = ready_cb;
    conn->data_cb = data_cb;
    conn->close_cb = close_cb;

    conn->websocket = EM_ASM_INT({
            if (Module['_gputop_websockets'] === undefined) {
                Module['_gputop_websockets'] = {};
                Module['_gputop_websockets_id'] = 1;
            }
            var url = 'ws://' + Pointer_stringify($1) + ':' + $2 + '/gputop';
            var ws = new WebSocket(url, 'binary');
            console.log('Connecting to ' + url);
            ws.binaryType = 'arraybuffer';
            var id = Module['_gputop_websockets_id'];
            Module['_gputop_websockets_id'] += 1;
            Module['_gputop_websockets'][id] = ws;
            ws.onopen = function() { Module['_gputop_emscripten_network_on_open']($0); };
            ws.onclose = function(event) {
                console.log('close code='+ event.code + ' message=' + event.reason);
                Module['_gputop_emscripten_network_on_close'](
                    $0, event.code, event.reason);
            };
            ws.onmessage = function(event) {
                if (event.data instanceof ArrayBuffer) {
                    ccall('gputop_emscripten_network_on_message',
                          null, ['number', 'array', 'number'],
                          [$0, new Uint8Array(event.data), event.data.byteLength]);
                } else
                    console.error('Unable to handle WebSocket message', event.data);
            };
            return id;
        }, conn, host, port);

    return conn;
}

void
gputop_connection_send(gputop_connection_t *conn, const void *data, size_t len)
{
    if (conn == NULL || conn->websocket == 0 || data == NULL || len == 0)
        return;

    EM_ASM_INT({
            var ws = Module['_gputop_websockets'][$0];
            if (ws === undefined)
                return;

            var bytes = new Uint8Array(Module.HEAPU8.buffer, $1, $2);
            ws.send(bytes);
        }, conn->websocket, data, len);
}

void
gputop_connection_close(gputop_connection_t *conn)
{
    if (conn == NULL)
        return;

    EM_ASM({
            var ws = Module['_gputop_websockets'][$0];
            if (ws === undefined)
                return;
            ws.close();
        }, conn->websocket);
}

bool
gputop_connection_connected(gputop_connection_t *conn)
{
    if (!conn || !conn->websocket)
        return false;
    return conn->connected;
}
