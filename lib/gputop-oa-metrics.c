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
#include "gputop-gens-metrics.h"


#include <string.h>

#include "util/hash_table.h"
#include "util/ralloc.h"

struct gputop_gen *
gputop_gen_for_devinfo(const struct gen_device_info *devinfo)
{
    struct gputop_devinfo gputop_devinfo = {};

    if (devinfo->is_haswell)
        return gputop_oa_get_metrics_hsw(&gputop_devinfo);
    if (devinfo->is_broadwell)
	return gputop_oa_get_metrics_bdw(&gputop_devinfo);
    if (devinfo->is_cherryview)
	return gputop_oa_get_metrics_chv(&gputop_devinfo);
    if (devinfo->is_skylake) {
	switch (devinfo->gt) {
	case 2:
            return gputop_oa_get_metrics_sklgt2(&gputop_devinfo);
	case 3:
	    return gputop_oa_get_metrics_sklgt3(&gputop_devinfo);
	case 4:
	    return gputop_oa_get_metrics_sklgt4(&gputop_devinfo);
	default:
	    return NULL;
	}
    }
    if (devinfo->is_broxton)
	return gputop_oa_get_metrics_bxt(&gputop_devinfo);
    if (devinfo->is_kabylake) {
	switch (devinfo->gt) {
	case 2:
            return gputop_oa_get_metrics_kblgt2(&gputop_devinfo);
	case 3:
	    return gputop_oa_get_metrics_kblgt3(&gputop_devinfo);
	default:
            return NULL;
	}
    }
    if (devinfo->is_geminilake)
        return gputop_oa_get_metrics_glk(&gputop_devinfo);
    if (devinfo->is_coffeelake) {
	switch (devinfo->gt) {
	case 2:
	    return gputop_oa_get_metrics_cflgt2(&gputop_devinfo);
	case 3:
	    return gputop_oa_get_metrics_cflgt3(&gputop_devinfo);
	default:
	    return NULL;
	}
    }
    if (devinfo->is_cannonlake)
        return gputop_oa_get_metrics_cnl(&gputop_devinfo);
    if (devinfo->gen == 11)
        return gputop_oa_get_metrics_icl(&gputop_devinfo);
    if (devinfo->gen == 12)
        return gputop_oa_get_metrics_tgl(&gputop_devinfo);
    return NULL;
}

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
    const char *group_path_end = group_path + strlen(group_path);
    struct gputop_counter_group *group = gen->root_group, *child_group = NULL;
    const char *name = group_path;

    while (name < group_path_end) {
        const char *name_end = strstr(name, "/");
        char group_name[128] = { 0, };

        if (!name_end)
            name_end = group_path_end;

        memcpy(group_name, name, name_end - name);

        child_group = NULL;
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

    list_addtail(&counter->link, &child_group->counters);
}

void
gputop_gen_add_metric_set(struct gputop_gen *gen,
                          struct gputop_metric_set *metric_set)
{
    list_addtail(&metric_set->link, &gen->metric_sets);
    _mesa_hash_table_insert(gen->metric_sets_map,
                            metric_set->hw_config_guid, metric_set);
}
