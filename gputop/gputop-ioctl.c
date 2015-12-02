#define _GNU_SOURCE
#include <dlfcn.h>

#include "i915_oa_drm.h"
#include "gputop-perf.h"

int ioctl(int fd, unsigned long request, void *data)
{
    static int (*real_ioctl) (int fd, unsigned long request, void *data);
    int ret;

    if (!real_ioctl)
      real_ioctl = dlsym(RTLD_NEXT, "ioctl");

    ret = real_ioctl(fd, request, data);
    if (!ret) {
	/* Contexts are specific to the drm_file instance used in rendering.
	 * The ctx_id created by the kernel is _not_ globally
	 * unique, but rather unique for that drm_file instance.
	 * For example from the global context_list, there will be multiple
	 * default contexts (0), but actually only one of these per file instance.
	 * Therefore to properly enable OA metrics on a per-context basis, we
	 * need the file descriptor and the ctx_id. */
	if (request == I915_IOCTL_GEM_CONTEXT_CREATE) {
	    struct drm_i915_gem_context_create *ctx_create = data;
	    gputop_add_ctx_handle(fd, ctx_create->ctx_id);
	} else if (request == I915_IOCTL_GEM_CONTEXT_DESTROY) {
	    struct drm_i915_gem_context_destroy *ctx_destroy = data;
	    gputop_remove_ctx_handle(ctx_destroy->ctx_id);
	}
    }

    return ret;
}
