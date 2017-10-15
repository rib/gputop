/*
 * GPU Top
 *
 * Copyright (C) 2017 Intel Corporation
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

#ifndef __GPUTOP_UI_UTILS_H__
#define __GPUTOP_UI_UTILS_H__

#include <stdint.h>

#include "imgui.h"

enum Colors {
    GputopCol_OaGraph,
    GputopCol_TopologySlice,
    GputopCol_TopologySubslice,
    GputopCol_TopologySubsliceDis,
    GputopCol_TopologyEu,
    GputopCol_TopologyEuDis,
    GputopCol_TopologyText,
    GputopCol_MultilineHover,
    GputopCol_TimelineZoomLeft,
    GputopCol_TimelineZoomRight,
    GputopCol_TimelineEventSelect,
    GputopCol_CounterSelectedText,

    GputopCol_COUNT
};

enum Properties {
    GputopProp_TimelineScrollScaleFactor,
    GputopProp_TopologySliceHeight,
    GputopProp_TopologySliceSpacing,
    GputopProp_TopologySubsliceSpacing,

    GputopProp_COUNT
};

namespace Gputop {

void InitColorsProperties();
ImU32 GetColor(enum Colors color);
int GetProperty(enum Properties prop);
void DisplayColorsProperties();

};

#endif /* __GPUTOP_UI_UTILS_H__ */
