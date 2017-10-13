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

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>

#include <sys/types.h>
#include <sys/stat.h>

#include "gputop-sysutil.h"

bool
gputop_get_bool_env(const char *var)
{
    char *val = getenv(var);

    if (!val)
        return false;

    if (strcmp(val, "1") == 0 ||
        strcasecmp(val, "yes") == 0 ||
        strcasecmp(val, "on") == 0 ||
        strcasecmp(val, "true") == 0)
        return true;
    else if (strcmp(val, "0") == 0 ||
             strcasecmp(val, "no") == 0 ||
             strcasecmp(val, "off") == 0 ||
             strcasecmp(val, "false") == 0)
        return false;

    fprintf(stderr, "unrecognised value for boolean variable %s\n", var);
    return false;
}

uint64_t
gputop_get_time(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000000000 + (uint64_t)t.tv_nsec;
}

bool
gputop_read_file(const char *filename, void *buf, int max)
{
    int fd;
    int n;

    memset(buf, 0, max);

    fd = open(filename, 0);
    if (fd < 0)
        return false;
    n = read(fd, buf, max - 1);
    close(fd);
    if (n < 0)
        return false;

    return true;
}
