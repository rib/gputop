/*
 * GPU Top
 *
 * Copyright (C) 2014 Intel Corporation
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

#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <strings.h>
#include <fcntl.h>
#include <unistd.h>

#define MAYBE_UNUSED __attribute__((unused))

#define unlikely(x) __builtin_expect(x, 0)

#define MIN(x, y) (((x) < (y)) ? (x) : (y))
#define MAX(a, b) (((a) > (b)) ? (a) : (b))

#define ARRAY_SIZE(x) (sizeof(x) / sizeof(*(x)))

static inline void *
xmalloc(size_t size)
{
    void *ret = malloc(size);
    if (!ret)
        exit(1);
    return ret;
}

static inline void *
xmalloc0(size_t size)
{
    void *ret = malloc(size);
    if (!ret)
        exit(1);
    memset(ret, 0, size);
    return ret;
}

static inline void *
xrealloc(void *ptr, size_t size)
{
    void *ret = realloc(ptr, size);
    if (!ret)
        exit(1);
    return ret;
}

struct array
{
    size_t elem_size;
    int len;

    size_t size;
    union {
        void *data;
        uint8_t *bytes;
    };
};

static inline struct array *
array_new(size_t elem_size, int alloc_len)
{
    struct array *array = xmalloc(sizeof(struct array));

    array->elem_size = elem_size;
    array->len = 0;
    array->size = elem_size * alloc_len;
    array->data = xmalloc(array->size);

    return array;
}

static inline void
array_free(struct array *array)
{
    free(array->data);
    free(array);
}

static inline void
array_set_len(struct array *array, int len)
{
    size_t needed = len * array->elem_size;

    array->len = len;

    if (array->size >= needed)
        return;

    array->size = MAX(needed, array->size * 1.7);
    array->data = xrealloc(array->data, array->size);
}

static inline void
array_remove_fast(struct array *array, int idx)
{
    uint8_t *elem;
    uint8_t *last;

    array->len--;
    if (idx == array->len)
        return;

    elem = array->bytes + idx * array->elem_size;
    last = array->bytes + array->len * array->elem_size;
    memcpy(elem, last, array->elem_size);
}

static inline void
array_append(struct array *array, void *data)
{
    void *dst;

    array_set_len(array, array->len + 1);

    dst = array->bytes + array->elem_size * (array->len - 1);
    memcpy(dst, data, array->elem_size);
}

#define array_value_at(ARRAY, TYPE, IDX) *(((TYPE *)(ARRAY)->data) + IDX)
