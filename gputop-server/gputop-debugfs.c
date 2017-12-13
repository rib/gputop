/*
 * GPU Top
 *
 * Copyright (C) 2016 Intel Corporation
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

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mount.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>

#include "gputop-util.h"
#include "gputop-sysutil.h"

static const char *debugfs_path = "/sys/kernel/debug";
static bool debugfs_mounted;

static bool debugfs_mount(void)
{
    struct stat st;

    if (debugfs_mounted)
        return true;

    if (stat("/sys/kernel/debug/dri", &st) == 0) {
        debugfs_mounted = true;
        return true;
    }

    if (mount("debug", debugfs_path, "debugfs", 0, 0) == 0) {
        debugfs_mounted = true;
        return true;
    }

    fprintf(stderr, "Failed to mount debugfs\n");
    return false;
}

int gputop_debugfs_open(const char *filename, int mode)
{
    char buf[1024];

    if (!debugfs_mount())
        return -1;

    snprintf(buf, sizeof(buf), "%s/%s", debugfs_path, filename);
    return open(buf, mode);
}

FILE *gputop_debugfs_fopen(const char *filename,
                           const char *mode)
{
    char buf[1024];

    if (!debugfs_mount())
        return NULL;

    snprintf(buf, sizeof(buf), "%s/%s", debugfs_path, filename);
    return fopen(buf, mode);
}

/* Returns entire file contents as a NUL terminated string
 * returned len doesnt count the added NUL terminator.
 *
 * NB: free() when done.
 */
void *gputop_debugfs_read(const char *filename, int *len)
{
    FILE *file;
    size_t size = 0;
    char *contents = NULL;
    int r = 0;
    int offset = 0;

    file = gputop_debugfs_fopen(filename, "r");
    if (!file) {
        fprintf(stderr, "Failed to open %s: %m\n", filename);
        return NULL;
    }

    do {
        size += 4096;
        contents = xrealloc(contents, size);

        r = fread(contents + offset, 1, 4096, file);
        offset += r;
    } while (r == 4096);

    contents[offset] = '\0';

    fclose(file);

    return contents;
}

uint64_t gputop_debugfs_read_uint64(const char *filename)
{
    char buf[1024];
    uint64_t value;

    if (!debugfs_mount()) {
        return 0;
    }

    snprintf(buf, sizeof(buf), "%s/%s", debugfs_path, filename);

    if (!gputop_read_file_uint64(buf, &value))
	return 0;

    return value;
}

char **
gputop_debugfs_get_tracepoint_names(void)
{
    FILE *fp;
    char *line = NULL;
    size_t line_len = 0;
    ssize_t read;
    int n = 0;
    int i = 0;
    char **vec;

    fp = gputop_debugfs_fopen("tracing/available_events", "r");
    if (!fp)
        return NULL;

    for (n = 0; (read = getline(&line, &line_len, fp)) != -1; n++)
        ;

    if (n == 0)
        return NULL;

    vec = xmalloc0(sizeof(char *) * (n + 1));

    fseek(fp, 0, SEEK_SET);
    while ((read = getline(&line, &line_len, fp)) != -1) {
        int len = strlen(line);
        char *delim;

        if (line[len - 1] == '\n')
            line[len - 1] = '\0';

        delim = strstr(line, ":");
        if (!delim) {
            fprintf(stderr, "spurious line format in available_events '%s'\n", line);
            continue;
        }
        *delim = '/';
        vec[i++] = strdup(line);
    }

    vec[i++] = NULL;

    fclose(fp);
    free(line);

    return vec;
}

void
gputop_debugfs_free_tracepoint_names(char **names)
{
    if (!names)
        return;

    for (int i = 0; names[i]; i++)
        free(names[i]);

    free(names);
}
