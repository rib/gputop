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

#include <config.h>

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>

#include "gputop-util.h"
#include "gputop-sysutil.h"
#include "gputop-log.h"
#include "gputop-cpu.h"


static pthread_once_t count_once = PTHREAD_ONCE_INIT;
static int n_cpus = 0;

static void
count_cpus(void)
{
    char buf[32];
    unsigned ignore = 0, max_cpu = 0;

    if (!gputop_read_file("/sys/devices/system/cpu/present", buf, sizeof(buf)))
        return;

    if (sscanf(buf, "%u-%u", &ignore, &max_cpu) != 2)
        return;

    n_cpus = max_cpu + 1;
}

int
gputop_cpu_count(void)
{
    if (n_cpus)
        return n_cpus;

    pthread_once(&count_once, count_cpus);

    return n_cpus;
}

bool
gputop_cpu_model(char *buf, int len)
{
    FILE *fp;
    char *line = NULL;
    size_t line_len = 0;
    ssize_t read;
    const char *key = "model name\t: ";
    int key_len = strlen(key);

    memset(buf, 0, len);

    snprintf(buf, len, "Unknown");

    fp = fopen("/proc/cpuinfo", "r");
    if (!fp)
        return false;

    while ((read = getline(&line, &line_len, fp)) != -1) {
        if (strncmp(key, line, key_len) == 0) {
            snprintf(buf, len, "%s", line + key_len);
            fclose(fp);
            free(line);
            return true;
        }
    }

    fclose(fp);
    free(line);
    return false;
}


bool
gputop_cpu_read_stats(struct cpu_stat *stats, int n_cpus)
{
    FILE *fp = fopen("/proc/stat", "r");
    char *line = NULL;
    size_t line_len = 0;
    uint64_t timestamp = gputop_get_time();
    int n_read = 0;

    if (!fp) {
        dbg("Failed to open /proc/stat\n");
        return false;
    }

    while (getline(&line, &line_len, fp) > 0) {
        struct cpu_stat st = { 0 };
        int ret = sscanf(line, "cpu%u %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu",
                         &st.cpu, &st.user, &st.nice, &st.system, &st.idle,
                         &st.iowait, &st.irq, &st.softirq,
                         &st.steal, &st.guest, &st.guest_nice);
        if (ret == 11) {
            if (st.cpu >= n_cpus) {
                dbg("cpu stats buffer too small\n");
                break;
            }
            st.timestamp = timestamp;
            stats[st.cpu] = st;
            n_read++;
        }
    }


    free(line);
    fclose(fp);

    if (n_read != n_cpus) {
        dbg("Failed to read all cpu stats\n");
        return false;
    }

    return true;
}
