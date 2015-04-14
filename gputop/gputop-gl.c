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

#include <uv.h>

#include "gputop-gl.h"
#include "gputop-ui.h"
#include "gputop-util.h"

/* XXX: As a GL interposer we have to be extra paranoid about
 * generating GL errors that might trample on the error state that the
 * application sees.
 *
 * For now we just try our best to never generate internal errors but
 * if we find we can't always guarantee that then we should be able to
 * intercept the KHR_debug extension and glGetError() to effectively
 * hide any internal errors.
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


static pthread_once_t init_once = PTHREAD_ONCE_INIT;

static pthread_key_t winsys_context_key;

static void *(*real_glXGetProcAddress)(const GLubyte *procName);
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
static GLXContext (*real_glXCreateContextAttribsARB)(Display *dpy,
						     GLXFBConfig config,
						     GLXContext share_context,
						     Bool direct,
						     const int *attrib_list);
static void (*real_glXDestroyContext)(Display *dpy, GLXContext glx_ctx);
static void (*real_glXSwapBuffers)(Display *dpy, GLXDrawable drawable);


static pthread_once_t initialise_gl_once = PTHREAD_ONCE_INIT;

static const GLubyte *(*pfn_glGetStringi)(GLenum name, GLuint index);
static GLenum (*pfn_glGetError)(void);
static void (*pfn_glEnable)(GLenum cap);
static void (*pfn_glDisable)(GLenum cap);

static void (*pfn_glDebugMessageControl)(GLenum source,
					 GLenum type,
					 GLenum severity,
					 GLsizei count,
					 const GLuint *ids,
					 GLboolean enabled);
static void (*pfn_glDebugMessageCallback)(GLDEBUGPROC callback,
					  const void *userParam);

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

GLXContext
gputop_glXCreateContextAttribsARB(Display *dpy,
				  GLXFBConfig config,
				  GLXContext share_context,
				  Bool direct,
				  const int *attrib_list);

bool gputop_has_intel_performance_query_ext;
bool gputop_has_khr_debug_ext;

pthread_rwlock_t gputop_gl_lock = PTHREAD_RWLOCK_INITIALIZER;
struct array *gputop_gl_contexts;
struct array *gputop_gl_surfaces;

bool gputop_gl_force_debug_ctx_enabled = false;

_Atomic int gputop_gl_n_countext = 0;

_Atomic bool gputop_gl_monitoring_enabled = false;
_Atomic bool gputop_gl_khr_debug_enabled = true;
_Atomic int gputop_gl_n_monitors;


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
glx_winsys_init(void)
{
    real_glXGetProcAddress = gputop_passthrough_gl_resolve("glXGetProcAddress");
    real_glXMakeCurrent = gputop_passthrough_gl_resolve("glXMakeCurrent");
    real_glXMakeContextCurrent = gputop_passthrough_gl_resolve("glXMakeContextCurrent");
    real_glXCreateContext = gputop_passthrough_gl_resolve("glXCreateContext");
    real_glXCreateNewContext = gputop_passthrough_gl_resolve("glXCreateNewContext");
    real_glXCreateContextAttribsARB = real_glXGetProcAddress((GLubyte *)"glXCreateContextAttribsARB");
    real_glXDestroyContext = gputop_passthrough_gl_resolve("glXDestroyContext");
    real_glXSwapBuffers = gputop_passthrough_gl_resolve("glXSwapBuffers");
}

static void
gputop_gl_init(void)
{
    glx_winsys_init();

    pthread_key_create(&winsys_context_key, NULL);

    gputop_gl_contexts = array_new(sizeof(void *), 5);
    gputop_gl_surfaces = array_new(sizeof(void *), 5);

    if (getenv("GPUTOP_FORCE_DEBUG_CONTEXT"))
	gputop_gl_force_debug_ctx_enabled = true;
}

static void
gputop_abort(const char *error) __attribute__((noreturn));

static void
gputop_abort(const char *error)
{
    if (gputop_ui_loop) {
	uv_idle_t idle;

	uv_idle_init(gputop_ui_loop, &idle);
	idle.data = (void *)error;
	uv_idle_start(&idle, gputop_ui_quit_idle_cb);

	for (;;)
	    ;
    }

    fprintf(stderr, "%s", error);
    fflush(stderr);
    exit(EXIT_FAILURE);
}

void *
gputop_glXGetProcAddress(const GLubyte *procName)
{
    pthread_once(&init_once, gputop_gl_init);

    if (strcmp((char *)procName, "glXCreateContextAttribsARB") == 0)
	return gputop_glXCreateContextAttribsARB;

    if (strcmp((char *)procName, "glXCreateContextWithConfigSGIX") == 0)
	return NULL;

    return real_glXGetProcAddress(procName);
}

void *
gputop_glXGetProcAddressARB(const GLubyte *procName)
{
    return gputop_glXGetProcAddress(procName);
}

static bool
have_extension(const char *name)
{
    const char *gl_extensions = (const char *)glGetString(GL_EXTENSIONS);
    int n_extensions = 0;
    int i;

    /* Note: we're not currently being careful to consider extension
     * names that are an abbreviation of another name... */
    if (gl_extensions)
	return strstr(gl_extensions, name);

    if (glGetError() != GL_INVALID_ENUM || pfn_glGetStringi == NULL)
	gputop_abort("Spurious NULL from glGetString(GL_EXTENSIONS)");

    /* If we got GL_INVALID_ENUM lets just assume we have a core
     * profile context so we need to use glGetStringi() */
    glGetIntegerv(GL_NUM_EXTENSIONS, &n_extensions);
    if (!n_extensions)
	gputop_abort("glGetIntegerv(GL_NUM_EXTENSIONS) returned zero");

    for (i = 0; i < n_extensions; i++)
	if (strcmp(name, (char *)pfn_glGetStringi(GL_EXTENSIONS, i)) == 0)
	    return true;
    return false;
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

	SYM(glEnable),
	SYM(glDisable),

	/* KHR_debug */
	SYM(glDebugMessageControl),
	SYM(glDebugMessageCallback),

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
    int i;

    /* Special case since we may need this to check the extensions... */
    pfn_glGetStringi = (void *)glXGetProcAddress((GLubyte *)"glGetStringi");

    gputop_has_intel_performance_query_ext =
	have_extension("GL_INTEL_performance_query");

    gputop_has_khr_debug_ext = have_extension("GL_KHR_debug");

    for (i = 0; i < sizeof(symbols) / sizeof(symbols[0]); i++)
	*(symbols[i].ptr) = glXGetProcAddress((GLubyte *)symbols[i].name);
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
gputop_khr_debug_callback(GLenum source,
			  GLenum type,
			  GLuint id,
			  GLenum gl_severity,
			  GLsizei length,
			  const GLchar *message,
			  void *userParam)
{
    int level = GPUTOP_LOG_LEVEL_NOTIFICATION;

    switch (gl_severity) {
    case GL_DEBUG_SEVERITY_HIGH:
	level = GPUTOP_LOG_LEVEL_HIGH;
	break;
    case GL_DEBUG_SEVERITY_MEDIUM:
	level = GPUTOP_LOG_LEVEL_MEDIUM;
	break;
    case GL_DEBUG_SEVERITY_LOW:
	level = GPUTOP_LOG_LEVEL_LOW;
	break;
    case GL_DEBUG_SEVERITY_NOTIFICATION:
	level = GPUTOP_LOG_LEVEL_NOTIFICATION;
	break;
    }

    gputop_ui_log(level, message, length);
}

static void
winsys_context_gl_initialise(struct winsys_context *wctx)
{
    unsigned int query_id;
    bool ok;

    pthread_once(&initialise_gl_once, initialise_gl);

    /* NB: we need to be paranoid about leaking GL errors to the application */

    ok = true;
    pfn_glGetPerfQueryIdByNameINTEL("Gen7 3D Observability Architecture Counters",
				    &query_id);
    while (pfn_glGetError() != GL_NO_ERROR)
	ok = false;

    if (ok)
	get_query_info(query_id, &wctx->oa_query_info);

    ok = true;
    pfn_glGetPerfQueryIdByNameINTEL("Gen7 Pipeline Statistics Registers",
				    &query_id);
    while (pfn_glGetError() != GL_NO_ERROR)
	ok = false;

    if (ok)
	get_query_info(query_id, &wctx->pipeline_stats_query_info);


    pfn_glDebugMessageControl(GL_DONT_CARE, /* source */
			      GL_DONT_CARE, /* type */
			      GL_DONT_CARE, /* severity */
			      0,
			      NULL,
			      false);

    pfn_glDebugMessageControl(GL_DONT_CARE, /* source */
			      GL_DEBUG_TYPE_PERFORMANCE,
			      GL_DONT_CARE, /* severity */
			      0,
			      NULL,
			      true);

    pfn_glDisable(GL_DEBUG_OUTPUT);
    wctx->khr_debug_enabled = false;

    pfn_glDebugMessageCallback((GLDEBUGPROC)gputop_khr_debug_callback, wctx);

}

static struct winsys_context *
winsys_context_create(GLXContext glx_ctx)
{
    struct winsys_context *wctx = xmalloc0(sizeof(struct winsys_context));

    wctx->glx_ctx = glx_ctx;
    wctx->gl_initialised = false;
    wctx->ref = 1;

    pthread_rwlock_wrlock(&gputop_gl_lock);
    array_append(gputop_gl_contexts, &wctx);
    pthread_rwlock_unlock(&gputop_gl_lock);

    return wctx;
}

GLXContext
gputop_glXCreateContextAttribsARB(Display *dpy,
				  GLXFBConfig config,
				  GLXContext share_context,
				  Bool direct,
				  const int *attrib_list)
{
    GLXContext glx_ctx;
    bool is_debug_context = false;
    struct winsys_context *wctx;
    int i;

    pthread_once(&init_once, gputop_gl_init);

    if (gputop_gl_force_debug_ctx_enabled) {
	int n_attribs;
	int flags_index;
	int *attribs_copy;

	for (n_attribs = 0; attrib_list[n_attribs] != None; n_attribs += 2)
	    ;

	attribs_copy = alloca(sizeof(int) * n_attribs + 3);
	memcpy(attribs_copy, attrib_list, sizeof(int) * (n_attribs + 1));

	for (flags_index = 0;
	     flags_index < n_attribs && attrib_list[flags_index] != GLX_CONTEXT_FLAGS_ARB;
	     flags_index += 2)
	    ;

	if (flags_index == n_attribs) {
	    attribs_copy[n_attribs++] = GLX_CONTEXT_FLAGS_ARB;
	    attribs_copy[n_attribs++] = 0;
	    attribs_copy[n_attribs] = None;
	}

	attribs_copy[flags_index + 1] = GLX_CONTEXT_DEBUG_BIT_ARB;

	attrib_list = attribs_copy;
	is_debug_context = true;
    }

    for (i = 0; attrib_list[i] != None; i += 2) {
	if (attrib_list[i] == GLX_CONTEXT_FLAGS_ARB &&
	    attrib_list[i+1] & GLX_CONTEXT_DEBUG_BIT_ARB)
	{
	    is_debug_context = true;
	}
    }

    glx_ctx = real_glXCreateContextAttribsARB(dpy, config, share_context,
					      direct, attrib_list);
    if (!glx_ctx)
	return glx_ctx;

    wctx = winsys_context_create(glx_ctx);
    wctx->is_debug_context = is_debug_context;

    return glx_ctx;
}

GLXContext
gputop_glXCreateNewContext(Display *dpy, GLXFBConfig config,
			  int render_type, GLXContext share_list, Bool direct)
{
    int attrib_list[] = { GLX_RENDER_TYPE, render_type, None };

    return gputop_glXCreateContextAttribsARB(dpy, config, share_list,
					     direct, attrib_list);
}

static GLXContext
try_create_new_context(Display *dpy, XVisualInfo *vis,
		       GLXContext shareList, Bool direct)
{
    int attrib_list[3] = { GLX_FBCONFIG_ID, None, None };
    int n_configs;
    int fb_config_id;
    GLXFBConfig *configs;
    GLXContext glx_ctx = NULL;

    glXGetConfig(dpy, vis, GLX_FBCONFIG_ID, &fb_config_id);
    attrib_list[1] = fb_config_id;

    configs = glXChooseFBConfig(dpy, vis->screen,
				attrib_list, &n_configs);

    if (n_configs == 1) {
	glx_ctx = gputop_glXCreateNewContext(dpy, configs[0], GLX_RGBA,
					     shareList, direct);
    }

    XFree(configs);

    return glx_ctx;
}

GLXContext
gputop_glXCreateContext(Display *dpy, XVisualInfo *vis,
			GLXContext shareList, Bool direct)
{
    GLXContext glx_ctx;
    struct winsys_context *wctx;

    /* We'd rather be able to use glXCreateContextAttribsARB() so that
     * we can optionally create a debug context, but sometimes it's
     * not possible to map a visual to an fbconfig. */
    glx_ctx = try_create_new_context(dpy, vis, shareList, direct);
    if (glx_ctx)
	return glx_ctx;

    pthread_once(&init_once, gputop_gl_init);

    glx_ctx = real_glXCreateContext(dpy, vis, shareList, direct);
    if (!glx_ctx)
	return NULL;

    wctx = winsys_context_create(glx_ctx);
    wctx->try_create_new_context_failed = true;

    return glx_ctx;
}

static struct winsys_context *
winsys_context_lookup(GLXContext glx_ctx, int *idx)
{
    struct winsys_context **contexts = gputop_gl_contexts->data;
    int i;

    for (i = 0; i < gputop_gl_contexts->len; i++) {
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

    array_remove(gputop_gl_contexts, idx);

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

    pthread_once(&init_once, gputop_gl_init);

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

    pthread_rwlock_wrlock(&gputop_gl_lock);
    array_append(gputop_gl_surfaces, &wsurface);
    pthread_rwlock_unlock(&gputop_gl_lock);

    return wsurface;
}

static struct winsys_surface *
get_wsurface(struct winsys_context *wctx, GLXWindow glx_window)
{
    struct winsys_surface **surfaces = gputop_gl_surfaces->data;
    struct winsys_surface *wsurface;
    int i;

    for (i = 0; i < gputop_gl_surfaces->len; i++) {
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
make_context_current(Display *dpy,
		     GLXDrawable draw, GLXDrawable read,
		     GLXContext glx_ctx)
{
    struct winsys_context *prev_wctx;
    struct winsys_context *wctx;
    int wctx_idx;
    Bool ret;

    pthread_once(&init_once, gputop_gl_init);

    prev_wctx = pthread_getspecific(winsys_context_key);

    ret = real_glXMakeContextCurrent(dpy, draw, read, glx_ctx);
    if (!ret)
	return ret;

    wctx = winsys_context_lookup(glx_ctx, &wctx_idx);
    if (glx_ctx && !wctx) {
	gputop_abort("Spurious glXMakeCurrent with unknown glx context\n"
		     "\n"
		     "GPU Top may be missing support for some new GLX API for creating contexts?\n");
    }

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
    return make_context_current(dpy, drawable, drawable, glx_ctx);
}

Bool
gputop_glXMakeContextCurrent(Display *dpy, GLXDrawable draw,
			    GLXDrawable read, GLXContext ctx)
{
    return make_context_current(dpy, draw, read, ctx);
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
    if (wctx->oa_query_info.id && !frame->oa_query) {
	GE(pfn_glCreatePerfQueryINTEL(wctx->oa_query_info.id, &frame->oa_query));
	wsurface->has_monitors = true;
	atomic_fetch_add(&gputop_gl_n_monitors, 1);
    }

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

static void
winsys_surface_destroy_monitors(struct winsys_surface *wsurface)
{
    int i;

    if (!wsurface->has_monitors)
	return;

    for (i = 0; i < MAX_FRAME_QUERIES; i++) {
	struct frame_query *frame = &wsurface->frames[i];

	pthread_rwlock_wrlock(&frame->lock);

	if (frame->oa_query) {
	    GE(pfn_glDeletePerfQueryINTEL(frame->oa_query));
	    frame->oa_query = 0;

	    atomic_fetch_sub(&gputop_gl_n_monitors, 1);
	}

	pthread_rwlock_unlock(&frame->lock);
    }

    atomic_store(&wsurface->finished_frames, wsurface->started_frames);
    wsurface->has_monitors = false;
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
    bool monitoring_enabled;
    bool khr_debug_enabled;

    pthread_once(&init_once, gputop_gl_init);

    wctx = pthread_getspecific(winsys_context_key);
    if (!wctx)
	gputop_abort("gputop can't support applications calling glXSwapBuffers without a current context");

    wsurface = get_wsurface(wctx, drawable);

    if (wsurface->wctx != wctx)
	gputop_abort("gputop can't support applications calling glXSwapBuffers with a drawable not bound to calling thread's current context");

    /* XXX: is checking wsurface->wctx->draw_surface == wsurface
     * redundant? */

    khr_debug_enabled = atomic_load(&gputop_gl_khr_debug_enabled);

    if (khr_debug_enabled != wctx->khr_debug_enabled) {
	if (khr_debug_enabled)
	    pfn_glEnable(GL_DEBUG_OUTPUT);
	else
	    pfn_glDisable(GL_DEBUG_OUTPUT);
	wctx->khr_debug_enabled = khr_debug_enabled;
    }

    monitoring_enabled = atomic_load(&gputop_gl_monitoring_enabled);

    /* XXX: check the semantics of atomic_load() will it ensure
     * the compiler won't perform multiple loads? */

    if (!monitoring_enabled)
	winsys_surface_destroy_monitors(wsurface);

    if (monitoring_enabled)
	winsys_surface_end_frame(wsurface);

    real_glXSwapBuffers(dpy, drawable);

    if (monitoring_enabled) {
	winsys_surface_check_for_finished_frames(wsurface);
	winsys_surface_start_frame(wsurface);
    }
}

void
gputop_glEnable(GLenum cap)
{
    if (cap == GL_DEBUG_OUTPUT) {
	dbg("Ignoring application's conflicting use of KHR_debug extension");
	return;
    }

    pfn_glEnable(cap);
}

void
gputop_glDisable(GLenum cap)
{
    pfn_glDisable(cap);
}

void
gputop_glDebugMessageControl(GLenum source,
			     GLenum type,
			     GLenum severity,
			     GLsizei count,
			     const GLuint *ids,
			     GLboolean enabled)
{
    dbg("Ignoring application's conflicting use of KHR_debug extension");
}

void
gputop_glDebugMessageCallback(GLDEBUGPROC callback,
			      const void *userParam)
{
    dbg("Ignoring application's conflicting use of KHR_debug extension");
}

