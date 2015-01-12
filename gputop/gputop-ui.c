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


static int real_stdin;
static int real_stdout;
static int real_stderr;

static int gputop_current_page = 0;

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
print_normalised_duration(WINDOW *win, int y, int x,
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
print_frame_counter(struct intel_counter *counter, uint8_t *data)
{

    switch(counter->data_type)
    {
    case GL_PERFQUERY_COUNTER_DATA_UINT32_INTEL:
	wprintw(stdscr, "%" PRIu32, *(uint32_t *)(data + counter->data_offset));
	break;
    case GL_PERFQUERY_COUNTER_DATA_UINT64_INTEL:
	wprintw(stdscr, "%" PRIu64, *(uint64_t *)(data + counter->data_offset));
	break;
    case GL_PERFQUERY_COUNTER_DATA_FLOAT_INTEL:
	wprintw(stdscr, "%f", *(float *)(data + counter->data_offset));
	break;
    case GL_PERFQUERY_COUNTER_DATA_DOUBLE_INTEL:
	wprintw(stdscr, "%f", *(double *)(data + counter->data_offset));
	break;
    case GL_PERFQUERY_COUNTER_DATA_BOOL32_INTEL:
	wprintw(stdscr, "%s", (*(uint32_t *)(data + counter->data_offset)) ? "TRUE" : "FALSE");
	break;
    }
}



static void
redraw_cb(uv_timer_t *timer)
{
    struct winsys_surface **surfaces;
    int screen_width;
    int screen_height __attribute__ ((unused));
    WINDOW *titlebar_win;
    int n_pages = 1;
    int i;

    init_pair(GPUTOP_DEFAULT_COLOR, COLOR_WHITE, COLOR_BLACK);
    init_pair(GPUTOP_HEADER_COLOR, COLOR_WHITE, COLOR_BLUE);

    werase(stdscr);

    getmaxyx(stdscr, screen_height, screen_width);

    titlebar_win = subwin(stdscr, 1, screen_width, 0, 0);
    touchwin(stdscr);

    wattrset(titlebar_win, COLOR_PAIR (GPUTOP_HEADER_COLOR));
    wbkgd(titlebar_win, COLOR_PAIR (GPUTOP_HEADER_COLOR));
    werase(titlebar_win);

    mvwprintw(titlebar_win, 0, 0,
              "     gputop %s       Tab %d/%d (Press Tab key to cycle through)",
              PACKAGE_VERSION,
              gputop_current_page, n_pages);

    wnoutrefresh(titlebar_win);


    mvwprintw(stdscr, 2, 0, "%40s  0%%                         100%%\n", "");
    mvwprintw(stdscr, 3, 0, "%40s  ┌─────────────────────────────┐\n", "");

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

	    switch (counter->type)
	    {
	    case GL_PERFQUERY_COUNTER_EVENT_INTEL:
		mvwprintw(stdscr, j + 4, 0, "%40ss: ", counter->name);
		print_frame_counter(counter, frame->oa_data);
		break;
	    case GL_PERFQUERY_COUNTER_DURATION_NORM_INTEL:
		mvwprintw(stdscr, j + 4, 0, "%40s: ", counter->name);
                print_normalised_duration(stdscr, j + 4, 41, counter, frame->oa_data);
		//print_frame_counter(counter, frame->oa_data);
		break;
	    case GL_PERFQUERY_COUNTER_DURATION_RAW_INTEL:
		mvwprintw(stdscr, j + 4, 0, "%40ss: ", counter->name);
		print_frame_counter(counter, frame->oa_data);
		break;
	    case GL_PERFQUERY_COUNTER_THROUGHPUT_INTEL:
		mvwprintw(stdscr, j + 4, 0, "%40ss: ", counter->name);
		print_frame_counter(counter, frame->oa_data);
		wprintw(stdscr, " bytes/s");
		break;
	    case GL_PERFQUERY_COUNTER_RAW_INTEL:
		mvwprintw(stdscr, j + 4, 0, "%40s: ", counter->name);
                print_normalised_duration(stdscr, j + 4, 41, counter, frame->oa_data);
		//print_frame_counter(counter, frame->oa_data);
		break;
	    case GL_PERFQUERY_COUNTER_TIMESTAMP_INTEL:
		mvwprintw(stdscr, j + 4, 0, "%40s: ", counter->name);
		print_frame_counter(counter, frame->oa_data);
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

    redrawwin(stdscr);
    wrefresh(stdscr);

    delwin(titlebar_win);
}

static void
reset_terminal(void)
{
    endwin();

    dup2(STDIN_FILENO, real_stdin);
    dup2(STDOUT_FILENO, real_stdout);
    dup2(STDERR_FILENO, real_stderr);
}

void
gputop_ui_quit_idle_cb(uv_idle_t *idle)
{
    reset_terminal();
    fprintf(stderr, "%s", (char *)idle->data);
    fflush(stderr);
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

    nonl();
    intrflush(stdscr, false);
    keypad(stdscr, true); /* enable arrow keys */

    cbreak(); /* don't buffer to \n */

    noecho();
    curs_set(0); /* don't display the cursor */

    start_color();
    use_default_colors();
}

void *
gputop_ui_run(void *arg)
{
    FILE *infile;
    FILE *outfile;
    uv_timer_t timer;

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
#endif

    infile = fdopen(real_stdin, "r+");
    outfile = fdopen(real_stdout, "r+");

    init_ncurses(infile, outfile);

    atexit(atexit_cb);

    gputop_ui_loop = uv_loop_new();

    uv_timer_init(gputop_ui_loop, &timer);
    uv_timer_start(&timer, redraw_cb, 1000, 1000);

    uv_run(gputop_ui_loop, UV_RUN_DEFAULT);

    return 0;
}
