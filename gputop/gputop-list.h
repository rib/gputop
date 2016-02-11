/*
 * Copyright © 2008 Kristian Høgsberg
 * Copyright © 2012,2015 Intel Corporation
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

#pragma once

#include <stddef.h>

/**
 * gputop_list_t - linked list
 *
 * The list head is of "gputop_list_t" type, and must be initialized
 * using gputop_list_init().  All entries in the list must be of the same
 * type.  The item type must have a "gputop_list_t" member. This
 * member will be initialized by gputop_list_insert(). There is no need to
 * call gputop_list_init() on the individual item. To query if the list is
 * empty in O(1), use gputop_list_empty().
 *
 * Let's call the list reference "gputop_list_t foo_list", the item type as
 * "item_t", and the item member as "gputop_list_t link". The following code
 *
 * The following code will initialize a list:
 *
 *      gputop_list_init(foo_list);
 *      gputop_list_insert(foo_list, item1);      Pushes item1 at the head
 *      gputop_list_insert(foo_list, item2);      Pushes item2 at the head
 *      gputop_list_insert(item2, item3);         Pushes item3 after item2
 *
 * The list now looks like [item2, item3, item1]
 *
 * Will iterate the list in ascending order:
 *
 *      item_t *item;
 *      gputop_list_for_each(item, foo_list, link) {
 *              Do_something_with_item(item);
 *      }
 */

typedef struct _gputop_list_t gputop_list_t;

struct _gputop_list_t {
    gputop_list_t *prev;
    gputop_list_t *next;
};

void gputop_list_init(gputop_list_t *list);
void gputop_list_insert(gputop_list_t *list, gputop_list_t *elm);
void gputop_list_remove(gputop_list_t *elm);
int gputop_list_length(gputop_list_t *list);
int gputop_list_empty(gputop_list_t *list);
void gputop_list_append_list(gputop_list_t *list, gputop_list_t *other);
void gputop_list_prepend_list(gputop_list_t *list, gputop_list_t *other);


/* This assigns to iterator first so that taking a reference to it
 * later in the second step won't be an undefined operation. It
 * assigns the value of list_node rather than 0 so that it is possible
 * have list_node be based on the previous value of iterator. In that
 * respect iterator is just used as a convenient temporary variable.
 * The compiler optimises all of this down to a single subtraction by
 * a constant */
#define gputop_list_set_iterator(list_node, iterator, member)               \
    ((iterator) = (void *)(list_node),                                      \
     (iterator) =                                                           \
         (void *)((char *)(iterator) -                                      \
                  (((char *)&(iterator)->member) - (char *)(iterator))))

#define gputop_container_of(ptr, type, member)                                   \
    (type *)((char *)(ptr) - offsetof(type, member))

#define gputop_list_first(list, type, member)                                    \
    gputop_list_empty(list) ? NULL : gputop_container_of((list)->next, type, member);

#define gputop_list_last(list, type, member)                                     \
    gputop_list_empty(list) ? NULL : gputop_container_of((list)->prev, type, member);

#define gputop_list_for_each(pos, head, member)                                  \
    for (gputop_list_set_iterator((head)->next, pos, member);                    \
         &pos->member != (head);                                                 \
         gputop_list_set_iterator(pos->member.next, pos, member))

#define gputop_list_for_each_safe(pos, tmp, head, member)                        \
    for (gputop_list_set_iterator((head)->next, pos, member),                    \
         gputop_list_set_iterator((pos)->member.next, tmp, member);              \
         &pos->member != (head);                                                 \
         pos = tmp, gputop_list_set_iterator(pos->member.next, tmp, member))

#define gputop_list_for_each_reverse(pos, head, member)                          \
    for (gputop_list_set_iterator((head)->prev, pos, member);                    \
         &pos->member != (head);                                                 \
         gputop_list_set_iterator(pos->member.prev, pos, member))

#define gputop_list_for_each_reverse_safe(pos, tmp, head, member)                \
    for (gputop_list_set_iterator((head)->prev, pos, member),                    \
         gputop_list_set_iterator((pos)->member.prev, tmp, member);              \
         &pos->member != (head);                                                 \
         pos = tmp, gputop_list_set_iterator(pos->member.prev, tmp, member))
