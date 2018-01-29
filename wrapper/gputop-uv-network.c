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

#include <string.h>
#include <unistd.h>

#include <uv.h>
#include <wslay/wslay_event.h>

#include "util/macros.h"

struct _gputop_connection_t {
    bool open;

    uv_tcp_t tcp_handle;
    uv_connect_t connect_req;

    uv_write_t write_header_req;

    const uv_buf_t *read_buf;
    size_t iread, nread;

    uv_check_t check_handle;

    wslay_event_context_ptr wslay_ctx;

    gputop_on_ready_cb_t ready_cb;
    gputop_on_data_cb_t data_cb;
    gputop_on_close_cb_t close_cb;
    void *user_data;

    char http_client_header[1024];
    char http_server_header[64 * 1024];
};

static char encoding_table[] = {'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H',
                                'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P',
                                'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X',
                                'Y', 'Z', 'a', 'b', 'c', 'd', 'e', 'f',
                                'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n',
                                'o', 'p', 'q', 'r', 's', 't', 'u', 'v',
                                'w', 'x', 'y', 'z', '0', '1', '2', '3',
                                '4', '5', '6', '7', '8', '9', '+', '/'};
static int mod_table[] = {0, 2, 1};

static void
base64_encode(const uint8_t *data_in, size_t input_length, uint8_t *data_out)
{
    size_t output_length = 4 * ((input_length + 2) / 3);

    for (int i = 0, j = 0; i < input_length;) {

        uint32_t octet_a = i < input_length ? (unsigned char)data_in[i++] : 0;
        uint32_t octet_b = i < input_length ? (unsigned char)data_in[i++] : 0;
        uint32_t octet_c = i < input_length ? (unsigned char)data_in[i++] : 0;

        uint32_t triple = (octet_a << 0x10) + (octet_b << 0x08) + octet_c;

        data_out[j++] = encoding_table[(triple >> 3 * 6) & 0x3F];
        data_out[j++] = encoding_table[(triple >> 2 * 6) & 0x3F];
        data_out[j++] = encoding_table[(triple >> 1 * 6) & 0x3F];
        data_out[j++] = encoding_table[(triple >> 0 * 6) & 0x3F];
    }

    for (int i = 0; i < mod_table[input_length % 3]; i++)
        data_out[output_length - 1 - i] = '=';

    data_out[output_length] = '\0';
}

static void
on_close_cb(uv_handle_t *handle)
{
    gputop_connection_t *conn = handle->data;

    wslay_event_context_free(conn->wslay_ctx);
    free(conn);
}

static void
gputop_connection_end(gputop_connection_t *conn, const char *error)
{
    conn->close_cb(conn, error, conn->user_data);
    uv_read_stop((uv_stream_t *) &conn->tcp_handle);
    uv_check_stop(&conn->check_handle);
    uv_close((uv_handle_t *) &conn->tcp_handle, on_close_cb);
}

static int
on_wslay_genmask_cb(wslay_event_context_ptr ctx,
                    uint8_t *buf, size_t len,
                    void *user_data)
{
    for (size_t i = 0; i < len; i++)
        buf[i] = rand() & 0xff;
    return 0;
}

static void
on_wslay_msg_recv_cb(wslay_event_context_ptr ctx,
                     const struct wslay_event_on_msg_recv_arg *arg,
                     void *user_data)
{
    gputop_connection_t *conn = user_data;

    if (arg->opcode == WSLAY_CONNECTION_CLOSE) {
        gputop_connection_end(conn, NULL);
        return;
    }

    conn->data_cb(conn, arg->msg, arg->msg_length, conn->user_data);
}

static ssize_t
on_wslay_recv_cb(wslay_event_context_ptr ctx,
                 uint8_t *buf, size_t len,
                 int flags, void *user_data)
{
    gputop_connection_t *conn = user_data;
    int l;

    if (!conn->read_buf || conn->iread >= conn->nread) {
        wslay_event_set_error(ctx, WSLAY_ERR_WOULDBLOCK);
        return -1;
    }

    l = MIN2(len, conn->nread - conn->iread);
    memcpy(buf, &conn->read_buf->base[conn->iread], l);
    conn->iread += l;

    return l;
}

static ssize_t
on_wslay_send_cb(wslay_event_context_ptr ctx,
                 const uint8_t *data, size_t len,
                 int flags, void *user_data)
{
    gputop_connection_t *conn = user_data;
    const uv_buf_t buf = uv_buf_init((char *)data, len);

    return uv_try_write((uv_stream_t *) &conn->tcp_handle, &buf, 1);
}

static void
on_uv_read_alloc_cb(uv_handle_t *handle, size_t size, uv_buf_t *buf)
{
    gputop_connection_t *conn = handle->data;

    buf->base = conn->http_server_header;
    buf->len = sizeof(conn->http_server_header);
}

static void
on_uv_read_cb(uv_stream_t *handle, ssize_t nread, const uv_buf_t *buf)
{
    gputop_connection_t *conn = handle->data;

    if (!conn->open) {
        conn->open = true;
        conn->ready_cb(conn, conn->user_data);
        return;
    }

    if (nread == UV_EOF) {
        gputop_connection_end(conn, NULL);
        return;
    }

    conn->read_buf = buf;
    conn->iread = 0;
    conn->nread = nread;
    wslay_event_recv(conn->wslay_ctx);

    conn->read_buf = NULL;
    conn->iread = 0;

}

static void
on_uv_connect_cb(uv_connect_t* req, int status)
{
    gputop_connection_t *conn = req->handle->data;
    uv_buf_t buf;

    if (status != 0) {
        gputop_connection_end(conn, uv_err_name(status));
        return;
    }

    uv_read_start((uv_stream_t *) &conn->tcp_handle,
                  on_uv_read_alloc_cb,
                  on_uv_read_cb);

    buf = uv_buf_init(conn->http_client_header,
                      strlen(conn->http_client_header));
    uv_write(&conn->write_header_req,
             (uv_stream_t *) &conn->tcp_handle,
             &buf, 1, NULL);
}

static void
on_check_cb(uv_check_t* handle)
{
    gputop_connection_t *conn = handle->data;

    if (wslay_event_want_write(conn->wslay_ctx)) {
        if (wslay_event_send(conn->wslay_ctx) != 0)
            gputop_connection_close(conn);
    }
    if (wslay_event_want_read(conn->wslay_ctx)) {
        if (wslay_event_recv(conn->wslay_ctx) != 0)
            gputop_connection_close(conn);
    }
}

gputop_connection_t *
gputop_connect(const char *host, int port,
               gputop_on_ready_cb_t ready_cb,
               gputop_on_data_cb_t data_cb,
               gputop_on_close_cb_t close_cb,
               void *user_data)
{
    gputop_connection_t *conn = calloc(1, sizeof(gputop_connection_t));
    uint8_t ws_key[16];
    uint8_t ws_key_b64[100];
    struct wslay_event_callbacks callbacks = {
        on_wslay_recv_cb,
        on_wslay_send_cb,
        on_wslay_genmask_cb,
        NULL,
        NULL,
        NULL,
        on_wslay_msg_recv_cb,
    };
    uv_getaddrinfo_t req;
    int i;

    conn->ready_cb = ready_cb;
    conn->data_cb = data_cb;
    conn->close_cb = close_cb;

    wslay_event_context_client_init(&conn->wslay_ctx, &callbacks, conn);

    uv_tcp_init(uv_default_loop(), &conn->tcp_handle);
    conn->tcp_handle.data = conn;
    //uv_stream_set_blocking((uv_stream_t *)&conn->tcp_handle, 0);

    if (uv_getaddrinfo(uv_default_loop(), &req, NULL, host, NULL, NULL) == 0) {
        union {
            struct sockaddr addr;
            struct sockaddr_in addr4;
            struct sockaddr_in6 addr6;
        } s;

        memcpy(&s.addr6, req.addrinfo->ai_addr, req.addrinfo->ai_addrlen);
        if (req.addrinfo->ai_family == AF_INET6) {
            s.addr6.sin6_port = htons(port);
        } else {
            s.addr4.sin_port = htons(port);
        }
        uv_tcp_connect(&conn->connect_req,
                       &conn->tcp_handle,
                       &s.addr,
                       on_uv_connect_cb);
        uv_freeaddrinfo(req.addrinfo);
    } else {
        struct sockaddr_in addr;

        uv_ip4_addr("127.0.0.1", port, &addr);
        uv_tcp_connect(&conn->connect_req,
                       &conn->tcp_handle,
                       (const struct sockaddr*) &addr,
                       on_uv_connect_cb);
    }

    for (i = 0; i < ARRAY_SIZE(ws_key); i++)
        ws_key[i] = rand() % 0xff;

    base64_encode(ws_key, sizeof(ws_key), ws_key_b64);

    snprintf(conn->http_client_header, sizeof(conn->http_client_header),
             "GET /gputop HTTP/1.1\r\n"
             "Host: %s:%u\r\n"
             "Upgrade: websocket\r\n"
             "Connection: Upgrade\r\n"
             "Sec-WebSocket-Key: %s\r\n"
             "Sec-WebSocket-Version: 13\r\n\r\n",
             host, port, ws_key_b64);

    uv_check_init(uv_default_loop(), &conn->check_handle);
    conn->check_handle.data = conn;
    uv_check_start(&conn->check_handle, on_check_cb);

    return conn;
}

void
gputop_connection_send(gputop_connection_t *conn, const void *data, size_t len)
{
    struct wslay_event_msg msg;

    assert(conn != NULL);
    assert(gputop_connection_connected(conn));
    assert(data != NULL && len > 0);

    msg.opcode = WSLAY_BINARY_FRAME;
    msg.msg = data;
    msg.msg_length = len;

    wslay_event_queue_msg(conn->wslay_ctx, &msg);
    wslay_event_send(conn->wslay_ctx);
}

void
gputop_connection_close(gputop_connection_t *conn)
{
    assert(conn != NULL);

    wslay_event_queue_close(conn->wslay_ctx, WSLAY_CODE_NORMAL_CLOSURE, NULL, 0);
    wslay_event_send(conn->wslay_ctx);
}

bool
gputop_connection_connected(gputop_connection_t *conn)
{
    assert(conn != NULL);

    return conn->open;
}
