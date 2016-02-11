/*
 * String functions
 *
 * Author:
 *   Miguel de Icaza (miguel@novell.com)
 *   Aaron Bockover (abockover@novell.com)
 *
 * Copyright (C) 2006 Novell, Inc.
 * Copyright (C) 2015 Intel Corporation
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

#ifndef EMSCRIPTEN
#include <config.h>
#endif

#include <stdio.h>
#include <stdarg.h>

#include <gputop-util.h>
#include <gputop-string.h>

#define GROW_IF_NECESSARY(s, l)                                     \
    {                                                               \
        if (s->len + l >= s->allocated_len) {			    \
            s->allocated_len = (s->allocated_len + l + 16) * 1.7;   \
            s->str = xrealloc(s->str, s->allocated_len);	    \
        }                                                           \
    }

gputop_string_t *
gputop_string_new_len(const char *init, ssize_t len)
{
    gputop_string_t *ret = xmalloc(sizeof(*ret));

    if (init == NULL)
        ret->len = 0;
    else
        ret->len = len < 0 ? strlen(init) : len;
    ret->allocated_len = MAX(ret->len + 1, 16);
    ret->str = xmalloc(ret->allocated_len);
    if (init)
        memcpy(ret->str, init, ret->len);
    ret->str[ret->len] = 0;

    return ret;
}

gputop_string_t *
gputop_string_new(const char *init)
{
    return gputop_string_new_len(init, -1);
}

gputop_string_t *
gputop_string_sized_new(size_t default_size)
{
    gputop_string_t *ret = xmalloc(sizeof(*ret));

    ret->str = xmalloc(default_size);
    ret->str[0] = 0;
    ret->len = 0;
    ret->allocated_len = default_size;

    return ret;
}

char *
gputop_string_free(gputop_string_t *string, bool free_segment)
{
    char *data = string->str;

    free(string);

    if (!free_segment) {
        return data;
    }

    free(data);
    return NULL;
}

gputop_string_t *
gputop_string_assign(gputop_string_t *string, const char *val)
{
    if (string->str == val)
        return string;

    gputop_string_truncate(string, 0);
    gputop_string_append(string, val);
    return string;
}

gputop_string_t *
gputop_string_append_len(gputop_string_t *string, const char *val, ssize_t len)
{
    if (len < 0)
        len = strlen(val);

    GROW_IF_NECESSARY(string, len);
    memcpy(string->str + string->len, val, len);
    string->len += len;
    string->str[string->len] = 0;

    return string;
}

gputop_string_t *
gputop_string_append(gputop_string_t *string, const char *val)
{
    return gputop_string_append_len(string, val, -1);
}

gputop_string_t *
gputop_string_append_c(gputop_string_t *string, char c)
{
    GROW_IF_NECESSARY(string, 1);

    string->str[string->len] = c;
    string->str[string->len + 1] = 0;
    string->len++;

    return string;
}

gputop_string_t *
gputop_string_prepend(gputop_string_t *string, const char *val)
{
    ssize_t len = strlen(val);

    GROW_IF_NECESSARY(string, len);
    memmove(string->str + len, string->str, string->len + 1);
    memcpy(string->str, val, len);

    return string;
}

gputop_string_t *
gputop_string_insert(gputop_string_t *string, ssize_t pos, const char *val)
{
    ssize_t len = strlen(val);

    GROW_IF_NECESSARY(string, len);
    memmove(string->str + pos + len,
            string->str + pos,
            string->len - pos - len + 1);
    memcpy(string->str + pos, val, len);

    return string;
}

static char *
strdup_vprintf(const char *format, va_list args)
{
    int n;
    char *ret;

    n = vasprintf(&ret, format, args);
    if (n == -1)
        return NULL;

    return ret;
}

void
gputop_string_append_printf(gputop_string_t *string, const char *format, ...)
{
    char *ret;
    va_list args;

    va_start(args, format);
    ret = strdup_vprintf(format, args);
    va_end(args);
    gputop_string_append(string, ret);

    free(ret);
}

void
gputop_string_append_vprintf(gputop_string_t *string, const char *format, va_list args)
{
    char *ret;

    ret = strdup_vprintf(format, args);
    gputop_string_append(string, ret);
    free(ret);
}

void
gputop_string_printf(gputop_string_t *string, const char *format, ...)
{
    va_list args;

    free(string->str);

    va_start(args, format);
    string->str = strdup_vprintf(format, args);
    va_end(args);

    string->len = strlen(string->str);
    string->allocated_len = string->len + 1;
}

gputop_string_t *
gputop_string_truncate(gputop_string_t *string, size_t len)
{
    /* Silent return */
    if (len >= string->len)
        return string;

    string->len = len;
    string->str[len] = 0;
    return string;
}

gputop_string_t *
gputop_string_set_size(gputop_string_t *string, size_t len)
{
    GROW_IF_NECESSARY(string, len);

    string->len = len;
    string->str[len] = 0;
    return string;
}

gputop_string_t *
gputop_string_erase(gputop_string_t *string, ssize_t pos, ssize_t len)
{
    /* Silent return */
    if (pos >= string->len)
        return string;

    if (len == -1 || (pos + len) >= string->len) {
        string->str[pos] = 0;
    } else {
        memmove(string->str + pos,
                string->str + pos + len,
                string->len - (pos + len) + 1);
        string->len -= len;
    }

    return string;
}

gputop_string_t *
gputop_string_append_escaped(gputop_string_t *str, const char *_str)
{
    int i;

    for (i = 0; _str[i]; i++) {
        char c = _str[i];
        switch (c) {
        case '"':
            gputop_string_append_len(str, "\"", 2);
            break;
        case '\\':
            gputop_string_append_len(str, "\\\\", 2);
            break;
        case '\b':
            gputop_string_append_len(str, "\\b", 2);
            break;
        case '\f':
            gputop_string_append_len(str, "\\f", 2);
            break;
        case '\n':
            gputop_string_append_len(str, "\\n", 2);
            break;
        case '\r':
            gputop_string_append_len(str, "\\r", 2);
            break;
        case '\t':
            gputop_string_append_len(str, "\\t", 2);
            break;
        default:
            gputop_string_append_c(str, c);
        }
    }
    return str;
}
