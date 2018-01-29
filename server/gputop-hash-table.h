/*
 * Copyright © 2009,2012 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 * Authors:
 *    Eric Anholt <eric@anholt.net>
 *
 */

#ifndef _GPUTOP_HASH_TABLE_H
#define _GPUTOP_HASH_TABLE_H

#include <stdlib.h>
#include <inttypes.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

struct gputop_hash_entry {
    uint32_t hash;
    const void *key;
    void *data;
};

struct gputop_hash_table {
    struct gputop_hash_entry *table;
    uint32_t (*key_hash_function)(const void *key);
    bool (*key_equals_function)(const void *a, const void *b);
    const void *deleted_key;
    uint32_t size;
    uint32_t rehash;
    uint32_t max_entries;
    uint32_t size_index;
    uint32_t entries;
    uint32_t deleted_entries;
};

struct gputop_hash_table *
gputop_hash_table_create(uint32_t (*key_hash_function)(const void *key),
                         bool (*key_equals_function)(const void *a,
                                                     const void *b));
void gputop_hash_table_destroy(struct gputop_hash_table *ht,
                               void (*delete_function)
                               (struct gputop_hash_entry *entry));
void gputop_hash_table_set_deleted_key(struct gputop_hash_table *ht,
                                       const void *deleted_key);

struct gputop_hash_entry *
gputop_hash_table_insert(struct gputop_hash_table *ht, const void *key,
                         void *data);
struct gputop_hash_entry *
gputop_hash_table_insert_pre_hashed(struct gputop_hash_table *ht,
                                    uint32_t gputop_hash, const void *key,
                                    void *data);
struct gputop_hash_entry *
gputop_hash_table_search(struct gputop_hash_table *ht, const void *key);
struct gputop_hash_entry *
gputop_hash_table_search_pre_hashed(struct gputop_hash_table *ht,
                                    uint32_t gputop_hash, const void *key);
void gputop_hash_table_remove(struct gputop_hash_table *ht,
                              struct gputop_hash_entry *entry);

struct gputop_hash_entry *
gputop_hash_table_next_entry(struct gputop_hash_table *ht,
                             struct gputop_hash_entry *entry);
struct gputop_hash_entry *
gputop_hash_table_random_entry(struct gputop_hash_table *ht,
                               bool (*predicate)
                               (struct gputop_hash_entry *entry));

uint32_t gputop_hash_data(const void *data, size_t size);
uint32_t gputop_hash_string(const char *key);
bool gputop_key_string_equal(const void *a, const void *b);
bool gputop_key_pointer_equal(const void *a, const void *b);

static inline uint32_t gputop_key_hash_string(const void *key)
{
     return gputop_hash_string((const char *)key);
}

static inline uint32_t gputop_hash_pointer(const void *pointer)
{
    return gputop_hash_data(&pointer, sizeof(pointer));
}

static const uint32_t gputop_fnv32_1a_offset_bias = 2166136261u;

static inline uint32_t
gputop_fnv32_1a_accumulate_block(uint32_t gputop_hash, const void *data,
                                 size_t size)
{
    const uint8_t *bytes = (const uint8_t *)data;

    while (size-- != 0) {
        gputop_hash ^= *bytes;
        gputop_hash = gputop_hash * 0x01000193;
        bytes++;
    }

    return gputop_hash;
}

#define gputop_fnv32_1a_accumulate(hash, expr) \
    gputop_fnv32_1a_accumulate_block(hash, &(expr), sizeof(expr))

/**
 * This foreach function is safe against deletion (which just replaces
 * an entry's data with the deleted marker), but not against insertion
 * (which may rehash the table, making entry a dangling pointer).
 */
#define gputop_hash_table_foreach(ht, entry)                   \
    for (entry = gputop_hash_table_next_entry(ht, NULL);  \
        entry != NULL;                                  \
        entry = gputop_hash_table_next_entry(ht, entry))

#ifdef __cplusplus
} /* extern C */
#endif

#endif /* _HASH_TABLE_H */
