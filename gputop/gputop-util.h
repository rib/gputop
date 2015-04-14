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

#ifndef _GPUTOP_UTIL_H_
#define _GPUTOP_UTIL_H_

#include <stdlib.h>
#include <string.h>

#define unlikely(x) __builtin_expect(x, 0)

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
array_set_len(struct array *array, int len)
{
    size_t needed = len * array->elem_size;

    array->len = len;

    if (array->size >= needed)
	return;

    array->data = realloc(array->data, array->size * 1.7);
    if (!array->data)
	exit(1);
    array->size = needed;
}

static inline void
array_remove(struct array *array, int idx)
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

#endif /* _GPUTOP_UTIL_H_ */
