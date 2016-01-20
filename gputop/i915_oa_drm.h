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
#define I915_PARAM_SLICE_MASK		45
#define I915_PARAM_SUBSLICE_MASK	46

typedef struct i915_getparam {
	int param;
	int *value;
} i915_getparam_t;



enum i915_oa_format {
	I915_OA_FORMAT_A13 = 1,	    /* HSW only */
	I915_OA_FORMAT_A29,	    /* HSW only */
	I915_OA_FORMAT_A13_B8_C8,   /* HSW only */
	I915_OA_FORMAT_B4_C8,	    /* HSW only */
	I915_OA_FORMAT_A45_B8_C8,   /* HSW only */
	I915_OA_FORMAT_B4_C8_A16,   /* HSW only */
	I915_OA_FORMAT_C4_B8,	    /* HSW+ */

	/* Gen8+ */
	I915_OA_FORMAT_A12,
	I915_OA_FORMAT_A12_B8_C8,
	I915_OA_FORMAT_A32u40_A4u32_B8_C8,

	I915_OA_FORMAT_MAX	    /* non-ABI */
};

#define I915_PERF_FLAG_FD_CLOEXEC	(1<<0)
#define I915_PERF_FLAG_FD_NONBLOCK	(1<<1)
#define I915_PERF_FLAG_DISABLED		(1<<2)

enum i915_perf_oa_event_source {
	I915_PERF_OA_EVENT_SOURCE_UNDEFINED,
	I915_PERF_OA_EVENT_SOURCE_PERIODIC,
	I915_PERF_OA_EVENT_SOURCE_CONTEXT_SWITCH,
	I915_PERF_OA_EVENT_SOURCE_RCS,

	I915_PERF_OA_EVENT_SOURCE_MAX	/* non-ABI */
};

#define I915_PERF_MMIO_NUM_MAX	8
struct i915_perf_mmio_list {
	uint32_t num_mmio;
	uint32_t mmio_list[I915_PERF_MMIO_NUM_MAX];
};


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

	/**
	 * The value of this property set to 1 requests inclusion of sample
	 * source field to be given to userspace. The sample source field
	 * specifies the origin of OA report.
	 */
	DRM_I915_PERF_SAMPLE_OA_SOURCE_PROP,

	/**
	 * The value of this property specifies the GPU engine (ring) for which
	 * the samples need to be collected. Specifying this property also
	 * implies the command stream based sample collection.
	 */
	DRM_I915_PERF_RING_PROP,

	/**
	 * The value of this property set to 1 requests inclusion of context ID
	 * in the perf sample data.
	 */
	DRM_I915_PERF_SAMPLE_CTX_ID_PROP,

	/**
	 * The value of this property set to 1 requests inclusion of pid in the
	 * perf sample data.
	 */
	DRM_I915_PERF_SAMPLE_PID_PROP,

	/**
	 * The value of this property set to 1 requests inclusion of tag in the
	 * perf sample data.
	 */
	DRM_I915_PERF_SAMPLE_TAG_PROP,

	/**
	 * The value of this property set to 1 requests inclusion of timestamp
	 * in the perf sample data.
	 */
	DRM_I915_PERF_SAMPLE_TS_PROP,

	/**
	 * This property requests inclusion of mmio register values in the perf
	 * sample data. The value of this property specifies the address of user
	 * struct having the register addresses.
	 */
	DRM_I915_PERF_SAMPLE_MMIO_PROP,

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
	 * affect on the ordering of data within a sample. The order is
	 * documented here.
	 *
	 * struct {
	 *     struct drm_i915_perf_record_header header;
	 *
	 *     { u32 source_info; } && DRM_I915_PERF_SAMPLE_OA_SOURCE_PROP
	 *     { u32 ctx_id; } && DRM_I915_PERF_SAMPLE_CTX_ID_PROP
	 *     { u32 pid; } && DRM_I915_PERF_SAMPLE_PID_PROP
	 *     { u32 tag; } && DRM_I915_PERF_SAMPLE_TAG_PROP
	 *     { u64 timestamp; } && DRM_I915_PERF_SAMPLE_TS_PROP
	 *     { u32 mmio[]; } && DRM_I915_PERF_SAMPLE_MMIO_PROP
	 *     { u32 oa_report[]; } && DRM_I915_PERF_SAMPLE_OA_PROP
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

struct drm_i915_gem_context_create {
        /*  output: id of new context*/
        uint32_t ctx_id;
        uint32_t pad;
};

struct drm_i915_gem_context_destroy {
        uint32_t ctx_id;
        uint32_t pad;
};

#ifndef EMSCRIPTEN
#define I915_IOCTL_GEM_CONTEXT_CREATE   _IOWR('d', 0x6d, struct drm_i915_gem_context_create)
#define I915_IOCTL_GEM_CONTEXT_DESTROY  _IOWR('d', 0x6e, struct drm_i915_gem_context_destroy)

#define I915_IOCTL_GETPARAM         _IOWR('d', 0x46, i915_getparam_t)
#define I915_IOCTL_PERF_OPEN        _IOWR('d', 0x76, struct i915_perf_open_param)

#define I915_PERF_IOCTL_ENABLE      _IO('i', 0x0)
#define I915_PERF_IOCTL_DISABLE     _IO('i', 0x1)
#endif
