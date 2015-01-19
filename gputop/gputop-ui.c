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
#include <unistd.h>
#include <stdatomic.h>

#include <locale.h>
#include <ncurses.h>

#include <uv.h>

#include "gputop-gl.h"
#include "gputop-ui.h"
#include "gputop-util.h"


enum {
    GPUTOP_DEFAULT_COLOR,
    GPUTOP_HEADER_COLOR,
};

struct tab
{
    const char *name;

    void (*enter)(void);    /* when user switches to tab */
    void (*leave)(void);    /* when user switches away from tab */
    void (*input)(int key);
    void (*redraw)(WINDOW *win);
};

enum {
    INPUT_UNHANDLED = 1,
    INPUT_HANDLED = 1
};

static int real_stdin;
static int real_stdout;
static int real_stderr;

static uv_poll_t input_poll;
static uv_idle_t redraw_idle;

static int current_tab = 0;
static bool debug_disable_ncurses = 0;

uv_loop_t *gputop_ui_loop;


/* Follow the horrible ncurses convention of passing y before x */
static void
print_percentage_bar(WINDOW *win, int y, int x, float percent)
{
    int bar_len = 30 * 8 * (percent + .5) / 100.0;
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

    wmove(win, y, x);

    for (i = bar_len; i >= 8; i -= 8)
        wprintw(win, "%s", bars[8]);
    if (i)
        wprintw(win, "%s", bars[i]);
}

static void
perf_basic_tab_enter(void)
{

}

static void
perf_basic_tab_leave(void)
{

}

static void
perf_basic_tab_input(int key)
{

}

static void
perf_basic_tab_redraw(WINDOW *win)
{

}

static void
perf_3d_tab_enter(void)
{

}

static void
perf_3d_tab_leave(void)
{

}

static void
perf_3d_tab_input(int key)
{

}

static void
perf_3d_tab_redraw(WINDOW *win)
{

}

static void
gl_basic_tab_enter(void)
{
    atomic_store(&gputop_gl_monitoring_enabled, 1);
}

static void
gl_basic_tab_leave(void)
{
    atomic_store(&gputop_gl_monitoring_enabled, 0);
}

static void
gl_basic_tab_input(int key)
{

}

static void
print_percentage_counter(WINDOW *win, int y, int x,
                          struct intel_counter *counter, uint8_t *data)
{
    float percentage;

    switch(counter->data_type)
    {
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
        print_percentage_bar(win, y, x, percentage);
    else
        mvwprintw(win, y, x, "%f", percentage);
}

static void
print_raw_counter(WINDOW *win, int y, int x,
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
gl_basic_tab_redraw(WINDOW *win)
{
    struct winsys_surface **surfaces;
    //int win_width;
    //int win_height __attribute__ ((unused));
    int i;

    //getmaxyx(win, win_height, win_width);

    mvwprintw(win, 1, 0, "%40s  0%%                         100%%\n", "");
    mvwprintw(win, 2, 0, "%40s  ┌─────────────────────────────┐\n", "");

    pthread_rwlock_rdlock(&gputop_gl_lock);

    surfaces = gputop_gl_surfaces->data;
    for (i = 0; i < gputop_gl_surfaces->len; i++) {
	struct winsys_surface *wsurface = surfaces[i];
	struct winsys_context *wctx = wsurface->wctx;
	int finished_frames;
	int last_finished;
	struct frame_query *frame;
	int j;

	finished_frames = atomic_load(&wsurface->finished_frames);
	if (!finished_frames)
	    continue;

	last_finished = finished_frames % MAX_FRAME_QUERIES;

	frame = &wsurface->frames[last_finished];

	pthread_rwlock_rdlock(&frame->lock);

	for (j = 0; j < wctx->oa_query_info.n_counters; j++) {
	    struct intel_counter *counter = &wctx->oa_query_info.counters[j];

	    switch (counter->type) {
	    case GL_PERFQUERY_COUNTER_EVENT_INTEL:
		mvwprintw(win, j + 3, 0, "%40ss: ", counter->name);
		print_raw_counter(win, j + 3, 41, counter, frame->oa_data);
		break;
	    case GL_PERFQUERY_COUNTER_DURATION_NORM_INTEL:
		mvwprintw(win, j + 3, 0, "%40s: ", counter->name);
		print_raw_counter(win, j + 3, 41, counter, frame->oa_data);
		break;
	    case GL_PERFQUERY_COUNTER_DURATION_RAW_INTEL:
		mvwprintw(win, j + 3, 0, "%40ss: ", counter->name);
		print_raw_counter(win, j + 3, 41, counter, frame->oa_data);
		break;
	    case GL_PERFQUERY_COUNTER_THROUGHPUT_INTEL:
		mvwprintw(win, j + 3, 0, "%40ss: ", counter->name);
		print_raw_counter(win, j + 3, 41, counter, frame->oa_data);
		wprintw(win, " bytes/s");
		break;
	    case GL_PERFQUERY_COUNTER_RAW_INTEL:
		mvwprintw(win, j + 3, 0, "%40s: ", counter->name);
		if (counter->max_raw_value == 100)
		    print_percentage_counter(win, j + 3, 41, counter, frame->oa_data);
		else
		    print_raw_counter(win, j + 3, 41, counter, frame->oa_data);
		break;
	    case GL_PERFQUERY_COUNTER_TIMESTAMP_INTEL:
		mvwprintw(win, j + 3, 0, "%40s: ", counter->name);
		print_raw_counter(win, j + 3, 41, counter, frame->oa_data);
		break;
	    }
	}
#if 0
    uint64_t max_raw_value;

    unsigned id;
    unsigned type;

    unsigned data_offset;
    unsigned data_size;
    unsigned data_type;

    char name[64];
    char description[256];
#endif

	pthread_rwlock_unlock(&frame->lock);
    }

    pthread_rwlock_unlock(&gputop_gl_lock);
}

static void
gl_3d_tab_enter(void)
{
    atomic_store(&gputop_gl_monitoring_enabled, 1);
}

static void
gl_3d_tab_leave(void)
{
    atomic_store(&gputop_gl_monitoring_enabled, 0);
}

static void
gl_3d_tab_input(int key)
{

}

static void
gl_3d_tab_redraw(WINDOW *win)
{

}

static void
gl_debug_log_tab_enter(void)
{
    atomic_store(&gputop_gl_khr_debug_enabled, 1);
}

static void
gl_debug_log_tab_leave(void)
{
    atomic_store(&gputop_gl_khr_debug_enabled, 0);
}

static void
gl_debug_log_tab_input(int key)
{

}

static void
gl_debug_log_tab_redraw(WINDOW *win)
{
    struct winsys_context **contexts;
    struct winsys_context *wctx;
    int win_width __attribute__ ((unused));
    int win_height;
    struct log_entry *tmp;
    int i = 0;

    getmaxyx(win, win_height, win_width);

    pthread_rwlock_wrlock(&gputop_gl_lock);

    if (!gputop_gl_contexts->len) {
	pthread_rwlock_unlock(&gputop_gl_lock);

	mvwprintw(win, 1, 0, "No contexts found");
	return;
    }

    if (gputop_gl_contexts->len > 1)
	mvwprintw(win, 0, 0, "Warning: only printing log for first context found");

    contexts = gputop_gl_contexts->data;
    wctx = contexts[i];

    if (gputop_list_empty(&wctx->khr_debug_log)) {
	mvwprintw(win, 1, 0, "No performance warnings have been reported from OpenGL so far...\n");

	if (!wctx->is_debug_context) {
	    mvwprintw(win, 3, 0,
		      "Note: The application is not running with a debug context which\n"
		      "might effectively disable the KHR_debug extension.");
	    if (!gputop_gl_force_debug_ctx_enabled)
		mvwprintw(win, 6, 0,
			  "Note: GPU Top can force the creation of a debug context if\n"
			  "you pass --debug-context or set the GPUTOP_FORCE_DEBUG_CONTEXT\n"
			  "environment variable.");
	    else if (wctx->try_create_new_context_failed)
		mvwprintw(win, 6, 0,
			  "Note: GPU Top failed to force this app using the legacy \n"
			  "glXCreateContext API to create a debug context\n");
	}
    }

    gputop_list_for_each(tmp, &wctx->khr_debug_log, link) {
	mvwprintw(win, win_height - 1 - i, 0, tmp->msg);

	if (i++ > win_height)
	    break;
    }

    pthread_rwlock_unlock(&gputop_gl_lock);
}

static void
gl_knobs_tab_enter(void)
{

}

static void
gl_knobs_tab_leave(void)
{

}

static void
gl_knobs_tab_input(int key)
{

}

static void
gl_knobs_tab_redraw(WINDOW *win)
{

}

static void
app_io_tab_enter(void)
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

static struct tab tabs[] =
    {
	{
	    .name = "Basic Counters (system wide)",
	    .enter = perf_basic_tab_enter,
	    .leave = perf_basic_tab_leave,
	    .input = perf_basic_tab_input,
	    .redraw = perf_basic_tab_redraw,
	},
	{
	    .name = "3D Counters (system wide)",
	    .enter = perf_3d_tab_enter,
	    .leave = perf_3d_tab_leave,
	    .input = perf_3d_tab_input,
	    .redraw = perf_3d_tab_redraw,
	},
	{
	    .name = "Basic Counters (OpenGL context)",
	    .enter = gl_basic_tab_enter,
	    .leave = gl_basic_tab_leave,
	    .input = gl_basic_tab_input,
	    .redraw = gl_basic_tab_redraw,
	},
	{
	    .name = "3D Counters (OpenGL context)",
	    .enter = gl_3d_tab_enter,
	    .leave = gl_3d_tab_leave,
	    .input = gl_3d_tab_input,
	    .redraw = gl_3d_tab_redraw,
	},
	{
	    .name = "OpenGL debug log",
	    .enter = gl_debug_log_tab_enter,
	    .leave = gl_debug_log_tab_leave,
	    .input = gl_debug_log_tab_input,
	    .redraw = gl_debug_log_tab_redraw,
	},
	{
	    .name = "3D Counters (OpenGL context)",
	    .enter = gl_knobs_tab_enter,
	    .leave = gl_knobs_tab_leave,
	    .input = gl_knobs_tab_input,
	    .redraw = gl_knobs_tab_redraw,
	},
	{
	    .name = "Application IO",
	    .enter = app_io_tab_enter,
	    .leave = app_io_tab_leave,
	    .input = app_io_tab_input,
	    .redraw = app_io_tab_redraw,
	},
    };

static void
redraw_ui(void)
{
    struct tab *tab = &tabs[current_tab];
    int screen_width;
    int screen_height;
    WINDOW *titlebar_win;
    WINDOW *tab_win;

    if (debug_disable_ncurses)
	return;

    werase(stdscr); /* XXX: call after touchwin? */

    getmaxyx(stdscr, screen_height, screen_width);

    /* don't attempt to track what parts of stdscr have changed */
    touchwin(stdscr);

    titlebar_win = subwin(stdscr, 1, screen_width, 0, 0);
    //touchwin(stdscr);

    wattrset(titlebar_win, COLOR_PAIR (GPUTOP_HEADER_COLOR));
    wbkgd(titlebar_win, COLOR_PAIR (GPUTOP_HEADER_COLOR));
    werase(titlebar_win);

    mvwprintw(titlebar_win, 0, 0,
              "     gputop %s   «%s» (Press Tab key to cycle through)",
              PACKAGE_VERSION,
              tab->name);

    wnoutrefresh(titlebar_win);

    tab_win = subwin(stdscr, screen_height - 1, screen_width, 1, 0);
    //touchwin(stdscr);

    tabs[current_tab].redraw(tab_win);

    wnoutrefresh(tab_win);

    redrawwin(stdscr); /* indicate whole window has changed */
    wrefresh(stdscr); /* XXX: can we just call doupdate() instead? */

    delwin(titlebar_win);
    delwin(tab_win);
}

static void
timer_cb(uv_timer_t *timer)
{
    redraw_ui();
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
    if (key == 9) { /* Urgh, ncurses is not making things better :-/ */
	int n_tabs = sizeof(tabs) / sizeof(tabs[0]);

	tabs[current_tab].leave();

	current_tab++;
	current_tab %= n_tabs;
	tabs[current_tab].enter();

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

	tabs[current_tab].input(key);
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
    tabs[current_tab].leave();

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

    if (debug_disable_ncurses)
	return;

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
}

void *
gputop_ui_run(void *arg)
{
    uv_timer_t timer;

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

    uv_timer_init(gputop_ui_loop, &timer);
    uv_timer_start(&timer, timer_cb, 1000, 1000);

    uv_idle_init(gputop_ui_loop, &redraw_idle);

    tabs[current_tab].enter();

    uv_run(gputop_ui_loop, UV_RUN_DEFAULT);

    return 0;
}
