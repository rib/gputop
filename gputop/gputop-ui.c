/*
 * GPU Top
 *
 * Copyright (C) 2014,2015 Intel Corporation
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
#include <inttypes.h>
#include <unistd.h>
#include <libgen.h>

#include <locale.h>
#include <ncurses.h>

#include <uv.h>

/* NB: We use a portable stdatomic.h, so we don't depend on a recent compiler...
 */
#include "stdatomic.h"

#include "gputop-perf.h"
#include "gputop-ui.h"
#include "gputop-util.h"
#include "gputop-server.h"
#include "gputop-log.h"
#include "gputop-oa-counters.h"

#ifdef SUPPORT_GL
#include "gputop-gl.h"
#endif

enum {
    GPUTOP_DEFAULT_COLOR,
    GPUTOP_HEADER_COLOR,
    GPUTOP_INACTIVE_COLOR,
    GPUTOP_ACTIVE_COLOR,
    GPUTOP_TAB_COLOR,
    GPUTOP_BAR_BAD_COLOR,
    GPUTOP_BAR_GOOD_COLOR,
};

struct tab
{
    gputop_list_t link;

    char *nick;
    char *name;

    void (*enter)(struct tab*);    /* when user switches to tab */
    void (*leave)(void);    /* when user switches away from tab */
    void (*input)(int key);
    void (*redraw)(WINDOW *win);

    unsigned int gl_query_id;
    struct gputop_perf_query *query;
};

#define TAB_TITLE_WIDTH 15
#define SPARKLINE_HEIGHT 4

enum {
    INPUT_UNHANDLED = 1,
    INPUT_HANDLED = 2
};

static int real_stdin;
static int real_stdout;
static int real_stderr;

static uv_timer_t timer, fake_timer;
static uv_poll_t input_poll;
static uv_idle_t redraw_idle;

static struct tab *current_tab;
static struct tab *pending_tab;
static bool debug_disable_ncurses = false;

#ifdef SUPPORT_WEBUI
static bool web_ui = false;
static bool nodejs_ui = false;
#endif

static int y_pos;
static double zoom = 1;

struct key_func
{
    char* key;
    int (*func)(bool change_state, int value);
};

#ifdef SUPPORT_GL
static bool added_gl_tabs;
static int gl_knobs_selected_line;
static int gl_knobs_start_pos;
#endif

static gputop_list_t tabs;

static pthread_t gputop_ui_thread_id;

uv_loop_t *gputop_ui_loop;

static void redraw_ui(void);


/**
 * Given pointers to starting and ending OA snapshots, calculate the deltas for each
 * counter to update the results.
 */
static void
overview_sample_cb(struct gputop_perf_stream *stream, uint8_t *start, uint8_t *end)
{
    gputop_oa_accumulate_reports(stream->query, start, end);
}

static void
oa_counter_accumulator_clear(struct gputop_perf_stream *stream)
{
    struct gputop_perf_query *query = stream->query;

    memset(query->accumulator, 0, sizeof(query->accumulator));

    stream->oa.last = NULL;
}

static struct perf_oa_user overview_user = {
    .sample = overview_sample_cb,
};

static bool
i915_perf_oa_overview_open(struct gputop_perf_query *query,
                           bool enable_per_ctx)
{
    int period_exponent;
    char *error = NULL;

    assert(gputop_current_perf_query == NULL);

    if (!gputop_perf_initialize())
        return false;

    gputop_perf_current_user = &overview_user;
    gputop_current_perf_query = query;

    gputop_current_perf_query->per_ctx_mode = enable_per_ctx;

    /* The timestamp for HSW+ increments every 80ns
     *
     * The period_exponent gives a sampling period as follows:
     *   sample_period = 80ns * 2^(period_exponent + 1)
     *
     * The overflow period for Haswell can be calculated as:
     *
     * 2^32 / (n_eus * max_gen_freq * 2)
     * (E.g. 40 EUs @ 1GHz = ~53ms)
     *
     * We currently sample ~ every 10 milliseconds...
     */
    period_exponent = 16;

    gputop_current_perf_stream =
        gputop_open_i915_perf_oa_query(gputop_current_perf_query,
                                       period_exponent,
                                       32 * sysconf(_SC_PAGE_SIZE),
                                       gputop_perf_read_samples,
                                       false,
                                       &error);

    if (!gputop_current_perf_stream) {
        gputop_log(GPUTOP_LOG_LEVEL_HIGH, error, -1);
        free(error);

        gputop_current_perf_query = NULL;
        return false;
    }

    oa_counter_accumulator_clear(gputop_current_perf_stream);

    return true;
}

static void
overview_closed_cb(struct gputop_perf_stream *stream)
{
    current_tab = NULL;

    gputop_perf_stream_unref(gputop_current_perf_stream);
    gputop_current_perf_stream = NULL;
    gputop_current_perf_query = NULL;
}

static void
i915_perf_oa_overview_close(void)
{
    if (!gputop_current_perf_stream)
        return;

    gputop_perf_stream_close(gputop_current_perf_stream,
                             overview_closed_cb);
}

int gputop_perf_trace_buffer_size;
uint8_t *gputop_perf_trace_buffer;
bool gputop_perf_trace_empty;
bool gputop_perf_trace_full;
uint8_t *gputop_perf_trace_head;
int gputop_perf_n_samples = 0;

static void
trace_sample_cb(struct gputop_perf_stream *stream, uint8_t *start, uint8_t *end)
{
    struct gputop_perf_query *query = stream->query;
    int sample_size = query->perf_raw_size;

    if (gputop_perf_trace_empty) {
        memcpy(gputop_perf_trace_head, start, sample_size);
        gputop_perf_trace_head += sample_size;
        gputop_perf_trace_empty = false;
    }

    memcpy(gputop_perf_trace_head, end, sample_size);

    gputop_perf_trace_head += sample_size;
    if (gputop_perf_trace_head >= (gputop_perf_trace_buffer + gputop_perf_trace_buffer_size)) {
        gputop_perf_trace_head = gputop_perf_trace_buffer;
        gputop_perf_trace_full = true;
    }

    if (!gputop_perf_trace_full)
        gputop_perf_n_samples++;
}

static struct perf_oa_user trace_user = {
    .sample = trace_sample_cb,
};

bool
gputop_i915_perf_oa_trace_open(struct gputop_perf_query *query,
                               bool enable_per_ctx)
{
    int period_exponent;
    double duration = 5.0; /* seconds */
    uint64_t period_ns;
    uint64_t n_samples;
    char *error = NULL;

    assert(gputop_current_perf_query == NULL);

    if (!gputop_perf_initialize())
        return false;

    gputop_perf_current_user = &trace_user;
    gputop_current_perf_query = query;

    gputop_current_perf_query->per_ctx_mode = enable_per_ctx;

    /* The timestamp for HSW+ increments every 80ns
     *
     * The period_exponent gives a sampling period as follows:
     *   sample_period = 80ns * 2^(period_exponent + 1)
     *
     * Sample ~ every 1 millisecond...
     */
    period_exponent = 11;

    gputop_current_perf_stream =
        gputop_open_i915_perf_oa_query(gputop_current_perf_query,
                                       period_exponent,
                                       32 * sysconf(_SC_PAGE_SIZE),
                                       gputop_perf_read_samples,
                                       false,
                                       &error);
    if (!gputop_current_perf_stream) {
        gputop_log(GPUTOP_LOG_LEVEL_HIGH, error, -1);
        free(error);

        gputop_current_perf_query = NULL;
        return false;
    }

    period_ns = 80 * (2 << period_exponent);
    n_samples = (duration  * 1000000000.0) / period_ns;
    n_samples *= 1.25; /* a bit of leeway */

    gputop_perf_trace_buffer_size = n_samples * gputop_current_perf_query->perf_raw_size;
    gputop_perf_trace_buffer = xmalloc0(gputop_perf_trace_buffer_size);
    gputop_perf_trace_head = gputop_perf_trace_buffer;
    gputop_perf_trace_empty = true;
    gputop_perf_trace_full = false;

    return true;
}

static void
trace_closed_cb(struct gputop_perf_stream *stream)
{
    current_tab = NULL;

    gputop_perf_stream_unref(gputop_current_perf_stream);

    free(gputop_perf_trace_buffer);
    gputop_current_perf_query = NULL;
    gputop_current_perf_stream = NULL;
}

void
gputop_i915_perf_oa_trace_close(void)
{
    if (!gputop_current_perf_stream)
        return;

    gputop_perf_stream_close(gputop_current_perf_stream,
                             trace_closed_cb);
}

static void
timer_cb(uv_timer_t *timer)
{
    redraw_ui();
}

#define RANGE_BAR_WIDTH 30
/* Follow the horrible ncurses convention of passing y before x */
static void
print_range_bar(WINDOW *win, int y, int x, uint64_t val, uint64_t range)
{
    uint64_t bar_len = RANGE_BAR_WIDTH * 8 * val / range;
    static const char *bars[] = {
        " ",
        "▏",
        "▎",
        "▍",
        "▌",
        "▋",
        "▊",
        "▉",
        "█"
    };
    int i;

    wattrset(win, COLOR_PAIR (GPUTOP_BAR_GOOD_COLOR));

    for (i = 0; i < RANGE_BAR_WIDTH; i++) {
        if (wmove(win, y, x + i) == ERR)
            return;
        if (bar_len > 8) {
            wprintw(win, "%s", bars[8]);
            bar_len -= 8;
        } else {
            wprintw(win, "%s", bars[bar_len]);
            bar_len = 0;
        }
    }

}

static void
print_range_oa_counter(WINDOW *win, int y, int x,
                       struct gputop_perf_query *query,
                       const struct gputop_perf_query_counter *counter,
                       uint64_t range)
{
    uint64_t val;

    switch(counter->data_type) {
    case GPUTOP_PERFQUERY_COUNTER_DATA_UINT32:
        val = read_uint32_oa_counter(query, counter, query->accumulator);
        break;

    case GPUTOP_PERFQUERY_COUNTER_DATA_UINT64:
        val = read_uint64_oa_counter(query, counter, query->accumulator);
        break;

    case GPUTOP_PERFQUERY_COUNTER_DATA_FLOAT:
        val = (read_float_oa_counter(query, counter, query->accumulator) + 0.5);
        break;
    case GPUTOP_PERFQUERY_COUNTER_DATA_DOUBLE:
        val = (read_double_oa_counter(query, counter, query->accumulator) + 0.5);
        break;

    case GPUTOP_PERFQUERY_COUNTER_DATA_BOOL32:
        val = read_bool_oa_counter(query, counter, query->accumulator);
        break;
    }

    if (range > 0 && val <= range)
        print_range_bar(win, y, x, val, range);
    else {
        wattrset(win, A_NORMAL);
        mvwprintw(win, y, x, "%f", val);
    }
}

static void
print_raw_oa_counter(WINDOW *win, int y, int x,
                     struct gputop_perf_query *query,
                     const struct gputop_perf_query_counter *counter)
{
    switch(counter->data_type) {
    case GPUTOP_PERFQUERY_COUNTER_DATA_UINT32:
        mvwprintw(win, y, x, "%" PRIu32,
                  read_uint32_oa_counter(query,
                                         counter,
                                         query->accumulator));
        break;
    case GPUTOP_PERFQUERY_COUNTER_DATA_UINT64:
        mvwprintw(win, y, x, "%" PRIu64,
                  read_uint64_oa_counter(query,
                                         counter,
                                         query->accumulator));
        break;
    case GPUTOP_PERFQUERY_COUNTER_DATA_FLOAT:
        mvwprintw(win, y, x, "%f",
                  read_float_oa_counter(query,
                                        counter,
                                        query->accumulator));
        break;
    case GPUTOP_PERFQUERY_COUNTER_DATA_DOUBLE:
        mvwprintw(win, y, x, "%f",
                  read_double_oa_counter(query,
                                         counter,
                                         query->accumulator));
        break;
    case GPUTOP_PERFQUERY_COUNTER_DATA_BOOL32:
        mvwprintw(win, y, x, "%s",
                  read_bool_oa_counter(query,
                                       counter,
                                       query->accumulator) ? "TRUE" : "FALSE");
        break;
    }
}


static void
perf_counters_redraw(WINDOW *win)
{
    struct gputop_perf_query *query = gputop_current_perf_query;
    //int win_width;
    //int win_height __attribute__ ((unused));
    int j;
    int y = -y_pos + 1;

    //getmaxyx(win, win_height, win_width);

    if (!gputop_current_perf_query) {
        mvwprintw(win, 2, 0, "Perf query not open (check log tab for details)");
        return;
    }

    gputop_perf_read_samples(gputop_current_perf_stream);

    for (j = 0; j < query->n_counters; j++) {
        struct gputop_perf_query_counter *counter = &query->counters[j];

        wattrset(win, A_NORMAL);

        switch (counter->type) {
        case GPUTOP_PERFQUERY_COUNTER_EVENT:
            mvwprintw(win, y, 0, "%40s: ", counter->name);
            print_raw_oa_counter(win, y, 41, query, counter);
            break;
        case GPUTOP_PERFQUERY_COUNTER_DURATION_NORM:
            mvwprintw(win, y, 0, "%40s: ", counter->name);
            print_raw_oa_counter(win, y, 41, query, counter);
            break;
        case GPUTOP_PERFQUERY_COUNTER_DURATION_RAW:
            mvwprintw(win, y, 0, "%40s: ", counter->name);
            print_raw_oa_counter(win, y, 41, query, counter);
            break;
        case GPUTOP_PERFQUERY_COUNTER_THROUGHPUT:
            if (wmove(win, y, 0) == ERR)
                break;
            wprintw(win, "%40s: ", counter->name);
            print_raw_oa_counter(win, y, 41, query, counter);
            wprintw(win, " bytes/s");
            break;
        case GPUTOP_PERFQUERY_COUNTER_RAW:
            mvwprintw(win, y, 0, "%40s: ", counter->name);
            print_raw_oa_counter(win, y, 41, query, counter);
            break;
        case GPUTOP_PERFQUERY_COUNTER_TIMESTAMP:
            mvwprintw(win, y, 0, "%40s: ", counter->name);
            print_raw_oa_counter(win, y, 41, query, counter);
            break;
        }

        if (counter->max) {
            uint64_t max = counter->max(&gputop_devinfo, query, query->accumulator);
            print_range_oa_counter(win, y, 60, query, counter, max);
        }

        y++;
    }

    oa_counter_accumulator_clear(gputop_current_perf_stream);
}

static void
print_percentage_spark(WINDOW *win, int x, int y, float percent)
{
    int bar_len = SPARKLINE_HEIGHT * 8 * (percent + .5) / 100.0;
    static const char *bars[] = {
        " ",
        "▁",
        "▂",
        "▃",
        "▄",
        "▅",
        "▆",
        "▇",
        "█"
    };
    int i;

    for (i = 0; i < SPARKLINE_HEIGHT; i++) {
        if (wmove(win, y - i, x) == ERR)
            return;
        if (bar_len > 8) {
            wprintw(win, "%s", bars[8]);
            bar_len -= 8;
        } else {
            wprintw(win, "%s", bars[bar_len]);
            bar_len = 0;
        }
    }
}

static void
trace_print_percentage_oa_counter(WINDOW *win, int x, int y,
                                  struct gputop_perf_query *query,
                                  const struct gputop_perf_query_counter *counter)
{
    float percentage;

    switch(counter->data_type) {
    case GPUTOP_PERFQUERY_COUNTER_DATA_UINT32:
        percentage = read_uint32_oa_counter(query, counter, query->accumulator);
        break;

    case GPUTOP_PERFQUERY_COUNTER_DATA_UINT64:
        percentage = read_uint64_oa_counter(query, counter, query->accumulator);
        break;

    case GPUTOP_PERFQUERY_COUNTER_DATA_FLOAT:
        percentage = read_float_oa_counter(query, counter, query->accumulator);
        break;
    case GPUTOP_PERFQUERY_COUNTER_DATA_DOUBLE:
        percentage = read_double_oa_counter(query, counter, query->accumulator);
        break;

    case GPUTOP_PERFQUERY_COUNTER_DATA_BOOL32:
        percentage = read_bool_oa_counter(query, counter, query->accumulator);
        break;
    }

    if (percentage <= 100) {
        wattrset(win, COLOR_PAIR (GPUTOP_BAR_GOOD_COLOR));
        //wbkgd(win, COLOR_PAIR (GPUTOP_BAR_GOOD_COLOR));
        print_percentage_spark(win, x, y, percentage);
    } else {
        wattrset(win, COLOR_PAIR (GPUTOP_BAR_BAD_COLOR));
        //wbkgd(win, COLOR_PAIR (GPUTOP_BAR_BAD_COLOR));
        print_percentage_spark(win, x, y, 100);
    }
}

static void
trace_print_raw_oa_counter(WINDOW *win, int x, int y,
                           struct gputop_perf_query *query,
                           const struct gputop_perf_query_counter *counter)
{
}

static void
print_trace_counter_names(WINDOW *win, struct gputop_perf_query *query)
{
    int i;
    int y = 10 - y_pos;

    wattrset(win, A_NORMAL);
    for (i = 0; i < query->n_counters; i++) {
        struct gputop_perf_query_counter *counter = &query->counters[i];

        mvwprintw(win, y, 0, "%25s: ", counter->name);

        y += (SPARKLINE_HEIGHT + 1);
    }
}

static void
print_trace_counter_spark(WINDOW *win, struct gputop_perf_query *query, int x)
{
    int i;
    int y = 10 - y_pos;

    x += 27;

    for (i = 0; i < query->n_counters; i++) {
        struct gputop_perf_query_counter *counter = &query->counters[i];

        switch (counter->type) {
        case GPUTOP_PERFQUERY_COUNTER_EVENT:
            trace_print_raw_oa_counter(win, x, y, query, counter);
            break;
        case GPUTOP_PERFQUERY_COUNTER_DURATION_NORM:
            trace_print_raw_oa_counter(win, x, y, query, counter);
            break;
        case GPUTOP_PERFQUERY_COUNTER_DURATION_RAW:
            if (counter->max &&
                counter->max(&gputop_devinfo, query, query->accumulator) == 100)
            {
                trace_print_percentage_oa_counter(win, x, y, query, counter);
            } else
                trace_print_raw_oa_counter(win, x, y, query, counter);
            break;
        case GPUTOP_PERFQUERY_COUNTER_THROUGHPUT:
            trace_print_raw_oa_counter(win, x, y, query, counter);
            break;
        case GPUTOP_PERFQUERY_COUNTER_RAW:
            if (counter->max &&
                counter->max(&gputop_devinfo, query, query->accumulator) == 100)
            {
                trace_print_percentage_oa_counter(win, x, y, query, counter);
            } else
                trace_print_raw_oa_counter(win, x, y, query, counter);
            break;
        case GPUTOP_PERFQUERY_COUNTER_TIMESTAMP:
            trace_print_raw_oa_counter(win, x, y, query, counter);
            break;
        }

        y += (SPARKLINE_HEIGHT + 1);
    }
}

static uint64_t trace_view_start;

const uint8_t *
get_next_trace_sample(struct gputop_perf_query *query, const uint8_t *current_sample)
{
    const uint8_t *next = current_sample + query->perf_raw_size;

    if (next >= (gputop_perf_trace_buffer + gputop_perf_trace_buffer_size))
        next = gputop_perf_trace_buffer;

    return next;
}

static void
perf_oa_trace_redraw(WINDOW *win)
{
    struct gputop_perf_query *query = gputop_current_perf_query;
    int win_width;
    int win_height __attribute__ ((unused));
    size_t fill = gputop_perf_trace_full ?
        gputop_perf_trace_buffer_size : gputop_perf_trace_head - gputop_perf_trace_buffer;
    float fill_percentage = 100.0f * ((float)fill / (float)gputop_perf_trace_buffer_size);
    int timeline_width;
    uint64_t ns_per_column;
    uint64_t start_timestamp;
    //float bars[timeline_width];
    const uint8_t *report0;
    const uint8_t *report1;
    int i;

    wattrset(win, A_NORMAL);

    getmaxyx(win, win_height, win_width);

    if (!gputop_current_perf_query) {
        mvwprintw(win, 2, 0, "Perf query not open (check log tab for details)");
        return;
    }

    gputop_perf_read_samples(gputop_current_perf_stream);

    if (!gputop_perf_trace_full) {
        mvwprintw(win, 2, 0, "Trace buffer fill %3.0f%: ", fill_percentage);
        print_range_bar(win, 2, 25, fill_percentage, 100);

        mvwprintw(win, 3, 0, "%d samples", gputop_perf_n_samples);

        mvwprintw(win, 5, 0, "Trace buffer not full yet...");
        return;
    }

    timeline_width = win_width - 30;

    if (timeline_width < 0)
        return;

    ns_per_column = (1000000000 * zoom) / timeline_width;

    report0 = gputop_perf_trace_head;
    report1 = get_next_trace_sample(query, report0);
    start_timestamp = read_report_timestamp((uint32_t *)report0);

    oa_counter_accumulator_clear(gputop_current_perf_stream);

    print_trace_counter_names(win, query);

    for (i = 0; i < timeline_width; i++) {
        uint64_t column_end = start_timestamp + trace_view_start +
            (i *  ns_per_column) + ns_per_column;

        while (1) {
            uint64_t report_timestamp = read_report_timestamp((uint32_t *)report1);

            if (report_timestamp <= start_timestamp)
                return;

            if (report_timestamp >= column_end) {
                print_trace_counter_spark(win, query, i);
                oa_counter_accumulator_clear(gputop_current_perf_stream);
                break;
            }

            gputop_oa_accumulate_reports(gputop_current_perf_stream->query, report0, report1);

            report0 = report1;
            report1 = get_next_trace_sample(query, report0);
        }
    }
}

static void
perf_tab_enter(struct tab *owner_tab)
{
    uv_timer_init(gputop_ui_loop, &timer);
    uv_timer_start(&timer, timer_cb, 1000, 1000);

    i915_perf_oa_overview_open(owner_tab->query, false);
}

static void
perf_tab_leave(void)
{
    i915_perf_oa_overview_close();

    uv_timer_stop(&timer);
}

static void
perf_tab_input(int key)
{

}

static void
perf_tab_redraw(WINDOW *win)
{
    perf_counters_redraw(win);
}

static void
perf_3d_trace_tab_enter(struct tab *owner_tab)
{
    y_pos = 0;
    zoom = 1;

    uv_timer_init(gputop_ui_loop, &timer);
    uv_timer_start(&timer, timer_cb, 100, 100);

    gputop_i915_perf_oa_trace_open(owner_tab->query, false);
}

static void
perf_3d_trace_tab_leave(void)
{
    gputop_i915_perf_oa_trace_close();

    uv_timer_stop(&timer);
}

static void
perf_3d_trace_tab_input(int key)
{

}

static void
perf_3d_trace_tab_redraw(WINDOW *win)
{
    perf_oa_trace_redraw(win);
}

static struct tab tab_3d_trace =
{
    .nick = "3D Trace",
    .name = "3D Counter Trace (system wide)",
    .enter = perf_3d_trace_tab_enter,
    .leave = perf_3d_trace_tab_leave,
    .input = perf_3d_trace_tab_input,
    .redraw = perf_3d_trace_tab_redraw,
};

static void
debug_log_tab_enter(struct tab *owner_tab)
{

}

static void
debug_log_tab_leave(void)
{
    current_tab = NULL;
}

static void
debug_log_tab_input(int key)
{

}

static void
debug_log_tab_redraw(WINDOW *win)
{
    int win_width __attribute__ ((unused));
    int win_height;
    struct gputop_log_entry *tmp;
    int i = 0;

    getmaxyx(win, win_height, win_width);

    pthread_once(&gputop_log_init_once, gputop_log_init);

    pthread_rwlock_rdlock(&gputop_log_lock);

#ifdef SUPPORT_GL
    if (gputop_gl_contexts && gputop_list_empty(&gputop_log_entries)) {
        struct winsys_context **contexts = gputop_gl_contexts->data;
        struct winsys_context *wctx = contexts[i];

        mvwprintw(win, 1, 0, "No performance warnings have been reported from OpenGL so far...\n");

        if (!wctx->is_debug_context) {
            mvwprintw(win, 3, 0,
                      "Note: The application is not running with a debug context which\n"
                      "might effectively disable the KHR_debug extension.");
            if (!gputop_gl_force_debug_ctx_enabled)
                mvwprintw(win, 6, 0,
                          "Note: GPU Top can force the creation of a debug context if\n"
                          "you pass --debug-gl-context or set the GPUTOP_FORCE_DEBUG_CONTEXT\n"
                          "environment variable.");
            else if (wctx->try_create_new_context_failed)
                mvwprintw(win, 6, 0,
                          "Note: GPU Top failed to force this app using the legacy \n"
                          "glXCreateContext API to create a debug context\n");
        }

        win_height -= 10;
    }
#endif

    gputop_list_for_each(tmp, &gputop_log_entries, link) {
        mvwprintw(win, win_height - 1 - i, 0, tmp->msg);

        if (i++ > win_height)
            break;
    }

    pthread_rwlock_unlock(&gputop_log_lock);
}

static struct tab tab_debug_log =
{
    .nick = "Log",
    .name = "Debug log",
    .enter = debug_log_tab_enter,
    .leave = debug_log_tab_leave,
    .input = debug_log_tab_input,
    .redraw = debug_log_tab_redraw,
};



#ifdef SUPPORT_GL
static void
print_percentage_gl_pq_counter(WINDOW *win, int y, int x,
                               struct intel_counter *counter, uint8_t *data)
{
    float percentage;

    switch(counter->data_type) {
    case GL_PERFQUERY_COUNTER_DATA_UINT32_INTEL:
        percentage = *(uint32_t *)(data + counter->data_offset);
        break;
    case GL_PERFQUERY_COUNTER_DATA_UINT64_INTEL:
        percentage = *(uint64_t *)(data + counter->data_offset);
        break;
    case GL_PERFQUERY_COUNTER_DATA_FLOAT_INTEL:
        percentage = *(float *)(data + counter->data_offset);
        break;
    case GL_PERFQUERY_COUNTER_DATA_DOUBLE_INTEL:
        percentage = *(double *)(data + counter->data_offset);
        break;
    case GL_PERFQUERY_COUNTER_DATA_BOOL32_INTEL:
        percentage = (*(uint32_t *)(data + counter->data_offset)) ? 0 : 100;
        break;
    }

    if (percentage <= 100)
        print_range_bar(win, y, x, percentage, 100);
    else
        mvwprintw(win, y, x, "%f", percentage);
}

static void
print_raw_gl_pq_counter(WINDOW *win, int y, int x,
                        struct intel_counter *counter, uint8_t *data)
{
    switch(counter->data_type) {
    case GL_PERFQUERY_COUNTER_DATA_UINT32_INTEL:
        mvwprintw(win, y, x, "%" PRIu32, *(uint32_t *)(data + counter->data_offset));
        break;
    case GL_PERFQUERY_COUNTER_DATA_UINT64_INTEL:
        mvwprintw(win, y, x, "%" PRIu64, *(uint64_t *)(data + counter->data_offset));
        break;
    case GL_PERFQUERY_COUNTER_DATA_FLOAT_INTEL:
        mvwprintw(win, y, x, "%f", *(float *)(data + counter->data_offset));
        break;
    case GL_PERFQUERY_COUNTER_DATA_DOUBLE_INTEL:
        mvwprintw(win, y, x, "%f", *(double *)(data + counter->data_offset));
        break;
    case GL_PERFQUERY_COUNTER_DATA_BOOL32_INTEL:
        mvwprintw(win, y, x, "%s", (*(uint32_t *)(data + counter->data_offset)) ? "TRUE" : "FALSE");
        break;
    }
}

static void
gl_perf_query_tab_redraw(WINDOW *win)
{
    struct winsys_surface **surfaces;
    struct winsys_surface *wsurface;
    struct winsys_context *wctx;
    struct intel_query_info *query_info;
    struct gl_perf_query *obj;
    gputop_list_t finished;
    //int win_width;
    //int win_height __attribute__ ((unused));
    int i;
    int y = - y_pos + 1;

    //getmaxyx(win, win_height, win_width);

    mvwprintw(win, y++, 0, "%40s  0%%                         100%%\n", "");
    mvwprintw(win, y++, 0, "%40s  ┌─────────────────────────────┐\n", "");


    pthread_rwlock_rdlock(&gputop_gl_lock);

    if (!gputop_gl_surfaces->len) {
        pthread_rwlock_unlock(&gputop_gl_lock);
        return;
    }

    surfaces = gputop_gl_surfaces->data;

    /* XXX: we can only visualize the metrics for a single surface
     * currently, so we just pick the first one... */
    wsurface = surfaces[0];

    if (!wsurface->wctx) {
        pthread_rwlock_unlock(&gputop_gl_lock);
        return;
    }

    /* Steal the list of finished queries from the GL thread
     *
     * By stealing the list like this we can spend as long as we need
     * visualizing the data without blocking the GL thread from
     * finishing more queries and modifying the list.
     */
    pthread_rwlock_wrlock(&wsurface->finished_queries_lock);

    gputop_list_init(&finished);
    gputop_list_append_list(&finished, &wsurface->finished_queries);
    gputop_list_init(&wsurface->finished_queries);

    pthread_rwlock_unlock(&wsurface->finished_queries_lock);

    wctx = wsurface->wctx;
    query_info = wctx->current_query;

    /* We currently assume that queries surround a whole frame, and
     * for this overview we only care about the most recent query.
     */
    obj = gputop_list_last(&finished, struct gl_perf_query, link);
    if (!obj) {
        pthread_rwlock_unlock(&gputop_gl_lock);
        return;
    }

    for (i = 0; i < query_info->n_counters; i++) {
        struct intel_counter *counter = &query_info->counters[i];

        wattrset(win, A_NORMAL);

        switch (counter->type) {
        case GL_PERFQUERY_COUNTER_EVENT_INTEL:
            mvwprintw(win, y, 0, "%40s: ", counter->name);
            print_raw_gl_pq_counter(win, y, 41, counter, obj->data);
            break;
        case GL_PERFQUERY_COUNTER_DURATION_NORM_INTEL:
            mvwprintw(win, y, 0, "%40s: ", counter->name);
            print_raw_gl_pq_counter(win, y, 41, counter, obj->data);
            break;
        case GL_PERFQUERY_COUNTER_DURATION_RAW_INTEL:
            mvwprintw(win, y, 0, "%40s: ", counter->name);
            if (counter->max_raw_value == 100)
                print_percentage_gl_pq_counter(win, y, 41, counter, obj->data);
            else
                print_raw_gl_pq_counter(win, y, 41, counter, obj->data);
            break;
        case GL_PERFQUERY_COUNTER_THROUGHPUT_INTEL:
            if (wmove(win, y, 0) == ERR)
                break;
            wprintw(win, "%40s: ", counter->name);
            print_raw_gl_pq_counter(win, y, 41, counter, obj->data);
            wprintw(win, " bytes/s");
            break;
        case GL_PERFQUERY_COUNTER_RAW_INTEL:
            mvwprintw(win, y, 0, "%40s: ", counter->name);
            if (counter->max_raw_value == 100)
                print_percentage_gl_pq_counter(win, y, 41, counter, obj->data);
            else
                print_raw_gl_pq_counter(win, y, 41, counter, obj->data);
            break;
        case GL_PERFQUERY_COUNTER_TIMESTAMP_INTEL:
            mvwprintw(win, y, 0, "%40s: ", counter->name);
            print_raw_gl_pq_counter(win, y, 41, counter, obj->data);
            break;
        }

        y++;
    }

    /* Give the finished query objects back to the GL thread */
    pthread_rwlock_wrlock(&wctx->query_obj_cache_lock);
    gputop_list_append_list(&wctx->query_obj_cache, &finished);
    pthread_rwlock_unlock(&wctx->query_obj_cache_lock);


    pthread_rwlock_unlock(&gputop_gl_lock);
}

static void
gl_perf_query_tab_enter(struct tab *owner_tab)
{
    struct winsys_context **contexts;
    int i;

    /* XXX: This should have been ensured when switching away from a
     * GL tab, and if it's not the case the driver will block us
     * from opening a conflicting query... */
    assert(atomic_load(&gputop_gl_n_queries) == 0);

    uv_timer_init(gputop_ui_loop, &timer);
    uv_timer_start(&timer, timer_cb, 1000, 1000);

    pthread_rwlock_wrlock(&gputop_gl_lock);

    contexts = gputop_gl_contexts->data;
    for (i = 0; i < gputop_gl_contexts->len; i++) {
        struct winsys_context *wctx = contexts[i];
        struct intel_query_info *q;

        assert(wctx->current_query == NULL);

        gputop_list_for_each(q, &wctx->queries, link) {
            if (q->id == current_tab->gl_query_id) {
                wctx->current_query = q;
                break;
            }
        }
    }

    pthread_rwlock_unlock(&gputop_gl_lock);

    atomic_store(&gputop_gl_monitoring_enabled, true);
}

static void
gl_perf_query_tab_leave(void)
{
    struct winsys_context **contexts;
    int i;

    atomic_store(&gputop_gl_monitoring_enabled, false);

    /* XXX: lazy, sycnchronous blocking here but we need to avoid
     * trying to open any other query until we've deleted all current
     * GL perf query objects. (OA queries require a mutually exclusive
     * hardware configuration)
     */
    while (1) {
        struct timespec ts = { .tv_sec = 0, .tv_nsec = 1000000 };

        if (atomic_load(&gputop_gl_n_queries) == 0)
            break;

        nanosleep(&ts, NULL);
    }

    /* Not, strictly necessary, but just to be clear lets set
     * all context current_query pointers to NULL while
     * gputop_gl_monitoring_enabled is false ...
     */
    pthread_rwlock_wrlock(&gputop_gl_lock);

    contexts = gputop_gl_contexts->data;
    for (i = 0; i < gputop_gl_contexts->len; i++) {
        struct winsys_context *wctx = contexts[i];

        wctx->current_query = NULL;
    }

    pthread_rwlock_unlock(&gputop_gl_lock);

    uv_timer_stop(&timer);

    current_tab = NULL;
}

static void
gl_perf_query_tab_input(int key)
{

}

static int
scissor_test(bool change_state, int value)
{
    if (change_state)
        atomic_store(&gputop_gl_scissor_test_enabled, (bool)value);
    return atomic_load(&gputop_gl_scissor_test_enabled);
}

static const struct key_func knobs[] = {
    {"Scissor Test", scissor_test},
};

static void
gl_knobs_tab_enter(struct tab *owner_tab)
{
    uv_timer_init(gputop_ui_loop, &timer);
    uv_timer_start(&timer, timer_cb, 1000, 1000);
}

static void
gl_knobs_tab_leave(void)
{
    uv_timer_stop(&timer);

    current_tab = NULL;
}

static void
gl_knobs_tab_input(int key)
{
    switch (key) {
        case 13: //13 = KEY_ENTER.
            knobs[gl_knobs_selected_line].func(true, !knobs[gl_knobs_selected_line].func(false, 0));
            redraw_ui();
            break;
    }
}

static void
gl_knobs_tab_redraw(WINDOW *win)
{
    int win_width __attribute__ ((unused));
    int win_height;
    int i;
    int caret_pos = 1;
    const int knobs_length = sizeof(knobs) / sizeof(struct key_func);

    if (y_pos >= knobs_length)
        y_pos = knobs_length - 1;

    gl_knobs_selected_line = y_pos;
    getmaxyx(win, win_height, win_width);

    if (gl_knobs_selected_line < gl_knobs_start_pos)
        gl_knobs_start_pos = gl_knobs_selected_line;
    if (gl_knobs_selected_line > (gl_knobs_start_pos + (win_height - 3)))
        gl_knobs_start_pos = gl_knobs_selected_line - (win_height - 3);

    for (i = gl_knobs_start_pos; i < knobs_length; i++)
    {
        if (i == gl_knobs_selected_line)
            mvwprintw(win, caret_pos, 38 - strlen(knobs[i].key), "->");

        mvwprintw(win, caret_pos, 40 - strlen(knobs[i].key), "%s:        %s",
                knobs[i].key,  ((knobs[i].func(false, 0)) ? "[x]" : "[-]"));

        caret_pos++;
        if (caret_pos >= (win_height - 1))
            break;
    }
}

static struct tab tab_gl_knobs =
{
    .nick = "Tune",
    .name = "OpenGL Tuneables",
    .enter = gl_knobs_tab_enter,
    .leave = gl_knobs_tab_leave,
    .input = gl_knobs_tab_input,
    .redraw = gl_knobs_tab_redraw,
};

#endif /* SUPPORT_GL */

#if 0
static void
app_io_tab_enter(struct tab *owner_tab)
{

}

static void
app_io_tab_leave(void)
{

}

static void
app_io_tab_input(int key)
{

}

static void
app_io_tab_redraw(WINDOW *win)
{

}

static struct tab tab_io =
{
    .nick = "App",
    .name = "Application IO",
    .enter = app_io_tab_enter,
    .leave = app_io_tab_leave,
    .input = app_io_tab_input,
    .redraw = app_io_tab_redraw,
};
#endif

static void
redraw_ui(void)
{
    int screen_width;
    int screen_height;
    WINDOW *titlebar_win;
    struct tab *reference_tab = pending_tab ? pending_tab : current_tab;
    struct tab *tab;
    WINDOW *tab_win;
    int i;

#ifdef SUPPORT_GL
    if (gputop_gl_has_intel_performance_query_ext && !added_gl_tabs && !gputop_fake_mode) {
        struct tab *switch_to_tab = NULL;

        pthread_rwlock_rdlock(&gputop_gl_lock);

        /* XXX: the ncurses ui can only really cope with a single GL
         * context + surface, so we assume if there are multiple
         * contexts in use, they have the same queries, with the
         * same IDs available...
         */
        if (gputop_gl_contexts->len) {
            struct winsys_context **contexts = gputop_gl_contexts->data;
            struct winsys_context *first_wctx = contexts[0];
            struct intel_query_info *q;

            gputop_list_for_each(q, &first_wctx->queries, link) {
                struct tab *gl_tab = xmalloc0(sizeof(*gl_tab));

                if (strlen(q->name) > TAB_TITLE_WIDTH) {
                    asprintf(&gl_tab->nick, "%.*s...", TAB_TITLE_WIDTH - 5, q->name);
                } else
                    gl_tab->nick = strdup(q->name);

                gl_tab->name = q->name;
                gl_tab->enter = gl_perf_query_tab_enter;
                gl_tab->leave = gl_perf_query_tab_leave;
                gl_tab->input = gl_perf_query_tab_input;
                gl_tab->redraw = gl_perf_query_tab_redraw;

                gl_tab->gl_query_id = q->id;

                gputop_list_insert(tabs.prev, &gl_tab->link);

                if (strstr(q->name, "Render Metrics Basic"))
                    switch_to_tab = gl_tab;
            }

            gputop_list_insert(tabs.prev, &tab_gl_knobs.link);

            added_gl_tabs = true;
        }

        pthread_rwlock_unlock(&gputop_gl_lock);

        if (switch_to_tab) {
            pending_tab = switch_to_tab;

            /* NB: current_tab will be set to NULL once any resources associated
             * with the tab have been freed, at which point the next redraw_ui()
             * will call pending_tab->enter() */
            if (current_tab)
                current_tab->leave();
        }
    }
#endif

    if (debug_disable_ncurses)
        return;

    werase(stdscr); /* XXX: call after touchwin? */

    getmaxyx(stdscr, screen_height, screen_width);

    /* don't attempt to track what parts of stdscr have changed */
    touchwin(stdscr);

    titlebar_win = subwin(stdscr, 1, screen_width, 0, 0);
    //touchwin(stdscr);
    //

    wattrset(titlebar_win, COLOR_PAIR (GPUTOP_HEADER_COLOR));
    wbkgd(titlebar_win, COLOR_PAIR (GPUTOP_HEADER_COLOR));
    werase(titlebar_win);

    mvwprintw(titlebar_win, 0, 0,
              "     gputop %s   «%s» (Press Tab key to cycle through)",
              PACKAGE_VERSION,
              reference_tab->name);

    wnoutrefresh(titlebar_win);

    i = 0;
    gputop_list_for_each(tab, &tabs, link) {
        WINDOW *tab_title_win = subwin(stdscr, 1, TAB_TITLE_WIDTH,
                                       1, i * TAB_TITLE_WIDTH);
        int len = strlen(tab->nick);
        int offset;

        if (tab == reference_tab) {
            wattrset(tab_title_win, COLOR_PAIR (GPUTOP_ACTIVE_COLOR));
            wbkgd(tab_title_win, COLOR_PAIR (GPUTOP_ACTIVE_COLOR));
        } else {
            wattrset(tab_title_win, COLOR_PAIR (GPUTOP_INACTIVE_COLOR));
            wbkgd(tab_title_win, COLOR_PAIR (GPUTOP_INACTIVE_COLOR));
        }

        werase(tab_title_win);

        offset = (TAB_TITLE_WIDTH - len) / 2;
        if (tab == reference_tab)
            mvwprintw(tab_title_win, 0, offset, "[%s]", tab->nick);
        else
            mvwprintw(tab_title_win, 0, offset, tab->nick);

        wnoutrefresh(tab_title_win);

        i++;
    }

    tab_win = subwin(stdscr, screen_height - 2, screen_width, 2, 0);
    //touchwin(stdscr);

    if (current_tab)
        current_tab->redraw(tab_win);
    else if (pending_tab) {
        current_tab = pending_tab;
        pending_tab = NULL;

        current_tab->enter(current_tab);
    }

    wnoutrefresh(tab_win);

    redrawwin(stdscr); /* indicate whole window has changed */
    wrefresh(stdscr); /* XXX: can we just call doupdate() instead? */

    delwin(titlebar_win);
    delwin(tab_win);
}

static void
redraw_idle_cb(uv_idle_t *idle)
{
    uv_idle_stop(&redraw_idle);

    redraw_ui();
}

static int
common_input(int key)
{
    struct tab *reference_tab = pending_tab ? pending_tab : current_tab;
    struct tab *next = NULL;

    switch (key) {
    case KEY_RIGHT:
    case 9: /* Urgh, ncurses is not making things better :-/ */
        if (reference_tab->link.next != &tabs)
            next = gputop_container_of(reference_tab->link.next, struct tab, link);
        else
            next = gputop_container_of(reference_tab->link.next->next, struct tab, link);
        break;
    case KEY_LEFT:
        if (reference_tab->link.prev != &tabs)
            next = gputop_container_of(reference_tab->link.prev, struct tab, link);
        else
            next = gputop_container_of(reference_tab->link.prev->prev, struct tab, link);
        break;
    case KEY_UP:
        if (y_pos > 0)
            y_pos--;
        return INPUT_HANDLED;
    case KEY_DOWN:
        y_pos++;
        return INPUT_HANDLED;
    case '-':
        zoom *= 1.1;
        return INPUT_HANDLED;
    case '+':
    case '=':
        zoom *= 0.9;
        return INPUT_HANDLED;
    }

    if (next) {
        pending_tab = next;

        /* NB: current_tab will be set to NULL once any resources associated
         * with the tab have been freed, at which point the next redraw_ui()
         * will call pending_tab->enter() */
        if (current_tab)
            current_tab->leave();

        return INPUT_HANDLED;
    }

    return INPUT_UNHANDLED;
}

static void
input_read_cb(uv_poll_t *input_poll, int status, int events)
{
    int key;

    while ((key = wgetch(stdscr)) != ERR) {
        if (common_input(key) == INPUT_HANDLED)
            continue;

        current_tab->input(key);
    }

    uv_idle_start(&redraw_idle, redraw_idle_cb);
}

static void
reset_terminal(void)
{
    if (!debug_disable_ncurses) {
        endwin();

        dup2(real_stdin, STDIN_FILENO);
        dup2(real_stdout, STDOUT_FILENO);
        dup2(real_stderr, STDERR_FILENO);
    }
}

void
gputop_ui_quit_idle_cb(uv_idle_t *idle)
{
    char clear_screen[] = { 0x1b, '[', 'H',
                            0x1b, '[', 'J',
                            0x0 };
    current_tab->leave();

    reset_terminal();

    fprintf(stderr, "%s", clear_screen);
    fprintf(stderr, "\n");
    fprintf(stderr, "%s\n", (char *)idle->data);
    fprintf(stderr, "\n");

    _exit(EXIT_FAILURE);
}

static void
atexit_cb(void)
{
    reset_terminal();
}

static void
init_ncurses(FILE *infile, FILE *outfile)
{
    SCREEN *screen;
    char *current_locale;

    /* We assume we have a utf8 locale when writing unicode characters
     * to the terminal via ncurses...
     */
    current_locale = setlocale(LC_ALL, NULL);
    if (strstr("UTF-8", current_locale) == 0) {
        /* hope we'll get a utf8 locale and that we won't
         * upset the app we're monitoring... */
        setlocale(LC_ALL, "");
    }

    screen = newterm(NULL, outfile, infile);
    set_term(screen);

    nodelay(stdscr, true); /* wgetch shouldn't block if no input */

    nonl();
    intrflush(stdscr, false);
    keypad(stdscr, true); /* enable arrow keys */

    cbreak(); /* don't buffer to \n */

    noecho();
    curs_set(0); /* don't display the cursor */

    start_color();
    use_default_colors();

    init_pair(GPUTOP_DEFAULT_COLOR, COLOR_WHITE, COLOR_BLACK);
    init_pair(GPUTOP_HEADER_COLOR, COLOR_WHITE, COLOR_BLUE);
    init_pair(GPUTOP_INACTIVE_COLOR, COLOR_WHITE, COLOR_BLACK);
    init_pair(GPUTOP_ACTIVE_COLOR, COLOR_WHITE, COLOR_BLUE);
    init_pair(GPUTOP_TAB_COLOR, COLOR_WHITE, COLOR_BLACK);
    init_pair(GPUTOP_BAR_GOOD_COLOR, COLOR_GREEN, COLOR_BLACK);
    init_pair(GPUTOP_BAR_BAD_COLOR, COLOR_RED, COLOR_BLACK);
}

static void
exit_fake_mode_cb(uv_timer_t *timer)
{
    exit(0);
}

void *
gputop_ui_run(void *arg)
{
    struct tab *tab, *tmp;
#ifdef SUPPORT_WEBUI
    const char *mode = getenv("GPUTOP_MODE");

    if (mode && strcmp(mode, "remote") == 0) {
        debug_disable_ncurses = true;
        web_ui = true;
    } else if (mode && strcmp(mode, "nodejs") == 0) {
        debug_disable_ncurses = true;
        nodejs_ui = true;
    }
#endif

    gputop_ui_loop = uv_loop_new();

    if (!debug_disable_ncurses) {
        FILE *infile;
        FILE *outfile;

        real_stdin = dup(STDIN_FILENO);
        real_stdout = dup(STDOUT_FILENO);
        real_stderr = dup(STDERR_FILENO);

        /* Instead of discarding the apps IO we might
         * want to expose it via a gputop tab later... */
#if 0
        {
        int in_sp[2];
        int out_sp[2];
        int err_sp[2];

        socketpair(AF_UNIX, SOCK_STREAM, 0, in_sp);
        socketpair(AF_UNIX, SOCK_STREAM, 0, out_sp);
        socketpair(AF_UNIX, SOCK_STREAM, 0, err_sp);

        dup2(in_sp[1], STDIN_FILENO);
        dup2(out_sp[1], STDOUT_FILENO);
        dup2(err_sp[1], STDERR_FILENO);

        close(in_sp[1]);
        close(out_sp[1]);
        close(err_sp[1]);
        }
#else
        {
        int null_fd = open("/dev/null", O_RDWR|O_CLOEXEC);

        dup2(null_fd, STDIN_FILENO);
        dup2(null_fd, STDOUT_FILENO);
        dup2(null_fd, STDERR_FILENO);

        close(null_fd);
        }

        infile = fdopen(real_stdin, "r+");
        outfile = fdopen(real_stdout, "r+");

        init_ncurses(infile, outfile);

        uv_poll_init(gputop_ui_loop, &input_poll, real_stdin);
        uv_poll_start(&input_poll, UV_READABLE, input_read_cb);
#endif
    } else {
        real_stdin = STDIN_FILENO;
        real_stdout = STDOUT_FILENO;
        real_stderr = STDERR_FILENO;
    }

    atexit(atexit_cb);

#ifdef SUPPORT_WEBUI
    if (web_ui) {
        gputop_server_run();

    } else if (nodejs_ui) {
        pid_t pid;

        gputop_server_run();
        pid = fork();

        if (pid != 0) {
            char *exec_args[] = { "node",
                GPUTOP_NODEJS_ROOT "/gputop-nodejs-ui.js", NULL };
            int i, err;

            unsetenv("LD_PRELOAD");
            execvp(exec_args[0], exec_args);
            err = errno;

            fprintf(stderr, "gputop: Failed to run Node.js application: \n\n"
                "  ");
            for (i = 0; i < 2; i++)
                fprintf(stderr, "%s ", exec_args[i]);
            fprintf(stderr, "\n\n%s\n", strerror(err));
        }
    } else
#endif
    {
        uv_idle_init(gputop_ui_loop, &redraw_idle);

        current_tab->enter(current_tab);
    }

    if (gputop_fake_mode && gputop_get_bool_env("GPUTOP_TRAVIS_MODE")) {
        uv_timer_init(gputop_ui_loop, &fake_timer);
        uv_timer_start(&fake_timer, exit_fake_mode_cb, 5000, 5000);
    }

    uv_run(gputop_ui_loop, UV_RUN_DEFAULT);

    gputop_perf_free();

    if (!debug_disable_ncurses) {
        gputop_list_for_each_safe(tab, tmp, &tabs, link) {
            free (tab);
        }
    }


    return 0;
}

__attribute__((constructor)) void
gputop_ui_init(void)
{
    int i;
    pthread_attr_t attrs;
    struct gputop_perf_query *trace_query = NULL;

    gputop_perf_initialize();
    gputop_list_init(&tabs);
    current_tab = NULL;

    for (i = 0; i < perf_oa_supported_query_guids->len; i++)
    {
        struct gputop_perf_query *query = (gputop_hash_table_search(queries,
            array_value_at(perf_oa_supported_query_guids, char*, i)))->data;
        struct tab *counter_tab = xmalloc0(sizeof(struct tab));

        counter_tab->name = (char*)query->name;
        counter_tab->nick = (char*)query->symbol_name;
        counter_tab->query = query;
        counter_tab->enter = perf_tab_enter;
        counter_tab->leave = perf_tab_leave;
        counter_tab->input = perf_tab_input;
        counter_tab->redraw = perf_tab_redraw;
        gputop_list_insert(tabs.prev, &counter_tab->link);

        if (current_tab == NULL)
            current_tab = counter_tab;
        if (trace_query == NULL &&
            strcmp(counter_tab->nick, "RenderBasic") == 0)
        {
            current_tab = counter_tab;
            trace_query = query;
        }
    }

    if (trace_query != NULL) {
        tab_3d_trace.query = trace_query;
        gputop_list_insert(tabs.prev, &tab_3d_trace.link);
    }

    gputop_list_insert(tabs.prev, &tab_debug_log.link);

    pthread_attr_init(&attrs);
    pthread_create(&gputop_ui_thread_id, &attrs, gputop_ui_run, NULL);
}
