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

#ifndef __GPUTOP_UI_MULTILINES_H__
#define __GPUTOP_UI_MULTILINES_H__

#include <stdint.h>

#include "imgui.h"

namespace Gputop {

void PlotMultilines(const char* label,
                    float (*values_getter)(void* data, int line, int idx), void* data,
                    int lines, int values_count, int values_offset,
                    const ImColor *colors,
                    const char* overlay_text = NULL,
                    float scale_min = FLT_MAX, float scale_max = FLT_MAX,
                    ImVec2 graph_size = ImVec2(0,0));

} // namespace Gputop

#endif /* __GPUTOP_UI_MULTILINES_H__ */
