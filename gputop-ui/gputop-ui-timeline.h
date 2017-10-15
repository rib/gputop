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

#ifndef __GPUTOP_UI_TIMELINE_H__
#define __GPUTOP_UI_TIMELINE_H__

#include <stdint.h>

#include "imgui.h"

namespace Gputop {

enum TimelineAction {
    TIMELINE_ACTION_NONE,
    TIMELINE_ACTION_SELECT,
    TIMELINE_ACTION_DRAG,
    TIMELINE_ACTION_ZOOM_IN,
    TIMELINE_ACTION_ZOOM_OUT,
};

void BeginTimeline(const char *label, int rows, int events,
                   uint64_t length, ImVec2 timeline_size = ImVec2(0,0));
bool TimelineItem(int row, uint64_t start, uint64_t end, bool selected = false);
bool TimelineEvent(int event, uint64_t time, bool selected = false);
bool EndTimeline(const char **units = NULL, int n_units = 0,
                 const char *row_labels[] = NULL,
                 int64_t *zoom_start = NULL, uint64_t *zoom_end = NULL);

ImColor GetTimelineRowColor(int row, int n_rows);

};

#endif /* __GPUTOP_UI_TIMELINE_H__ */
