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

#ifndef EMSCRIPTEN
#define I915_IOCTL_GETPARAM         _IOWR('d', 0x46, i915_getparam_t)
#endif
