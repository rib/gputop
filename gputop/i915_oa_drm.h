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



#define I915_PARAM_SUBSLICE_TOTAL	33
#define I915_PARAM_EU_TOTAL		34
#define I915_PARAM_SLICE_MASK		37
#define I915_PARAM_SUBSLICE_MASK	38

typedef struct i915_getparam {
	int param;
	int *value;
} i915_getparam_t;



enum i915_oa_format {
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

enum i915_oa_set {
	I915_OA_METRICS_SET_3D = 1,

	I915_OA_METRICS_SET_COMPUTE,
	I915_OA_METRICS_SET_COMPUTE_EXTENDED,
	I915_OA_METRICS_SET_MEMORY_READS,
	I915_OA_METRICS_SET_MEMORY_WRITES,
	I915_OA_METRICS_SET_SAMPLER_BALANCE,

	I915_OA_METRICS_SET_RENDER_PIPE_PROFILE,
	I915_OA_METRICS_SET_COMPUTE_L3_CACHE,
	I915_OA_METRICS_SET_DATA_PORT_READS_COALESCING,
	I915_OA_METRICS_SET_DATA_PORT_WRITES_COALESCING,
	I915_OA_METRICS_SET_L3_1,
	I915_OA_METRICS_SET_L3_2,
	I915_OA_METRICS_SET_L3_3,
	I915_OA_METRICS_SET_L3_4,
	I915_OA_METRICS_SET_RASTERIZER_AND_PIXEL_BACKEND,
	I915_OA_METRICS_SET_SAMPLER_1,
	I915_OA_METRICS_SET_SAMPLER_2,
	I915_OA_METRICS_SET_TDL_1,
	I915_OA_METRICS_SET_TDL_2,

	I915_OA_METRICS_SET_HDC_AND_SF,
	I915_OA_METRICS_SET_SAMPLER,

	I915_OA_METRICS_SET_MAX                 /* non-ABI */
};

#define I915_PERF_FLAG_FD_CLOEXEC	(1<<0)
#define I915_PERF_FLAG_FD_NONBLOCK	(1<<1)
#define I915_PERF_FLAG_DISABLED		(1<<2)

enum i915_perf_property_id {
	/**
	 * Open the stream for a specific context handle (as used with
	 * execbuffer2). A stream opened for a specific context this way
	 * won't typically require root privileges.
	 */
	DRM_I915_PERF_CTX_HANDLE_PROP = 1,

	/**
	 * A value of 1 requests the inclusion of raw OA unit reports as
	 * part of stream samples.
	 */
	DRM_I915_PERF_SAMPLE_OA_PROP,

	/**
	 * The value specifies which set of OA unit metrics should be
	 * be configured, defining the contents of any OA unit reports.
	 */
	DRM_I915_PERF_OA_METRICS_SET_PROP,

	/**
	 * The value specifies the size and layout of OA unit reports.
	 */
	DRM_I915_PERF_OA_FORMAT_PROP,

	/**
	 * Specifying this property implicitly requests periodic OA unit
	 * sampling and (at least on Haswell) the sampling frequency is derived
	 * from this exponent as follows:
	 *
	 *   80ns * 2^(period_exponent + 1)
	 */
	DRM_I915_PERF_OA_EXPONENT_PROP,

	DRM_I915_PERF_PROP_MAX /* non-ABI */
};

struct i915_perf_open_param {
	/** CLOEXEC, NONBLOCK... */
	uint32_t flags;

	/**
	 * Pointer to array of u64 (id, value) pairs configuring the stream
	 * to open.
	 */
	uint64_t properties;

	/** The number of u64 (id, value) pairs */
	uint32_t n_properties;

	/* OUT */
	uint32_t fd;
};

/**
 * Common to all i915 perf records
 */
struct i915_perf_record_header {
	uint32_t type;
	uint16_t pad;
	uint16_t size;
};

enum i915_perf_record_type {

	/**
	 * Samples are the work horse record type whose contents are extensible
	 * and defined when opening an i915 perf stream based on the given
	 * properties.
	 *
	 * Boolean properties following the naming convention
	 * DRM_I915_PERF_SAMPLE_xyz_PROP request the inclusion of 'xyz' data in
	 * every sample.
	 *
	 * The order of these sample properties given by userspace has no
	 * affect on the ordering of data within a sample. The order will be
	 * documented here.
	 *
	 * struct {
	 *     struct drm_i915_perf_record_header header;
	 *
	 *     TODO: itemize extensible sample data here
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
