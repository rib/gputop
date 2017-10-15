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

#include "gputop-network.h"

#include <libsoup/soup.h>

struct _gputop_connection_t {
    SoupSession *session;
    SoupWebsocketConnection *conn;
    GCancellable *cancel;

    gputop_on_ready_cb_t ready_cb;
    gputop_on_data_cb_t data_cb;
    gputop_on_close_cb_t close_cb;
    void *user_data;
};

static void
gputop_connection_free(gputop_connection_t *conn)
{
    g_clear_pointer(&conn->conn, g_object_unref);
    g_clear_pointer(&conn->session, g_object_unref);
    g_clear_pointer(&conn->cancel, g_object_unref);
    g_free(conn);
}

static void
on_websocket_message(SoupWebsocketConnection *self,
                     gint type, GBytes *message,
                     gpointer user_data)
{
    gputop_connection_t *conn = user_data;

    conn->data_cb(conn,
                  g_bytes_get_data(message, NULL),
                  g_bytes_get_size(message),
                  conn->user_data);
}

static void
on_websocket_closed(SoupWebsocketConnection *self, gpointer user_data)
{
    gputop_connection_t *conn = user_data;

    conn->close_cb(conn, NULL, conn->user_data);
    gputop_connection_free(conn);
}

static void
on_websocket_ready(GObject *source_object,
                   GAsyncResult *res,
                   gpointer user_data)
{
    gputop_connection_t *conn = user_data;
    GError *error = NULL;

    conn->conn = soup_session_websocket_connect_finish(conn->session, res, &error);
    if (error) {
        conn->close_cb(conn, error->message, conn->user_data);
        g_error_free(error);
        gputop_connection_free(conn);
        return;
    }

    g_signal_connect(conn->conn, "message", G_CALLBACK(on_websocket_message), conn);
    g_signal_connect(conn->conn, "closed", G_CALLBACK(on_websocket_closed), conn);

    conn->ready_cb(conn, conn->user_data);
}

gputop_connection_t *
gputop_connect(const char *host, int port,
               gputop_on_ready_cb_t ready_cb,
               gputop_on_data_cb_t data_cb,
               gputop_on_close_cb_t close_cb,
               void *user_data)
{
    gputop_connection_t *conn = g_new0(gputop_connection_t, 1);

    conn->ready_cb = ready_cb;
    conn->data_cb = data_cb;
    conn->close_cb = close_cb;

    gchar *url = g_strdup_printf("ws://%s:%u/gputop/", host, port);
    SoupMessage *msg = soup_message_new("GET", url);
    g_free(url);

    conn->session = soup_session_new();
    g_object_set(conn->session, "proxy-resolver", NULL, NULL);
    conn->cancel = g_cancellable_new();
    soup_session_websocket_connect_async(conn->session, msg, NULL, NULL,
                                         conn->cancel, on_websocket_ready, conn);

    return conn;
}

void
gputop_connection_send(gputop_connection_t *conn, const void *data, size_t len)
{
    g_return_if_fail(conn != NULL);
    g_return_if_fail(conn->conn != NULL);
    g_return_if_fail(data != NULL && len != 0);

    soup_websocket_connection_send_binary(conn->conn, data, len);
}

void
gputop_connection_close(gputop_connection_t *conn)
{
    g_return_if_fail(conn != NULL);

    if (conn->conn)
        soup_websocket_connection_close(conn->conn, 0, "");
    else
        g_cancellable_cancel(conn->cancel);
}

bool
gputop_connection_connected(gputop_connection_t *conn)
{
    g_return_val_if_fail(conn != NULL, false);

    if (!conn->conn)
        return false;
    return true;
}
