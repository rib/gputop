/*
 * Copyright (C) 2015 Intel Corporation.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

void _gputop_web_console_log(const char *message);
void gputop_web_console_log(const char *format, ...);

void _gputop_web_console_info(const char *message);
void gputop_web_console_info(const char *format, ...);

void _gputop_web_console_warn(const char *message);
void gputop_web_console_warn(const char *format, ...);

void _gputop_web_console_error(const char *message);
void gputop_web_console_error(const char *format, ...);

void _gputop_web_console_assert(bool condition, const char *message) __attribute__((noreturn));
void gputop_web_console_assert(bool condition, const char *format, ...) __attribute__((noreturn));

void _gputop_web_console_trace(void);

void _gputop_web_worker_post(const char *message);

const char *_gputop_web_get_location_host(void);

int _gputop_web_socket_new(const char *url, const char *protocols);
typedef void (*gputop_handle_func_t)(int id, void *user_data);
void _gputop_web_socket_set_onopen(int id, gputop_handle_func_t onopen, void *user_data);
void _gputop_web_socket_set_onerror(int id, gputop_handle_func_t onerror, void *user_data);
void _gputop_web_socket_set_onclose(int id, gputop_handle_func_t onclose, void *user_data);
typedef void (*gputop_message_func_t)(int id, uint8_t *buf, int len, void *user_data);
void _gputop_web_socket_set_onmessage(int id, gputop_message_func_t onerror, void *user_data);
void _gputop_web_socket_destroy(int id);
void _gputop_web_socket_post(int id, uint8_t *data, int len);
