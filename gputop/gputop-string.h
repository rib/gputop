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

#pragma once

#include <stdbool.h>

typedef struct {
    char *str;
    size_t len;
    size_t allocated_len;
} gputop_string_t;

gputop_string_t *gputop_string_new(const char *init);
gputop_string_t *gputop_string_new_len(const char *init, ssize_t len);
gputop_string_t *gputop_string_sized_new(size_t default_size);
char *gputop_string_free(gputop_string_t *string, bool free_segment);
gputop_string_t *gputop_string_assign(gputop_string_t *string, const char *val);
gputop_string_t *gputop_string_append(gputop_string_t *string, const char *val);
void gputop_string_printf(gputop_string_t *string, const char *format, ...);
void gputop_string_append_printf(gputop_string_t *string, const char *format, ...);
void gputop_string_append_vprintf(gputop_string_t *string, const char *format, va_list args);
gputop_string_t *gputop_string_append_c(gputop_string_t *string, char c);
gputop_string_t *gputop_string_append(gputop_string_t *string, const char *val);
gputop_string_t *gputop_string_append_len(gputop_string_t *string, const char *val, ssize_t len);
gputop_string_t *gputop_string_truncate(gputop_string_t *string, size_t len);
gputop_string_t *gputop_string_prepend(gputop_string_t *string, const char *val);
gputop_string_t *gputop_string_insert(gputop_string_t *string, ssize_t pos, const char *val);
gputop_string_t *gputop_string_set_size(gputop_string_t *string, size_t len);
gputop_string_t *gputop_string_erase(gputop_string_t *string, ssize_t pos, ssize_t len);
gputop_string_t *gputop_string_append_escaped(gputop_string_t *str, const char *_str);
#define gputop_string_sprintfa gputop_string_append_printf

