/*
 * GPU Top
 *
 * Copyright (C) 2017 Intel Corporation
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

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include <getopt.h>
#include <unistd.h>

#include "gputop-client-context.h"
#include "gputop-network.h"

#include <uv.h>

const struct gputop_metric_set_counter timestamp_counter = {
    .metric_set = NULL,
    .name = "Timestamp",
    .symbol_name = "Timestamp",
    .desc = "OA unit timestamp",
    .type = GPUTOP_PERFQUERY_COUNTER_TIMESTAMP,
    .data_type = GPUTOP_PERFQUERY_COUNTER_DATA_UINT64,
    .units = GPUTOP_PERFQUERY_COUNTER_UNITS_NS,
};

static struct {
    struct gputop_client_context ctx;
    const char *metric_name;

    struct {
        char *symbol_name;
        const struct gputop_metric_set_counter *counter;
        int width;
    } *metric_columns;
    int n_metric_columns;
    bool all_columns;
    bool human_units;
    bool print_headers;
    bool print_maximums;
    FILE *wrapper_output;

    int n_accumulations;
    struct gputop_accumulated_samples *last_samples;

    int child_process_pid;
    char **child_process_args;
    const char *child_process_output_file;
    uint32_t n_child_process_args;
    uv_timer_t child_process_timer_handle;
    bool child_exited;
    uint32_t max_idle_child_time_ms;

    struct hash_table *process_ids;
} context;

static void comment(const char *format, ...)
{
    va_list ap;

    va_start(ap, format);
    vfprintf(stderr, format, ap);
    va_end(ap);
}

static void output(const char *format, ...)
{
    va_list ap;

    va_start(ap, format);
    vfprintf(context.wrapper_output, format, ap);
    va_end(ap);
}

void gputop_cr_console_log(const char *format, ...)
{
    va_list ap;

    va_start(ap, format);
    vfprintf(stderr, format, ap);
    va_end(ap);
    fprintf(stderr, "\n");
}

static int unit_to_width(gputop_counter_units_t unit)
{
    switch (unit) {
    case GPUTOP_PERFQUERY_COUNTER_UNITS_BYTES:   return 8;
    case GPUTOP_PERFQUERY_COUNTER_UNITS_HZ:      return 8;
    case GPUTOP_PERFQUERY_COUNTER_UNITS_NS:      return 12;
    case GPUTOP_PERFQUERY_COUNTER_UNITS_US:      return 8;
    case GPUTOP_PERFQUERY_COUNTER_UNITS_PIXELS:
    case GPUTOP_PERFQUERY_COUNTER_UNITS_TEXELS:  return 8 + 2;
    case GPUTOP_PERFQUERY_COUNTER_UNITS_THREADS: return 8;
    case GPUTOP_PERFQUERY_COUNTER_UNITS_PERCENT: return 6;


    case GPUTOP_PERFQUERY_COUNTER_UNITS_MESSAGES:
    case GPUTOP_PERFQUERY_COUNTER_UNITS_NUMBER:
    case GPUTOP_PERFQUERY_COUNTER_UNITS_CYCLES:
    case GPUTOP_PERFQUERY_COUNTER_UNITS_EVENTS:
    case GPUTOP_PERFQUERY_COUNTER_UNITS_UTILIZATION:
    case GPUTOP_PERFQUERY_COUNTER_UNITS_EU_SENDS_TO_L3_CACHE_LINES:
    case GPUTOP_PERFQUERY_COUNTER_UNITS_EU_ATOMIC_REQUESTS_TO_L3_CACHE_LINES:
    case GPUTOP_PERFQUERY_COUNTER_UNITS_EU_REQUESTS_TO_L3_CACHE_LINES:
    case GPUTOP_PERFQUERY_COUNTER_UNITS_EU_BYTES_PER_L3_CACHE_LINE:  return 8 + 2;

    default:
        assert(!"Missing case");
        return 0;
    }
}

static const char *unit_to_string(gputop_counter_units_t unit)
{
    switch (unit) {
    case GPUTOP_PERFQUERY_COUNTER_UNITS_BYTES:   return "B";
    case GPUTOP_PERFQUERY_COUNTER_UNITS_HZ:      return "Hz";
    case GPUTOP_PERFQUERY_COUNTER_UNITS_NS:      return "ns";
    case GPUTOP_PERFQUERY_COUNTER_UNITS_US:      return "us";
    case GPUTOP_PERFQUERY_COUNTER_UNITS_PIXELS:  return "pixels";
    case GPUTOP_PERFQUERY_COUNTER_UNITS_TEXELS:  return "texels";
    case GPUTOP_PERFQUERY_COUNTER_UNITS_THREADS: return "threads";
    case GPUTOP_PERFQUERY_COUNTER_UNITS_PERCENT: return "%";


    case GPUTOP_PERFQUERY_COUNTER_UNITS_MESSAGES: return "messages/s";
    case GPUTOP_PERFQUERY_COUNTER_UNITS_NUMBER:   return "/s";
    case GPUTOP_PERFQUERY_COUNTER_UNITS_CYCLES:   return "cycles/s";
    case GPUTOP_PERFQUERY_COUNTER_UNITS_EVENTS:   return "events/s";
    case GPUTOP_PERFQUERY_COUNTER_UNITS_UTILIZATION: return "utilization";
    case GPUTOP_PERFQUERY_COUNTER_UNITS_EU_SENDS_TO_L3_CACHE_LINES: return "sends-to-L3-CL";
    case GPUTOP_PERFQUERY_COUNTER_UNITS_EU_ATOMIC_REQUESTS_TO_L3_CACHE_LINES: return "atomics-to-L3-CL";
    case GPUTOP_PERFQUERY_COUNTER_UNITS_EU_REQUESTS_TO_L3_CACHE_LINES: return "requests-to-L3-CL";
    case GPUTOP_PERFQUERY_COUNTER_UNITS_EU_BYTES_PER_L3_CACHE_LINE:  return "bytes-per-L3-CL";

    default:
        assert(!"Missing case");
        return 0;
    }
}

static void quit(void)
{
    gputop_client_context_stop_sampling(&context.ctx);
    gputop_connection_close(context.ctx.connection);
}

static void start_child_process(void)
{
    int i, fd_out;

    if (context.ctx.devinfo.gen < 8) {
        comment("Process monitoring not supported in Haswell\n");
        quit();
        return;
    }

    context.process_ids = _mesa_hash_table_create(NULL,
                                                  _mesa_hash_pointer,
                                                  _mesa_key_pointer_equal);
    context.child_process_pid = fork();
    switch (context.child_process_pid) {
    case 0:
        close(1);
        fd_out = open(context.child_process_output_file, O_CREAT | O_CLOEXEC,
                      S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
        if (fd_out == -1) {
            comment("Cannot create output file '%s': %s\n",
                    context.child_process_output_file, strerror(errno));
            exit(EXIT_FAILURE);
        }
        if (dup2(fd_out, 1) == -1) {
            comment("Error redirecting output stream: %s\n", strerror(errno));
            exit(EXIT_FAILURE);
        }
        execvp(context.child_process_args[0], context.child_process_args);
        break;
    case -1:
        comment("Cannot start child process: %s\n", strerror(errno));
        quit();
        break;
    default:
        comment("Monitoring pid=%u: ", context.child_process_pid);
        for (i = 0; i < context.n_child_process_args; i++)
            comment("%s ", context.child_process_args[i]);
        comment("\n");
        break;
    }
}

static void on_ctrl_c(uv_signal_t* handle, int signum)
{
    quit();
}

static void on_child_timer(uv_timer_t* handle)
{
    uv_timer_stop(&context.child_process_timer_handle);
    quit();
}

static void on_child_process_exit(uv_signal_t* handle, int signum)
{
    /* Given it another aggregation period and quit. */
    comment("Child exited.\n");
    context.child_exited = true;
    uv_timer_init(uv_default_loop(), &context.child_process_timer_handle);
    uv_timer_start(&context.child_process_timer_handle, on_child_timer,
                   MAX2(context.max_idle_child_time_ms,
                        context.ctx.oa_aggregation_period_ns / 1000000ULL),
                   0);
}

static void print_system_info(void)
{
    const struct gputop_devinfo *devinfo = &context.ctx.devinfo;
    char temp[80];

    comment("System info:\n");
    comment("\tKernel release: %s\n", context.ctx.features->features->kernel_release);
    comment("\tKernel build: %s\n", context.ctx.features->features->kernel_build);

    comment("CPU info:\n");
    comment("\tCPU model: %s\n", context.ctx.features->features->cpu_model);
    comment("\tCPU cores: %i\n", context.ctx.features->features->n_cpus);

    comment("GPU info:\n");
    comment("\tGT name: %s (Gen %u, PCI 0x%x)\n",
            devinfo->prettyname, devinfo->gen, devinfo->devid);
    comment("\tTopology: %llu threads, %llu EUs, %llu slices, %llu subslices\n",
            devinfo->eu_threads_count, devinfo->n_eus,
            devinfo->n_eu_slices, devinfo->n_eu_sub_slices);
    comment("\tGT frequency range: %.1fMHz / %.1fMHz\n",
            (double) devinfo->gt_min_freq / 1000000.0f,
            (double) devinfo->gt_max_freq / 1000000.0f);
    comment("\tCS timestamp frequency: %lu Hz / %.2f ns\n",
            devinfo->timestamp_frequency,
            1000000000.0f / devinfo->timestamp_frequency);

    comment("OA info:\n");
    comment("\tOA Hardware Sampling Exponent: %u\n",
            gputop_time_to_oa_exponent(&context.ctx.devinfo, context.ctx.oa_aggregation_period_ns));
    gputop_client_pretty_print_value(GPUTOP_PERFQUERY_COUNTER_UNITS_NS,
                                     gputop_oa_exponent_to_period_ns(&context.ctx.devinfo,
                                                                     gputop_time_to_oa_exponent(&context.ctx.devinfo,
                                                                                                context.ctx.oa_aggregation_period_ns)),
                                     temp, sizeof(temp));
    comment("\tOA Hardware Period: %u ns / %s\n",
            gputop_oa_exponent_to_period_ns(&context.ctx.devinfo,
                                            gputop_time_to_oa_exponent(&context.ctx.devinfo,
                                                                       context.ctx.oa_aggregation_period_ns)),
            temp);
}

static void print_metrics(void)
{
    int max_name_length = 0;
    list_for_each_entry(struct gputop_metric_set, metric_set,
                        &context.ctx.gen_metrics->metric_sets, link)
        max_name_length = MAX2(max_name_length, strlen(metric_set->symbol_name));

    comment("List of metric sets selectable with -m/--metrics=...\n");
    list_for_each_entry(struct gputop_metric_set, metric_set,
                        &context.ctx.gen_metrics->metric_sets, link) {
        comment("\t%s:%*s %s hw-config-guid=%s\n",
                metric_set->symbol_name,
                max_name_length - strlen(metric_set->symbol_name), "",
                metric_set->name, metric_set->hw_config_guid);
    }
}


static void print_metric_counter(struct gputop_client_context *ctx,
                                 const struct gputop_metric_set *metric_set)
{
    int i, max_symbol_length = 0;
    comment("all: Timestamp");
    for (i = 0; i < metric_set->n_counters; i++) {
        comment(",%s", metric_set->counters[i].symbol_name);
        max_symbol_length = MAX2(strlen(metric_set->counters[i].symbol_name),
                                 max_symbol_length);
    }

    comment("\n\n");
    comment("Detailed:\n");
    for (i = 0; i < metric_set->n_counters; i++) {
        char pretty_max_value[80];
        gputop_client_context_pretty_print_max(ctx,
                                               &metric_set->counters[i],
                                               1000000000ULL,
                                               pretty_max_value, sizeof(pretty_max_value));

        comment("%s:%*s %s (max=%s)\n",
                metric_set->counters[i].symbol_name,
                max_symbol_length - strlen(metric_set->counters[i].symbol_name), "",
                metric_set->counters[i].desc,
                pretty_max_value);
    }
}

static void print_metric_column_names(void)
{
    int i;
    for (i = 0; i < context.n_metric_columns; i++) {
        output("%*s%s%s",
               context.metric_columns[i].width -
               strlen(context.metric_columns[i].counter->symbol_name), "",
               context.metric_columns[i].counter->symbol_name,
               (i == (context.n_metric_columns - 1)) ? "" : ",");
    }
    output("\n");
    for (i = 0; i < context.n_metric_columns; i++) {
        const char *units = unit_to_string(context.metric_columns[i].counter->units);
        output("%*s(%s)%s", context.metric_columns[i].width - strlen(units) - 2, "", units,
               (i == (context.n_metric_columns - 1)) ? "" : ",");
    }
    output("\n");
    if (context.print_maximums) {
        for (i = 0; i < context.n_metric_columns; i++) {
            char pretty_max_value[80];

            if (context.human_units) {
                gputop_client_context_pretty_print_max(&context.ctx,
                                                       context.metric_columns[i].counter,
                                                       context.ctx.oa_aggregation_period_ns,
                                                       pretty_max_value, sizeof(pretty_max_value));
            } else {
                snprintf(pretty_max_value, sizeof(pretty_max_value), "%f",
                         gputop_client_context_max_value(&context.ctx,
                                                         context.metric_columns[i].counter,
                                                         1000000000ULL));
            }

            output("%*s%s%s",
                   context.metric_columns[i].width - strlen(pretty_max_value), "",
                   pretty_max_value,
                   (i == (context.n_metric_columns - 1)) ? "" : ",");
        }
        output("\n\n");
    }
#if 0
    /* Debugging column sizes */
    for (i = 0; i < context.n_metric_columns; i++) {
        char num[10];
        snprintf(num, sizeof(num), "(max=%u)", context.metric_columns[i].width);
        output("%*s%s%s", context.metric_columns[i].width - strlen(num), "", num,
               (i == (context.n_metric_columns - 1)) ? "" : ",");
    }
    output("\n");
#endif
}

static bool pid_is_child_of(uint32_t parent, uint32_t child)
{
    static char path[80];
    snprintf(path, sizeof(path), "/proc/%u/status", child);


    FILE *f = fopen(path, "r");
    char *line = NULL;
    size_t n = 0;
    bool is_child = false;

    while (getline(&line, &n, f) > 0) {
        size_t len = strlen("PPid:");

        if (!strncmp(line, "PPid:", len)) {
            uint32_t ppid = atoi(&line[len]);

            if (ppid == 0)
                is_child = false;
            else if (ppid == parent)
                is_child = true;
            else
                is_child = pid_is_child_of(parent, ppid);
            break;
        }
    }

    free(line);

    return is_child;
}

static bool match_process(struct gputop_hw_context *hw_context)
{
    if (hw_context == NULL) {
        if (context.child_process_pid == 0)
            return true;
        return false;
    }

    if (!hw_context->process)
        return false;

    if (hw_context->process->pid == context.child_process_pid)
        return true;

    struct hash_entry *entry =
        _mesa_hash_table_search(context.process_ids,
                                (void *) (uintptr_t) hw_context->process->pid);
    if (!entry) {
        bool is_child = pid_is_child_of(context.child_process_pid,
                                        hw_context->process->pid);

        _mesa_hash_table_insert(context.process_ids,
                                (void *) (uintptr_t) hw_context->process->pid,
                                (void *) (uintptr_t) is_child);

        return is_child;
    }

    return (bool) entry->data;
}

static void print_accumulated_columns(struct gputop_client_context *ctx,
                                      struct gputop_accumulated_samples *samples)
{
    int i;
    for (i = 0; i < context.n_metric_columns; i++) {
        const struct gputop_metric_set_counter *counter =
            context.metric_columns[i].counter;
        char svalue[20];

        if (counter == &timestamp_counter) {
            snprintf(svalue, sizeof(svalue), "%" PRIu64,
                     samples->accumulator.first_timestamp);
        } else {
            double value = gputop_client_context_read_counter_value(ctx, samples, counter);
            if (context.human_units)
                gputop_client_pretty_print_value(counter->units, value, svalue, sizeof(svalue));
            else
                snprintf(svalue, sizeof(svalue), "%.2f", value);
        }
        output("%*s%s%s", context.metric_columns[i].width - strlen(svalue), "",
               svalue, i == (context.n_metric_columns - 1) ? "" : ",");
    }
    output("\n");
}

static void print_columns(struct gputop_client_context *ctx,
                          struct gputop_hw_context *hw_context)
{
    struct list_head *list;

    if (!match_process(hw_context))
        return;

    context.n_accumulations++;
    list = hw_context == NULL ? &ctx->graphs : &hw_context->graphs;

    if (context.last_samples == NULL) {
        list_for_each_entry(struct gputop_accumulated_samples, samples, list, link) {
            print_accumulated_columns(ctx, samples);
        }
        context.last_samples = list_last_entry(list, struct gputop_accumulated_samples, link);
    } else {
        context.last_samples = list_last_entry(list, struct gputop_accumulated_samples, link);
        print_accumulated_columns(ctx, context.last_samples);
    }

    if (context.child_exited)
        quit();
}

static bool handle_features()
{
    static bool info_printed = false;
    struct gputop_client_context *ctx = &context.ctx;
    int i;

    if (!context.metric_name ||
        (ctx->metric_set = gputop_client_context_symbol_to_metric_set(ctx, context.metric_name)) == NULL) {
        print_metrics();
        return true;
    }
    if (!context.metric_columns) {
        if (!context.all_columns) {
            print_metric_counter(ctx, ctx->metric_set);
            return true;
        } else {
            context.n_metric_columns = ctx->metric_set->n_counters + 1;
            context.metric_columns = calloc(context.n_metric_columns,
                                            sizeof(context.metric_columns[0]));
            context.metric_columns[0].symbol_name = strdup("Timestamp");
            for (i = 1; i < context.n_metric_columns; i++) {
                context.metric_columns[i].symbol_name =
                    strdup(ctx->metric_set->counters[i - 1].symbol_name);
            }
        }
    }
    if (!context.metric_columns[0].counter) {
        for (i = 0; i < context.n_metric_columns; i++) {
            int j;

            if (!strcmp("Timestamp", context.metric_columns[i].symbol_name)) {
                context.metric_columns[i].counter = &timestamp_counter;
            } else {
                for (j = 0; j < ctx->metric_set->n_counters; j++) {
                    if (!strcmp(ctx->metric_set->counters[j].symbol_name,
                                context.metric_columns[i].symbol_name)) {
                        context.metric_columns[i].counter = &ctx->metric_set->counters[j];
                        break;
                    }
                }

                if (!context.metric_columns[i].counter) {
                    comment("Unknown counter '%s'\n", context.metric_columns[i]);
                    return true;
                }
            }

            context.metric_columns[i].width =
                MAX2(strlen(context.metric_columns[i].counter->symbol_name),
                     strlen(unit_to_string(context.metric_columns[i].counter->units)) +
                     unit_to_width(context.metric_columns[i].counter->units) + 1) + 1;
        }
    }
    if (!info_printed && ctx->features) {
        info_printed = true;
        print_system_info();
    }
    if (context.print_headers)
        print_metric_column_names();
    return false;
}

static void on_ready(gputop_connection_t *conn, void *user_data)
{
    comment("Connected\n\n");
    gputop_client_context_reset(&context.ctx, conn);
}

static void on_data(gputop_connection_t *conn,
                    const void *data, size_t len,
                    void *user_data)
{
    static bool features_handled = false;

    gputop_client_context_handle_data(&context.ctx, data, len);
    if (!features_handled && context.ctx.features) {
        features_handled = true;
        if (handle_features()) {
            quit();
            return;
        }
        if (context.child_process_pid != 0)
            gputop_client_context_add_tracepoint(&context.ctx, "i915/i915_context_create");
        else
            gputop_client_context_start_sampling(&context.ctx);
    } else {
        if (!context.ctx.is_sampling) {
            bool all_tracepoints = true;
            list_for_each_entry(struct gputop_perf_tracepoint, tp,
                                &context.ctx.perf_tracepoints, link) {
                if (tp->event_id == 0)
                    all_tracepoints = false;
            }
            if (all_tracepoints)
                gputop_client_context_start_sampling(&context.ctx);
        } else {
            if (context.child_process_args && context.child_process_pid == -1)
                start_child_process();
        }
    }
}

static void on_close(gputop_connection_t *conn, const char *error,
                     void *user_data)
{
    if (error)
        comment("Connection error : %s\n", error);

    context.ctx.connection = NULL;
    uv_stop(uv_default_loop());
}

static const char *next_column(const char *string)
{
    const char *s;
    if (!string)
        return NULL;

    s = strchr(string, ',');
    if (s)
        return s + 1;
    return NULL;
}

static void usage(void)
{
    output("Usage: gputop-wrapper [options] <program> [program args...]\n"
           "\n"
           "\t -h, --help                        Display this help\n"
           "\t -H, --host <hostname>             Host to connect to\n"
           "\t -p, --port <port>                 Port on which the server is running\n"
           "\t -P, --period <period>             Accumulation period (in seconds, floating point)\n"
           "\t -m, --metric <name>               Metric set to use\n"
           "\t                                   (prints out a list of metric sets with: -m list)\n"
           "\t -M, --max                         Outputs maximum counter values\n"
           "\t                                   (first line after units)\n"
           "\t -c, --columns <col0,col1,..>      Columns to print out\n"
           "\t                                   (prints out a lists of counters with: -c list,\n"
           "\t                                    selects all counters with: -c all)\n"
           "\t -n, --no-human-units              Disable human readable units (for machine readable output)\n"
           "\t -N, --no-headers                  Disable headers (for machine readable output)\n"
           "\t -O, --child-output <filename>     Outputs the child's standard output to filename\n"
           "\t -o, --output <filename>           Outputs gputop-wrapper's data to filename\n"
           "\t                                   (disables human readable units)\n"
           "\t -w, --max-inactive-time <time>    Maximum time of inactivity before killing\n"
           "\t                                   the child process (in seconds, floating point)\n"
           "\n"
        );
}

static void init_context(void)
{
    memset(&context, 0, sizeof(context));

    context.human_units = true;
    context.print_headers = true;
    context.wrapper_output = stdout;

    context.child_process_pid = -1;
    context.child_process_output_file = "wrapper_child_output.txt";
}

int main (int argc, char **argv)
{
    const struct option long_options[] = {
        { "help",              no_argument,        0, 'h' },
        { "host",              required_argument,  0, 'H' },
        { "port",              required_argument,  0, 'p' },
        { "period",            required_argument,  0, 'P' },
        { "metric",            required_argument,  0, 'm' },
        { "max",               no_argument,        0, 'M' },
        { "columns",           required_argument,  0, 'c' },
        { "no-human-units",    no_argument,        0, 'n' },
        { "no-headers",        no_argument,        0, 'N' },
        { "child-output",      required_argument,  0, 'O' },
        { "output",            required_argument,  0, 'o' },
        { "max-inactive-time", required_argument,  0, 'w' },
        { NULL,                required_argument,  0, '-' },
        { 0, 0, 0, 0 }
    };
    int opt, port = 7890;
    const char *host = "localhost";
    bool opt_done = false;
    char temp[1024];
    uv_loop_t *loop;
    uv_signal_t ctrl_c_handle;
    uv_signal_t child_process_handle;

    init_context();

    gputop_client_context_init(&context.ctx);
    context.ctx.accumulate_cb = print_columns;
    context.ctx.oa_aggregation_period_ns = 1000000000ULL;

    while (!opt_done &&
           (opt = getopt_long(argc, argv, "c:hH:m:Mp:P:-nNO:o:w:", long_options, NULL)) != -1)
    {
        switch (opt) {
        case 'h':
            usage();
            return EXIT_SUCCESS;
        case 'H':
            host = optarg;
            break;
        case 'm':
            if (strcmp(optarg, "list"))
                context.metric_name = optarg;
            break;
        case 'M':
            context.print_maximums = true;
            break;
        case 'c': {
            if (!strcmp(optarg, "all")) {
                context.all_columns = true;
            } else if (strcmp(optarg, "list")) {
                const char *s = optarg;
                int n;
                context.n_metric_columns = 1;
                while ((s = next_column(s)) != NULL)
                    context.n_metric_columns++;

                context.metric_columns =
                    calloc(context.n_metric_columns, sizeof(context.metric_columns[0]));

                for (s = optarg, n = 0; s != NULL; s = next_column(s)) {
                    context.metric_columns[n++].symbol_name =
                        strndup(s, next_column(s) ? (next_column(s) - s - 1) : strlen(s));
                }
            }
            break;
        }
        case 'p':
            port = atoi(optarg);
            break;
        case 'P':
            context.ctx.oa_aggregation_period_ns = atof(optarg) * 1000000000.0f;
            break;
        case 'n':
            context.human_units = false;
            break;
        case 'N':
            context.print_headers = false;
            break;
        case 'O':
            context.child_process_output_file = optarg;
            break;
        case 'o':
            context.wrapper_output = fopen(optarg, "w+");
            context.human_units = false;
            if (context.wrapper_output == NULL) {
                comment("Unable to open output file '%s': %s\n",
                        optarg, strerror(errno));
                return EXIT_FAILURE;
            }
            break;
        case 'w':
            context.max_idle_child_time_ms = atof(optarg) * 1000.0f;
            break;
        case '-':
            opt_done = true;
            break;
        default:
            comment("Unrecognized option: %d\n", opt);
            return EXIT_FAILURE;
        }
    }

    loop = uv_default_loop();
    uv_signal_init(loop, &ctrl_c_handle);
    uv_signal_start_oneshot(&ctrl_c_handle, on_ctrl_c, SIGINT);

    uv_signal_init(loop, &child_process_handle);
    uv_signal_start_oneshot(&child_process_handle, on_child_process_exit, SIGCHLD);

    gputop_connect(host, port, on_ready, on_data, on_close, NULL);

    gputop_client_pretty_print_value(GPUTOP_PERFQUERY_COUNTER_UNITS_NS,
                                     context.ctx.oa_aggregation_period_ns,
                                     temp, sizeof(temp));
    comment("Server: %s:%i\n", host, port);
    comment("Sampling period: %s\n", temp);

    if (optind == argc) {
        comment("Monitoring: system wide\n");
        context.child_process_pid = 0;
    } else {
        if (strcmp(host, "localhost") != 0) {
            comment("Cannot monitor process on a different host.\n");
            return EXIT_FAILURE;
        }

        context.child_process_args = &argv[optind];
        context.n_child_process_args = argc - optind;
    }

    uv_run(loop, UV_RUN_DEFAULT);
    uv_signal_stop(&ctrl_c_handle);
    uv_signal_stop(&child_process_handle);

    gputop_client_context_reset(&context.ctx, NULL);

    comment("Finished.\n");

    return EXIT_SUCCESS;
}
