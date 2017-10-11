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

#include <config.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "gputop-util.h"
#include "gputop-log.h"

pthread_once_t gputop_log_init_once = PTHREAD_ONCE_INIT;
pthread_rwlock_t gputop_log_lock = PTHREAD_RWLOCK_INITIALIZER;

int gputop_log_len;
struct list_head gputop_log_entries;

void
gputop_log_init(void)
{
    list_inithead(&gputop_log_entries);
}

void
gputop_log(int level, const char *message, int len)
{
    struct gputop_log_entry *entry;

    pthread_once(&gputop_log_init_once, gputop_log_init);

    /* XXX: HACK */
    printf("%s", message);

    if (len < 0)
        len = strlen(message);

    pthread_rwlock_wrlock(&gputop_log_lock);

    if (gputop_log_len > 10000) {
        entry = list_last_entry(&gputop_log_entries,
                                struct gputop_log_entry, link);
        list_del(&entry->link);
        free(entry->msg);
        gputop_log_len--;
    } else
        entry = xmalloc(sizeof(*entry));

    entry->level = level;
    entry->msg = strndup(message, len);

    list_addtail(&entry->link, &gputop_log_entries);
    gputop_log_len++;

    pthread_rwlock_unlock(&gputop_log_lock);
}

Gputop__Log *
gputop_get_pb_log(void)
{
    Gputop__Log *log = NULL;
    int i = 0;

    pthread_once(&gputop_log_init_once, gputop_log_init);

    if (!gputop_log_len) {
        return NULL;
    }

    pthread_rwlock_rdlock(&gputop_log_lock);

    log = xmalloc(sizeof(Gputop__Log));
    gputop__log__init(log);
    log->n_entries = gputop_log_len;
    log->entries = xmalloc(gputop_log_len * sizeof(void *));

    list_for_each_entry_safe(struct gputop_log_entry, entry,
                             &gputop_log_entries, link) {
        Gputop__LogEntry *pb_entry = xmalloc(sizeof(Gputop__LogEntry));

        gputop__log_entry__init(pb_entry);
        pb_entry->log_level = entry->level;
        pb_entry->log_message = entry->msg; /* steal the string */
        log->entries[i] = pb_entry;

        i++;

        list_del(&entry->link);
    }
    gputop_log_len = 0;

    pthread_rwlock_unlock(&gputop_log_lock);

    return log;
}

void
gputop_pb_log_free(Gputop__Log *log)
{
    int i;

    if (log->n_entries) {
        for (i = 0; i < log->n_entries; i++) {
            Gputop__LogEntry *entry = log->entries[i];
            free(entry->log_message);
            free(entry);
        }

        free(log->entries);
    }

    free(log);
}
