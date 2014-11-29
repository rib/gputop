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

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <dlfcn.h>
#include <pthread.h>
#include <unistd.h>
#include <string.h>
#include <stdatomic.h>

#include <locale.h>
#include <ncurses.h>

#include <uv.h>

#include <GL/glx.h>
#include <GL/glxext.h>

#define unlikely(x) __builtin_expect(x, 0)
#define dbg(format, ...) do { } while(0)


/* XXX: As a GL interposer we have to be extra paranoid about checking
 * for GL errors if we use GL ourselves otherwise we will leak error
 * state that can affect the running of the application itself.
 *
 * Also see: gputop_save_glerror() and gputop_glGetError()
 */
#define GE(X)                                                   \
    do {                                                        \
        GLenum __err;                                           \
        X;                                                      \
        while ((__err = pfn_glGetError()) != GL_NO_ERROR)	\
            dbg("GL error (%d):\n", __err);			\
    } while(0)

#define GE_RET(ret, X)                                          \
    do {                                                        \
        GLenum __err;                                           \
        ret = X;                                                \
        while ((__err = pfn_glGetError()) != GL_NO_ERROR)	\
            dbg("GL error (%d):\n", __err);			\
    } while(0)

struct array
{
    size_t elem_size;
    int len;

    size_t size;
    union {
	void *data;
	uint8_t *bytes;
    };
};

struct intel_counter
{
    uint64_t max_raw_value;

    unsigned id;
    unsigned type;

    unsigned data_offset;
    unsigned data_size;
    unsigned data_type;

    char name[64];
    char description[256];
};
#define MAX_QUERY_COUNTERS 100

struct intel_query_info
{
    unsigned id;
    unsigned n_counters;
    unsigned max_queries;
    unsigned n_active_queries;
    unsigned max_counter_data_len;
    unsigned caps_mask;

    struct intel_counter counters[MAX_QUERY_COUNTERS];

    char name[128];
};
#define MAX_QUERY_TYPES 10

struct frame_query
{
    pthread_rwlock_t lock;

    unsigned oa_query;
    uint8_t *oa_data;
    unsigned oa_data_len;

    unsigned pipeline_stats_query;
    uint8_t *pipeline_stats_data;
    unsigned pipeline_stats_data_len;
};
/* We pipeline up to three per-frame queries so we don't
 * need to block waiting for one frame to finish before
 * we can start a query for the next frame and we can
 * lock a frame while we read the counters for display
 * without blocking the GL thread.
 */
#define MAX_FRAME_QUERIES 3

struct winsys_context
{
    _Atomic int ref;

    GLXContext glx_ctx;
    /* TODO: Add EGL support */

    struct winsys_surface *read_wsurface;
    struct winsys_surface *draw_wsurface;

    bool gl_initialised;

    int n_query_types;
    struct intel_query_info query_types[MAX_QUERY_TYPES];

    struct intel_query_info pipeline_stats_query_info;
    struct intel_query_info oa_query_info;
};

struct winsys_surface
{
    struct winsys_context *wctx;

    /* Ignore pixmaps for now */
    GLXWindow glx_window;
    /* TODO: Add EGL support */

    struct frame_query frames[MAX_FRAME_QUERIES];
    _Atomic int started_frames;
    _Atomic int finished_frames;
};


static pthread_once_t init_once = PTHREAD_ONCE_INIT;

static pthread_key_t winsys_context_key;

static pthread_t gputop_thread_id;

static pthread_rwlock_t winsys_lock = PTHREAD_RWLOCK_INITIALIZER;
static struct array *winsys_contexts;
static struct array *winsys_surfaces;

static Bool (*real_glXMakeCurrent)(Display *dpy, GLXDrawable drawable,
				   GLXContext ctx);
static Bool (*real_glXMakeContextCurrent)(Display *dpy, GLXDrawable draw,
					  GLXDrawable read, GLXContext ctx);
static GLXContext (*real_glXCreateContext)(Display *dpy, XVisualInfo *vis,
					   GLXContext shareList, Bool direct);
static GLXContext (*real_glXCreateNewContext)(Display *dpy, GLXFBConfig config,
					      int render_type,
					      GLXContext share_list,
					      Bool direct);
static void (*real_glXDestroyContext)(Display *dpy, GLXContext glx_ctx);
static void (*real_glXSwapBuffers)(Display *dpy, GLXDrawable drawable);


static pthread_once_t initialise_gl_once = PTHREAD_ONCE_INIT;

static GLenum (*pfn_glGetError)(void);

static void (*pfn_glGetPerfQueryInfoINTEL)(GLuint queryId, GLuint queryNameLength,
					   GLchar *queryName, GLuint *dataSize,
					   GLuint *noCounters, GLuint *maxInstances,
					   GLuint *noActiveInstances, GLuint *capsMask);
static void (*pfn_glGetPerfCounterInfoINTEL)(GLuint queryId, GLuint counterId,
					     GLuint counterNameLength, GLchar *counterName,
					     GLuint counterDescLength, GLchar *counterDesc,
					     GLuint *counterOffset, GLuint *counterDataSize,
					     GLuint *counterTypeEnum,
					     GLuint *counterDataTypeEnum,
					     GLuint64 *rawCounterMaxValue);
static void (*pfn_glGetFirstPerfQueryIdINTEL)(GLuint *queryId);
static void (*pfn_glGetNextPerfQueryIdINTEL)(GLuint queryId, GLuint *nextQueryId);
static void (*pfn_glGetPerfQueryIdByNameINTEL)(GLchar *queryName, GLuint *queryId);
static void (*pfn_glCreatePerfQueryINTEL)(GLuint queryId, GLuint *queryHandle);
static void (*pfn_glDeletePerfQueryINTEL)(GLuint queryHandle);
static void (*pfn_glBeginPerfQueryINTEL)(GLuint queryHandle);
static void (*pfn_glEndPerfQueryINTEL)(GLuint queryHandle);
static void (*pfn_glGetPerfQueryDataINTEL)(GLuint queryHandle, GLuint flags,
					   GLsizei dataSize, GLvoid *data,
					   GLuint *bytesWritten);

static bool have_saved_glerror;
static GLenum saved_glerror;

static int real_stdin;
static int real_stdout;
static int real_stderr;

uv_loop_t *loop;

static void *
xmalloc(size_t size)
{
    void *ret = malloc(size);
    if (!ret)
	exit(1);
    return ret;
}

static struct array *
array_new(size_t elem_size, int alloc_len)
{
    struct array *array = xmalloc(sizeof(struct array));

    array->elem_size = elem_size;
    array->len = 0;
    array->size = elem_size * alloc_len;
    array->data = xmalloc(array->size);

    return array;
}

static void
array_set_len(struct array *array, int len)
{
    size_t needed = len * array->elem_size;

    array->len = len;

    if (array->size >= needed)
	return;

    array->data = realloc(array->data, array->size * 1.7);
    if (!array->data)
	exit(1);
    array->size = needed;
}

static void
array_remove(struct array *array, int idx)
{
    uint8_t *elem;
    uint8_t *last;

    array->len--;
    if (idx == array->len)
	return;

    elem = array->bytes + idx * array->elem_size;
    last = array->bytes + array->len * array->elem_size;
    memcpy(elem, last, array->elem_size);
}

static void
array_append(struct array *array, void *data)
{
    void *dst;

    array_set_len(array, array->len + 1);

    dst = array->bytes + array->elem_size * (array->len - 1);
    memcpy(dst, data, array->elem_size);
}

void *
gputop_passthrough_gl_resolve(const char *name)
{
    static void *libgl_handle = NULL;

    if (!libgl_handle) {
        const char *libgl_filename = getenv("GPUTOP_GL_LIBRARY");

        if (!libgl_filename)
            libgl_filename = "/usr/lib/libGL.so.1";

        libgl_handle = dlopen(libgl_filename, RTLD_LAZY | RTLD_GLOBAL);
        if (!libgl_handle) {
            fprintf(stderr, "gputop: Failed to open %s library: %s",
                    libgl_filename, dlerror());
            exit(EXIT_FAILURE);
        }
    }

    return dlsym(libgl_handle, name);
}

void *
gputop_passthrough_glx_resolve(const char *name)
{
    return gputop_passthrough_gl_resolve(name);
}

void *
gputop_passthrough_egl_resolve(const char *name)
{
    static void *libegl_handle = NULL;

    if (!libegl_handle) {
        const char *libegl_filename = getenv("GPUTOP_EGL_LIBRARY");

        if (!libegl_filename)
            libegl_filename = "/usr/lib/libEGL.so.1";

        libegl_handle = dlopen(libegl_filename, RTLD_LAZY | RTLD_GLOBAL);
        if (!libegl_handle) {
            fprintf(stderr, "gputop: Failed to open %s library: %s",
                    libegl_filename, dlerror());
            exit(EXIT_FAILURE);
        }
    }

    return dlsym(libegl_handle, name);
}

static void
save_glerror(void)
{
    saved_glerror = glGetError();
    have_saved_glerror = true;
}

GLenum
gputop_glGetError(void)
{
    if (have_saved_glerror)
	return saved_glerror;
    else
	return pfn_glGetError();
}

enum {
    GPUTOP_DEFAULT_COLOR,
    GPUTOP_HEADER_COLOR,
};

static int gputop_current_page = 0;

#define PERCENTAGE_BAR_END      (79)

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

/* Follow the horrible ncurses convention of passing y before x */
static void
print_percentage_bar(WINDOW *win, int y, int x, float percent)
{
    int bar_len = 30 * 8 * (percent + .5) / 100.0;
    int i;

    mvwin(win, y, x);

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

    pthread_rwlock_rdlock(&winsys_lock);

    surfaces = winsys_surfaces->data;
    for (i = 0; i < winsys_surfaces->len; i++) {
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

    pthread_rwlock_unlock(&winsys_lock);

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

static void
abort_cb(uv_idle_t *idle)
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

static void *
run_gputop(void *arg)
{
    FILE *infile;
    FILE *outfile;
    uv_timer_t timer;

    real_stdin = dup(STDIN_FILENO);
    real_stdout = dup(STDOUT_FILENO);
    real_stderr = dup(STDERR_FILENO);

    /* Instead of discarding the apps IO we might
     * want to expose it via a gltop tab later... */
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

    loop = uv_loop_new();

    uv_timer_init(loop, &timer);
    uv_timer_start(&timer, redraw_cb, 1000, 1000);

    uv_run(loop, UV_RUN_DEFAULT);

    return 0;
}

static void
glx_winsys_init(void)
{
    real_glXMakeCurrent = gputop_passthrough_gl_resolve("glXMakeCurrent");
    real_glXMakeContextCurrent = gputop_passthrough_gl_resolve("glXMakeContextCurrent");
    real_glXCreateContext = gputop_passthrough_gl_resolve("glXCreateContext");
    real_glXCreateNewContext = gputop_passthrough_gl_resolve("glXCreateNewContext");
    real_glXDestroyContext = gputop_passthrough_gl_resolve("glXDestroyContext");
    real_glXSwapBuffers = gputop_passthrough_gl_resolve("glXSwapBuffers");
}

static void
gputop_init(void)
{
    pthread_attr_t attrs;

    glx_winsys_init();

    pthread_key_create(&winsys_context_key, NULL);

    winsys_contexts = array_new(sizeof(void *), 5);
    winsys_surfaces = array_new(sizeof(void *), 5);

    pthread_attr_init(&attrs);
    pthread_create(&gputop_thread_id, &attrs, run_gputop, NULL);
}

static void
gputop_abort(const char *error) __attribute__((noreturn));

static void
gputop_abort(const char *error)
{
    if (loop) {
	uv_idle_t idle;

	uv_idle_init(loop, &idle);
	idle.data = (void *)error;
	uv_idle_start(&idle, abort_cb);

	for (;;)
	    ;
    }

    fprintf(stderr, "%s", error);
    fflush(stderr);
    exit(EXIT_FAILURE);
}

static void
initialise_gl(void)
{
#define SYM(X) { #X, (void **)&pfn_##X }
    struct {
	const char *name;
	void **ptr;
    } symbols[] = {
	SYM(glGetError),

	/* GL_INTEL_performance_query */
	SYM(glGetPerfQueryInfoINTEL),
	SYM(glGetPerfCounterInfoINTEL),
	SYM(glGetFirstPerfQueryIdINTEL),
	SYM(glGetNextPerfQueryIdINTEL),
	SYM(glGetPerfQueryIdByNameINTEL),
	SYM(glCreatePerfQueryINTEL),
	SYM(glDeletePerfQueryINTEL),
	SYM(glBeginPerfQueryINTEL),
	SYM(glEndPerfQueryINTEL),
	SYM(glGetPerfQueryDataINTEL),
    };
#undef SYM
    const char *gl_extensions;
    int i;

    gl_extensions = (const char *)glGetString(GL_EXTENSIONS);
    if (!strstr(gl_extensions, "GL_INTEL_performance_query"))
	gputop_abort("Missing required GL_INTEL_performance_query");

    for (i = 0; i < sizeof(symbols) / sizeof(symbols[0]); i++) {
	void *sym = glXGetProcAddress((GLubyte *)symbols[i].name);

	if (!sym)
	    gputop_abort("Missing required GL symbol");

	*(symbols[i].ptr) = sym;
    }
}

static void
get_query_info(unsigned id, struct intel_query_info *query_info)
{
    int i;

    query_info->id = id;

    pfn_glGetPerfQueryInfoINTEL(
	id,
	sizeof(query_info->name),
	query_info->name,
	&query_info->max_counter_data_len,
	&query_info->n_counters,
	&query_info->max_queries,
	&query_info->n_active_queries,
	&query_info->caps_mask);

    for (i = 0; i < query_info->n_counters && i < MAX_QUERY_COUNTERS; i++) {
	struct intel_counter *counter = &query_info->counters[i];

	/* XXX: INTEL_performance_query reserves id 0 with sequential
	 * counter ids, base 1 */
	counter->id = i + 1;

	pfn_glGetPerfCounterInfoINTEL(
	    id,
	    counter->id,
	    sizeof(counter->name),
	    counter->name,
	    sizeof(counter->description),
	    counter->description,
	    &counter->data_offset,
	    &counter->data_size,
	    &counter->type,
	    &counter->data_type,
	    &counter->max_raw_value);
    }
}

static void
winsys_context_gl_initialise(struct winsys_context *wctx)
{
    unsigned int query_id;

    pthread_once(&initialise_gl_once, initialise_gl);

    pfn_glGetPerfQueryIdByNameINTEL("Gen7 3D Observability Architecture Counters",
				    &query_id);
    get_query_info(query_id, &wctx->oa_query_info);

    pfn_glGetPerfQueryIdByNameINTEL("Gen7 Pipeline Statistics Registers",
				    &query_id);
    get_query_info(query_id, &wctx->pipeline_stats_query_info);


    for (pfn_glGetFirstPerfQueryIdINTEL(&query_id);
	 query_id && wctx->n_query_types < MAX_QUERY_TYPES;
	 pfn_glGetNextPerfQueryIdINTEL(query_id, &query_id))
    {
	get_query_info(query_id, &wctx->query_types[wctx->n_query_types++]);
    }
}

static struct winsys_context *
winsys_context_create(GLXContext glx_ctx)
{
    struct winsys_context *wctx = xmalloc(sizeof(struct winsys_context));

    wctx->glx_ctx = glx_ctx;
    wctx->gl_initialised = false;

    pthread_rwlock_wrlock(&winsys_lock);
    array_append(winsys_contexts, &wctx);
    pthread_rwlock_unlock(&winsys_lock);

    return wctx;
}

GLXContext
gputop_glXCreateContext(Display *dpy, XVisualInfo *vis,
		       GLXContext shareList, Bool direct)
{
    GLXContext glx_ctx;

    pthread_once(&init_once, gputop_init);

    glx_ctx = real_glXCreateContext(dpy, vis, shareList, direct);
    if (!glx_ctx)
	return glx_ctx;

    winsys_context_create(glx_ctx);

    return glx_ctx;
}

GLXContext
gputop_glXCreateNewContext(Display *dpy, GLXFBConfig config,
			  int render_type, GLXContext share_list, Bool direct)
{
    GLXContext glx_ctx;

    pthread_once(&init_once, gputop_init);

    glx_ctx = real_glXCreateNewContext(dpy, config, render_type,
				       share_list, direct);
    if (!glx_ctx)
	return glx_ctx;

    winsys_context_create(glx_ctx);

    return glx_ctx;
}

static struct winsys_context *
winsys_context_lookup(GLXContext glx_ctx, int *idx)
{
    struct winsys_context **contexts = winsys_contexts->data;
    int i;

    for (i = 0; i < winsys_contexts->len; i++) {
	struct winsys_context *wctx = contexts[i];

	if (wctx->glx_ctx == glx_ctx) {
	    *idx = i;
	    return wctx;
	}
    }

    return NULL;
}

static void
winsys_context_destroy(struct winsys_context *wctx)
{
    int idx;

    winsys_context_lookup(wctx->glx_ctx, &idx);

    array_remove(winsys_contexts, idx);

    if (wctx->draw_wsurface)
	wctx->draw_wsurface->wctx = NULL;
    if (wctx->read_wsurface)
	wctx->read_wsurface->wctx = NULL;

    free(wctx);
}

void
gputop_glXDestroyContext(Display *dpy, GLXContext glx_ctx)
{
    struct winsys_context *wctx;
    int context_idx;

    pthread_once(&init_once, gputop_init);

    wctx = winsys_context_lookup(glx_ctx, &context_idx);
    if (wctx) {
	if (atomic_fetch_sub(&wctx->ref, 1) == 1)
	    winsys_context_destroy(wctx);
    } else
	dbg("Spurious glXDestroyContext for unknown glx context");
}

/* XXX: We don't currently have a way of knowing when a window has
 * been destroyed for us to free winsys_surface state, so we just
 * *hope* applications don't create too many!
 *
 * If we didn't start *and* finish frame queries in glXSwapBuffers but
 * instead hooked into GL apis that can submit drawing commands for
 * marking the start of frames, then we'd potentially be able to
 * reference count winsys_surface state. This would likely be very
 * fragile though since GL can always be extended with new drawing
 * apis and we'd have to keep up with hooking into all of them.
 *
 * In an ideal world we'd have a GL extension that would let us
 * register a callback so the GL implementation could notify us when
 * a frame start (at a point where there is a context current) which
 * would be future proof.
 */
static struct winsys_surface *
winsys_surface_create(struct winsys_context *wctx, GLXWindow glx_window)
{
    struct winsys_surface *wsurface = xmalloc(sizeof(struct winsys_surface));
    int i;

    memset(wsurface, 0, sizeof(struct winsys_surface));

    /* XXX: gputop only supports drawables accessed from a single
     * context (see comment in glXSwapBuffers for further details
     * why)
     */
    wsurface->wctx = wctx;

    wsurface->glx_window = glx_window;

    for (i = 0; i < MAX_FRAME_QUERIES; i++) {
	struct frame_query *frame = &wsurface->frames[i];

	pthread_rwlock_init(&frame->lock, NULL);

	if (wctx->oa_query_info.id)
	    frame->oa_data = xmalloc(wctx->oa_query_info.max_counter_data_len);

	if (wctx->pipeline_stats_query_info.id)
	    frame->pipeline_stats_data =
		xmalloc(wctx->pipeline_stats_query_info.max_counter_data_len);
    }

    pthread_rwlock_wrlock(&winsys_lock);
    array_append(winsys_surfaces, &wsurface);
    pthread_rwlock_unlock(&winsys_lock);

    return wsurface;
}

static struct winsys_surface *
get_wsurface(struct winsys_context *wctx, GLXWindow glx_window)
{
    struct winsys_surface **surfaces = winsys_surfaces->data;
    struct winsys_surface *wsurface;
    int i;

    for (i = 0; i < winsys_surfaces->len; i++) {
	wsurface = surfaces[i];

	if (wsurface->glx_window == glx_window) {

	    if (wsurface->wctx != wctx)
		gputop_abort("gputop doesn't support applications accessing one drawable from multiple contexts");

	    return wsurface;
	}
    }

    /* XXX: we don't try and hook into glXCreateWindow as a place
     * to initialise winsys_surface state since GLX allows
     * applications to pass a vanilla Window xid as a glx drawable
     * without calling glXCreateWindow.
     *
     * XXX: we're currently assuming the passed xid is a window,
     * although it might actually be a GLXPixmap.
     */

    return winsys_surface_create(wctx, glx_window);
}

static bool
gputop_make_context_current(Display *dpy,
			   GLXDrawable draw, GLXDrawable read,
			   GLXContext glx_ctx)
{
    struct winsys_context *prev_wctx;
    struct winsys_context *wctx;
    int wctx_idx;
    Bool ret;

    pthread_once(&init_once, gputop_init);

    prev_wctx = pthread_getspecific(winsys_context_key);

    ret = real_glXMakeContextCurrent(dpy, draw, read, glx_ctx);
    if (!ret)
	return ret;

    wctx = winsys_context_lookup(glx_ctx, &wctx_idx);
    if (!wctx)
	dbg("Spurious glXMakeCurrent with unknown glx context");

    pthread_setspecific(winsys_context_key, wctx);

    /* NB: we can't simply bail here if prev_wctx == wctx since
     * the drawables may have changed */

    /* NB: GLX ref counts contexts so they are only destroyed when
     * they are no longer current in any thread. */

    if (wctx)
	atomic_fetch_add(&wctx->ref, 1);

    if (prev_wctx && atomic_fetch_sub(&prev_wctx->ref, 1) == 1) {
	winsys_context_destroy(prev_wctx);
	prev_wctx = NULL;
    }

    if (!wctx)
	return ret;

    if (!wctx->gl_initialised)
	winsys_context_gl_initialise(wctx);

    /* XXX: We have to make some assumptions about how applications
     * use GLX to be able to start and stop performance queries on
     * frame boundaries...
     *
     * In particular we are hooking into glXSwapBuffers as a way to
     * delimit frames but glXSwapBuffers acts on a drawable without
     * requiring a GL context to be current in the calling thread.
     *
     * Since we need to use the GL_INTEL_performance_query extension
     * to start and stop queries we do require there to be a context
     * current though.
     *
     * There will be software in the wild that will defer swap buffers
     * to a separate thread as a way to avoid blocking a rendering
     * thread but gputop won't be able to handle this situation.
     *
     * gtop explicitly imposes the constraint that a drawable can only
     * be made current in one context at a time and the constraint
     * that that context must be current during glXSwapBuffers.
     */
    wctx->draw_wsurface = get_wsurface(wctx, draw);
    wctx->draw_wsurface->wctx = wctx;

    if (read != draw) {
	wctx->read_wsurface = get_wsurface(wctx, read);
	wctx->read_wsurface->wctx = wctx;
    }

    return ret;

}
Bool
gputop_glXMakeCurrent(Display *dpy, GLXDrawable drawable, GLXContext glx_ctx)
{
    return gputop_make_context_current(dpy, drawable, drawable, glx_ctx);
}

Bool
gputop_glXMakeContextCurrent(Display *dpy, GLXDrawable draw,
			    GLXDrawable read, GLXContext ctx)
{
    return gputop_make_context_current(dpy, draw, read, ctx);
}

static void
winsys_surface_start_frame(struct winsys_surface *wsurface)
{
    struct winsys_context *wctx = wsurface->wctx;
    int current_frame_idx = wsurface->started_frames % MAX_FRAME_QUERIES;
    struct frame_query *frame = &wsurface->frames[current_frame_idx];

    if ((wsurface->started_frames - wsurface->finished_frames) >= MAX_FRAME_QUERIES)
	gputop_abort("Performance counter queries couldn't keep up with frame-rate");

    /* Although this probably doesn't matter too much on a per-frame
     * basis; it's not particularly cheap to create a query instance
     * so we aim to re-use the instances repeatedly... */
    if (wctx->oa_query_info.id && !frame->oa_query)
	GE(pfn_glCreatePerfQueryINTEL(wctx->oa_query_info.id, &frame->oa_query));

    /* XXX: pipeline statistic queries are apparently not working
     * a.t.m so just ignore for now */
#if 0
    if (wctx->pipeline_stats_query_info.id && !frame->pipeline_stats_query)
	GE(pfn_glCreatePerfQueryINTEL(wctx->pipeline_stats_query_info.id,
				      &frame->pipeline_stats_query));
#endif


    /* XXX: We're assuming that a BeginPerfQuery doesn't implicitly
     * flush anything to the hardware since we don't want to start
     * measuring anything while there's no real work going on.
     *
     * It would be good to have a unit test that makes sure we
     * maintain this behaviour in Mesa even though it doesn't seem to
     * be explicitly guaranteed by the spec.
     */

    if (frame->oa_query) {
	frame->oa_data_len = 0;
	GE(pfn_glBeginPerfQueryINTEL(frame->oa_query));
    }

#if 0
    if (frame->pipeline_stats_query) {
	frame->pipeline_stats_data_len = 0;
	GE(pfn_glBeginPerfQueryINTEL(frame->pipeline_stats_query));
    }
#endif

    atomic_fetch_add(&wsurface->started_frames, 1);
}

static void
winsys_surface_end_frame(struct winsys_surface *wsurface)
{
    int prev_frame_idx;
    struct frame_query *frame;

    if (!wsurface->started_frames)
	return;

    prev_frame_idx = (wsurface->started_frames - 1) % MAX_FRAME_QUERIES;

    frame = &wsurface->frames[prev_frame_idx];
    if (frame->oa_query)
	pfn_glEndPerfQueryINTEL(frame->oa_query);
}

static void
winsys_surface_check_for_finished_frames(struct winsys_surface *wsurface)
{
    struct winsys_context *wctx = wsurface->wctx;
    int next_finish_candidate;
    int last_finish_candidate;
    int i;

    if (!wsurface->started_frames)
	return;

    next_finish_candidate = wsurface->finished_frames % MAX_FRAME_QUERIES;
    last_finish_candidate =
	(next_finish_candidate + (MAX_FRAME_QUERIES - 1)) % MAX_FRAME_QUERIES;

    for (i = next_finish_candidate;
	 i != last_finish_candidate;
	 i = (i + 1) % MAX_FRAME_QUERIES)
    {
	struct frame_query *frame = &wsurface->frames[i];
	bool finished = true;

	pthread_rwlock_wrlock(&frame->lock);

	if (frame->oa_query && frame->oa_data_len == 0) {
	    GE(pfn_glGetPerfQueryDataINTEL(
		    frame->oa_query,
		    GL_PERFQUERY_DONOT_FLUSH_INTEL,
		    wctx->oa_query_info.max_counter_data_len,
		    frame->oa_data,
		    &frame->oa_data_len));

	    if (!frame->oa_data_len)
		finished = false;
	}

	if (frame->pipeline_stats_query &&
	    frame->pipeline_stats_data_len == 0)
	{
	    GE(pfn_glGetPerfQueryDataINTEL(
		    frame->pipeline_stats_query,
		    GL_PERFQUERY_DONOT_FLUSH_INTEL,
		    wctx->pipeline_stats_query_info.max_counter_data_len,
		    frame->pipeline_stats_data,
		    &frame->pipeline_stats_data_len));

	    if (!frame->pipeline_stats_data_len)
		finished = false;
	}

	pthread_rwlock_unlock(&frame->lock);

	if (finished) {
	    /* Should we explicitly issue a write memory barrier here
	     * to ensure the counter data has landed before
	     * advertising the frame to the redraw_cb() thread?
	     *
	     * TODO: double check memory sync semantics of _fetch_add()
	     */
	    atomic_fetch_add(&wsurface->finished_frames, 1);
	} else
	    break;
    }
}


/* XXX: The GLX api allows multiple threads to render to the same
 * drawable and glXSwapBuffers doesn't refer to the current GL
 * context it is an operation on a drawable visible to all
 * contexts.
 *
 * gputop currently assumes this is all madness and only allows a
 * drawable to be current in a single thread at a time and
 * therefore only associated with a single context otherwise we
 * couldn't drive per-frame queries from here since we'd have no
 * way of knowing which context to create + begin the queries
 * with.
 *
 * If we do end up needing to profile something binding
 * drawables into multiple threads at the same time then we will
 * need to hook into all GL entry points that can submit commands
 * to the gpu so that the first command after a swap-buffers
 * would implicitly start a new frame. We would still end the
 * queries here though (for all contexts).
 */
void
gputop_glXSwapBuffers(Display *dpy, GLXDrawable drawable)
{
    struct winsys_context *wctx;
    struct winsys_surface *wsurface;

    pthread_once(&init_once, gputop_init);

    wctx = pthread_getspecific(winsys_context_key);
    if (!wctx)
	gputop_abort("gputop can't support applications calling glXSwapBuffers without a current context");

    wsurface = get_wsurface(wctx, drawable);

    if (wsurface->wctx != wctx)
	gputop_abort("gputop can't support applications calling glXSwapBuffers with a drawable not bound to calling thread's current context");

    /* XXX: is checking wsurface->wctx->draw_surface == wsurface
     * redundant? */

    winsys_surface_end_frame(wsurface);

    real_glXSwapBuffers(dpy, drawable);

    /* Since we need to use the GL api there's a risk that we might
     * trigger an error and we don't want the application to ever see
     * such errors; potentially triggering an abort or other
     * misinterpretation of the error so we save the applications
     * current error state first...
     */
    save_glerror();

    winsys_surface_check_for_finished_frames(wsurface);
    winsys_surface_start_frame(wsurface);
}
