/*
 * GPU Top
 *
 * Copyright (C) 2018 Intel Corporation
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

#include "gputop-oa-metrics.h"

#include <string.h>

#include "util/hash_table.h"
#include "util/ralloc.h"

static struct gputop_counter_group *
gputop_counter_group_new(struct gputop_gen *gen,
                         struct gputop_counter_group *parent,
                         const char *name)
{
    struct gputop_counter_group *group = ralloc(gen, struct gputop_counter_group);

    group->name = ralloc_strdup(group, name);

    list_inithead(&group->counters);
    list_inithead(&group->groups);

    if (parent)
        list_addtail(&group->link, &parent->groups);
    else
        list_inithead(&group->link);

    return group;
}

struct gputop_gen *
gputop_gen_new(void)
{
    struct gputop_gen *gen = ralloc(NULL, struct gputop_gen);

    gen->root_group = gputop_counter_group_new(gen, NULL, "");

    list_inithead(&gen->metric_sets);
    gen->metric_sets_map = _mesa_hash_table_create(gen,
                                                   _mesa_hash_string,
                                                   _mesa_key_string_equal);

    return gen;
}

void
gputop_gen_add_counter(struct gputop_gen *gen,
                       struct gputop_metric_set_counter *counter,
                       const char *group_path)
{
    char group_name[128];
    struct gputop_counter_group *group = gen->root_group;
    const char *name = group_path, *name_end;

    while ((name_end = strstr(name, "/")) != NULL) {
        memset(group_name, 0, sizeof(group_name));
        memcpy(group_name, name, name_end - name);

        struct gputop_counter_group *child_group = NULL;
        list_for_each_entry(struct gputop_counter_group, iter_group,
                            &group->groups, link) {
            if (!strcmp(iter_group->name, group_name)) {
                child_group = iter_group;
                break;
            }
        }

        if (!child_group)
            child_group = gputop_counter_group_new(gen, group, group_name);

        name = name_end + 1;
        group = child_group;
    }

    struct gputop_counter_group *child_group = NULL;
    list_for_each_entry(struct gputop_counter_group, iter_group,
                        &group->groups, link) {
        if (!strcmp(iter_group->name, group_name)) {
            child_group = iter_group;
            break;
        }
    }

    if (!child_group)
        child_group = gputop_counter_group_new(gen, group, group_name);

    list_addtail(&counter->group_link, &child_group->counters);
}

void
gputop_gen_add_metric_set(struct gputop_gen *gen,
                          struct gputop_metric_set *metric_set)
{
    list_addtail(&metric_set->link, &gen->metric_sets);
    _mesa_hash_table_insert(gen->metric_sets_map,
                            metric_set->hw_config_guid, metric_set);
}
