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


#pragma once

#ifndef EMSCRIPTEN
#include <asm/ioctl.h>
#endif

#include <stdint.h>

enum drm_i915_oa_format {
	I915_OA_FORMAT_A13	    = 0, /* HSW only */
	I915_OA_FORMAT_A29	    = 1, /* HSW only */
	I915_OA_FORMAT_A13_B8_C8    = 2, /* HSW only */
	I915_OA_FORMAT_B4_C8	    = 4, /* HSW only */
	I915_OA_FORMAT_A45_B8_C8    = 5, /* HSW only */
	I915_OA_FORMAT_B4_C8_A16    = 6, /* HSW only */
	I915_OA_FORMAT_C4_B8	    = 7, /* HSW+ */

	/* Gen8+ */
	I915_OA_FORMAT_A12		    = 8,
	I915_OA_FORMAT_A12_B8_C8	    = 9,
	I915_OA_FORMAT_A32u40_A4u32_B8_C8   = 10,

	I915_OA_FORMAT_MAX	    /* non-ABI */
};

enum drm_i915_oa_set {
	I915_OA_METRICS_SET_3D			= 1,
	I915_OA_METRICS_SET_COMPUTE		= 2,
	I915_OA_METRICS_SET_COMPUTE_EXTENDED	= 3,
	I915_OA_METRICS_SET_MEMORY_READS	= 4,
	I915_OA_METRICS_SET_MEMORY_WRITES	= 5,
	I915_OA_METRICS_SET_SAMPLER_BALANCE	= 6,

	I915_OA_METRICS_SET_MAX			/* non-ABI */
};

#define I915_OA_METRICS_SET_3D			1
#define I915_OA_METRICS_SET_COMPUTE		2
#define I915_OA_METRICS_SET_COMPUTE_EXTENDED	3
#define I915_OA_METRICS_SET_MEMORY_READS	4
#define I915_OA_METRICS_SET_MEMORY_WRITES	5
#define I915_OA_METRICS_SET_SAMPLER_BALANCE	6
#define I915_OA_METRICS_SET_MAX			I915_OA_METRICS_SET_SAMPLER_BALANCE

#define I915_OA_ATTR_SIZE_VER0		32  /* sizeof first published struct */

typedef struct _i915_oa_attr {
	uint32_t size;

	uint32_t format;
	uint32_t metrics_set;
	uint32_t timer_exponent;

	uint32_t drm_fd;
	uint32_t ctx_id;

	uint64_t single_context : 1,
	      __reserved_1 : 63;
} i915_oa_attr_t;

/* Header for PERF_RECORD_DEVICE type events */
typedef struct _i915_oa_event_header {
	uint32_t type;
	uint32_t __reserved_1;
} i915_oa_event_header_t;

enum i915_oa_event_type {

	/*
	 * struct {
	 *	struct perf_event_header	header;
	 *	drm_i915_oa_event_header_t	i915_oa_header;
	 * };
	 */
	I915_OA_RECORD_BUFFER_OVERFLOW		= 1,

	/*
	 * struct {
	 *	struct perf_event_header	header;
	 *	drm_i915_oa_event_header_t	i915_oa_header;
	 * };
	 */
	I915_OA_RECORD_REPORT_LOST		= 2,

	I915_OA_RECORD_MAX,			/* non-ABI */
};

#define I915_PARAM_SUBSLICE_TOTAL	33
#define I915_PARAM_EU_TOTAL		34
#define I915_PARAM_SLICE_MASK		37
#define I915_PARAM_SUBSLICE_MASK	38

typedef struct i915_getparam {
	int param;
	int *value;
} i915_getparam_t;


/* Note: same versioning scheme as struct perf_event_attr
 *
 * Userspace specified size defines ABI version and kernel
 * zero extends to size of latest version. If userspace
 * gives a larger structure than the kernel expects then
 * kernel asserts that all unknown fields are zero.
 */

#define I915_OA_FLAG_PERIODIC		(1<<0)

struct i915_perf_oa_attr {
	uint32_t size;

	uint32_t flags;

	uint32_t metrics_set;
	uint32_t oa_format;
	uint32_t oa_timer_exponent;
};


enum i915_perf_event_type {
	I915_PERF_OA_EVENT
};

#define I915_PERF_FLAG_FD_CLOEXEC	(1<<0)
#define I915_PERF_FLAG_FD_NONBLOCK	(1<<1)
#define I915_PERF_FLAG_SINGLE_CONTEXT	(1<<2)

#define I915_PERF_SAMPLE_OA_REPORT	(1<<0)
#define I915_PERF_SAMPLE_CTXID		(1<<1)
#define I915_PERF_SAMPLE_TIMESTAMP	(1<<2)

struct i915_perf_open_param {
	/* Such as I915_PERF_OA_EVENT */
	uint32_t type;

	/* CLOEXEC, NONBLOCK, SINGLE_CONTEXT, PERIODIC... */
	uint32_t flags;

	/* What to include in samples */
	uint64_t sample_flags;

	/* A specific context to profile */
	uint32_t ctx_id;

	/* Event specific attributes struct pointer:
	 * (E.g. to struct i915_perf_oa_attr)
	 */
	uint64_t attr;

	/* OUT */
	uint32_t fd;
};


/* Note: same as struct perf_event_header */
struct i915_perf_event_header {
	uint32_t type;
	uint16_t misc;
	uint16_t size;
};

enum i915_perf_record_type {

	/*
	 * struct {
	 *     struct drm_i915_perf_event_header header;
	 *
	 *     { u32 ctx_id; }	    && I915_PERF_SAMPLE_CTXID
	 *     { u32 timestamp; }   && I915_PERF_SAMPLE_TIMESTAMP
	 *     { u32 oa_report[]; } && I915_PERF_SAMPLE_OA_REPORT
	 *
	 * };
	 */
	DRM_I915_PERF_RECORD_SAMPLE = 1,

	/*
	 * Indicates that one or more OA reports was not written
	 * by the hardware.
	 */
	DRM_I915_PERF_RECORD_OA_REPORT_LOST = 2,

	/*
	 * Indicates that the internal circular buffer that Gen
	 * graphics writes OA reports into has filled, which may
	 * either mean that old reports could be overwritten or
	 * subsequent reports lost until the buffer is cleared.
	 */
	DRM_I915_PERF_RECORD_OA_BUFFER_OVERFLOW = 3,

	DRM_I915_PERF_RECORD_MAX /* non-ABI */
};


#ifndef EMSCRIPTEN
#define I915_IOCTL_GETPARAM         _IOWR('d', 0x46, i915_getparam_t)
#define I915_IOCTL_PERF_OPEN        _IOWR('d', 0x76, struct i915_perf_open_param)

#define I915_PERF_IOCTL_ENABLE      _IO('i', 0x0)
#define I915_PERF_IOCTL_DISABLE     _IO('i', 0x1)
#endif
