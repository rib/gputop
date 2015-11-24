/*
 * Copyright © 2008-2011 Kristian Høgsberg
 * Copyright © 2011, 2012, 2015 Intel Corporation
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that copyright
 * notice and this permission notice appear in supporting documentation, and
 * that the name of the copyright holders not be used in advertising or
 * publicity pertaining to distribution of the software without specific,
 * written prior permission.  The copyright holders make no representations
 * about the suitability of this software for any purpose.  It is provided "as
 * is" without express or implied warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
 * OF THIS SOFTWARE.
 */

/* This list implementation is based on the Wayland source code */

#ifndef EMSCRIPTEN
#include <config.h>
#endif

#include <stdlib.h>
#include <string.h>

#include "gputop-list.h"

void
gputop_list_init(gputop_list_t *list)
{
    list->prev = list;
    list->next = list;
}

void
gputop_list_insert(gputop_list_t *list, gputop_list_t *elm)
{
    elm->prev = list;
    elm->next = list->next;
    list->next = elm;
    elm->next->prev = elm;
}

void
gputop_list_remove(gputop_list_t *elm)
{
    elm->prev->next = elm->next;
    elm->next->prev = elm->prev;
    elm->next = NULL;
    elm->prev = NULL;
}

int
gputop_list_length(gputop_list_t *list)
{
    gputop_list_t *e;
    int count;

    count = 0;
    e = list->next;
    while (e != list) {
        e = e->next;
        count++;
    }

    return count;
}

int
gputop_list_empty(gputop_list_t *list)
{
    return list->next == list;
}


void
gputop_list_prepend_list(gputop_list_t *list, gputop_list_t *other)
{
    if (gputop_list_empty(other))
        return;

    other->next->prev = list;
    other->prev->next = list->next;
    list->next->prev = other->prev;
    list->next = other->next;
}

void
gputop_list_append_list(gputop_list_t *list, gputop_list_t *other)
{
    if (gputop_list_empty(other))
        return;

    list->prev->next = other->next;
    other->prev->next = list;
    other->next->prev = list->prev;
    list->prev = other->prev;
}
