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

#include <stdbool.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include "gputop-web-lib.h"

void
gputop_web_console_log(const char *format, ...)
{
    va_list ap;
    char *str = NULL;

    va_start(ap, format);
    vasprintf(&str, format, ap);
    va_end(ap);

    _gputop_web_console_log(str);

    free(str);
}

void
gputop_web_console_info(const char *format, ...)
{
    va_list ap;
    char *str = NULL;

    va_start(ap, format);
    vasprintf(&str, format, ap);
    va_end(ap);

    _gputop_web_console_info(str);

    free(str);
}

void
gputop_web_console_warn(const char *format, ...)
{
    va_list ap;
    char *str = NULL;

    va_start(ap, format);
    vasprintf(&str, format, ap);
    va_end(ap);

    _gputop_web_console_warn(str);

    free(str);
}

void
gputop_web_console_error(const char *format, ...)
{
    va_list ap;
    char *str = NULL;

    va_start(ap, format);
    vasprintf(&str, format, ap);
    va_end(ap);

    _gputop_web_console_error(str);

    free(str);
}

void
gputop_web_console_assert(bool condition, const char *format, ...)
{
    va_list ap;
    char *str = NULL;

    va_start(ap, format);
    vasprintf(&str, format, ap);
    va_end(ap);

    _gputop_web_console_assert(condition, str);

    free(str);
}
