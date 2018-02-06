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

#define IMGUI_DEFINE_MATH_OPERATORS

#include <inttypes.h>

#include "imgui.h"
#include "imgui_internal.h"

#include "gputop-ui-timeline.h"
#include "gputop-ui-utils.h"

#include "util/macros.h"

using namespace ImGui;

namespace Gputop {

static int timeline_n_events = 1;
static int timeline_n_rows = 1;
static float timeline_row_height = 1;
static ImRect timeline_inner_bb;
static ImRect timeline_frame_bb;
static uint64_t timeline_length = 1;
static ImVec2 timeline_zoom_pos = ImVec2(-1.0f, -1.0f);
static ImVec2 timeline_range_pos = ImVec2(-1.0f, -1.0f);
static ImGuiID timeline_id = 0;
static bool timeline_item_hovered = false;
static float timeline_last_item_pos = -1.0f;
static float timeline_last_event_pos = -1.0f;

static bool
RangeDragBehavior(const ImRect& frame_bb, ImGuiID id, ImVec2* v, bool prev_in_zoom, bool modifier)
{
    ImGuiContext& g = *GImGui;
    bool in_zoom = false;

    // Process clicking on the drag
    if (g.ActiveId == id)
    {
        if (modifier && g.IO.MouseClicked[0])
        {
            // Lock current value on click
            v->x = ImClamp(g.IO.MousePos.x, frame_bb.GetTL().x, frame_bb.GetBR().x);
            v->y = v->x;
            in_zoom = true;
        }
        else
        {
            if (prev_in_zoom)
            {
                v->y = ImClamp(g.IO.MousePos.x, frame_bb.GetTL().x, frame_bb.GetBR().x);

                in_zoom = !g.IO.MouseReleased[0];
            }
        }
    }

    return in_zoom;
}

static bool
DragBehavior(const ImRect& frame_bb, ImGuiID id, ImVec2* v)
{
    ImGuiContext& g = *GImGui;
    bool in_drag = false;

    // Process clicking on the drag
    if (g.ActiveId == id)
    {
        if (g.IO.MouseDown[0])
        {
            *v = g.IO.MouseDelta;
            in_drag = true;
        }
    }

    return in_drag;
}

void BeginTimeline(const char *label, int rows, int events, uint64_t length, ImVec2 timeline_size)
{
    timeline_length = length;

    ImGuiWindow* window = GetCurrentWindow();
    if (window->SkipItems)
        return;

    ImGuiContext& g = *GImGui;
    const ImGuiStyle& style = g.Style;
    timeline_id = window->GetID(label);

    const float ruler_height = 5.0f; /* TODO: Ruler */
    const ImVec2 label_size = CalcTextSize("99999"); /* Top & bottom labels */
    const float default_row_height = label_size.y; /* TODO: make this configurable */
    if (timeline_size.x == 0.0f)
        timeline_size.x = CalcItemWidth();
    if (timeline_size.y == 0.0f)
        timeline_size.y = ruler_height + (label_size.y * 2) + rows * default_row_height + (style.FramePadding.y * 2);

    const ImRect frame_bb(window->DC.CursorPos, window->DC.CursorPos + timeline_size);
    const ImRect inner_bb(frame_bb.Min + style.FramePadding + ImVec2(0, ruler_height + label_size.y), frame_bb.Max - style.FramePadding - ImVec2(0, label_size.y));
    const ImRect total_bb(frame_bb.Min, frame_bb.Max);
    ItemSize(total_bb, style.FramePadding.y);
    if (!ItemAdd(total_bb, timeline_id))
        return;

    timeline_inner_bb = inner_bb;
    timeline_frame_bb = frame_bb;
    timeline_row_height = inner_bb.GetHeight() / rows;
    timeline_n_rows = rows;
    timeline_n_events = events;
    timeline_item_hovered = false;
    timeline_last_event_pos = -1.0f;
    timeline_last_item_pos = -1.0f;
}

bool TimelineItem(int row, uint64_t start, uint64_t end, bool selected)
{
    ImColor color(GetHueColor(row, timeline_n_rows));

    ImGuiWindow* window = GetCurrentWindow();
    double width = timeline_inner_bb.GetWidth();
    ImRect item(timeline_inner_bb.GetTL().x + (double) start * width / timeline_length,
                timeline_inner_bb.GetTL().y + timeline_row_height * row,
                timeline_inner_bb.GetTL().x + (double) end * width / timeline_length,
                timeline_inner_bb.GetTL().y + timeline_row_height * (row + 1));
    const bool should_draw = fabs(timeline_last_item_pos - item.GetTL().x) >= 0.75;

    if (should_draw)
    {
        window->DrawList->AddRectFilled(item.GetTL(), item.GetBR(), color);
        if (selected)
          window->DrawList->AddRect(item.GetTL(), item.GetBR(), GetColorU32(ImGuiCol_Text));
        timeline_last_item_pos = item.GetTL().x;
    }

    const bool hovered = ItemHoverable(item, timeline_id);

    if (hovered)
      timeline_item_hovered = true;

    return hovered;
}

bool TimelineEvent(int event, uint64_t time, bool selected)
{
    ImColor color(GetHueColor(event, timeline_n_events));

    ImGuiWindow* window = GetCurrentWindow();
    double width = timeline_inner_bb.GetWidth();
    double pos = timeline_inner_bb.GetTL().x + (double) time * width / timeline_length;
    float top = timeline_inner_bb.GetTL().y, bottom = timeline_inner_bb.GetBL().y;
    ImRect item(pos, top, pos, bottom);
    const bool should_draw = fabs(timeline_last_event_pos - pos) >= 0.75;

    if (should_draw) {
        window->DrawList->AddLine(item.GetTL(), item.GetBR(), color);
        timeline_last_event_pos = pos;
    }

    item.Expand(ImVec2(3.0f, 0.0f));

    ImGuiContext& g = *GImGui;
    const bool hovered = ItemHoverable(item, timeline_id);

    if ((should_draw && hovered && !timeline_item_hovered) || selected)
    {
        window->DrawList->AddCircleFilled(ImVec2(item.GetTL().x + 2.0f,
                                                 ImClamp(g.IO.MousePos.y, top, bottom)), 5.0f,
                                          GetColor(GputopCol_TimelineEventSelect));
        timeline_item_hovered = true;
    }

    return hovered;
}

static void DrawRange(const ImVec2& range, const char **units, int n_units)
{
    ImGuiWindow* window = GetCurrentWindow();

    ImRect rect(ImMin(range.x, range.y),
                timeline_frame_bb.GetTL().y,
                ImMax(range.x, range.y),
                timeline_frame_bb.GetBR().y);
    window->DrawList->AddRectFilledMultiColor(rect.GetTL(), rect.GetBR(),
                                              GetColor(GputopCol_TimelineZoomLeft),
                                              GetColor(GputopCol_TimelineZoomRight),
                                              GetColor(GputopCol_TimelineZoomRight),
                                              GetColor(GputopCol_TimelineZoomLeft));

    float tl_start = timeline_inner_bb.GetTL().x;
    float tl_width = timeline_inner_bb.GetWidth();
    double range_length =
        (double) timeline_length * (ImMax(range.x, range.y) - tl_start) / tl_width -
        (double) timeline_length * (ImMin(range.x, range.y) - tl_start) / tl_width;
    int unit_idx = 0;
    while (range_length >= 1000.0f && unit_idx < (n_units - 1))
    {
        range_length /= 1000.0f;
        unit_idx++;
    }

    char range_str[20];
    snprintf(range_str, sizeof(range_str), "%.3f%s", range_length, units[unit_idx]);

    const ImVec2 label_size = CalcTextSize(range_str);

    window->DrawList->AddLine(rect.GetBL() - ImVec2(0, label_size.y),
                              rect.GetBR() - ImVec2(0, label_size.y),
                              GetColorU32(ImGuiCol_Text));

    window->DrawList->AddText(ImVec2((rect.GetBL().x + rect.GetBR().x) / 2, rect.GetBL().y) -
                              ImVec2(label_size.x / 2, label_size.y),
                              GetColorU32(ImGuiCol_Text), range_str);
}

#ifndef ALIGN
#define ALIGN(x, align) (((x) % (align)) == 0 ? (x) : ((x) + ((align) - (x) % (align))))
#endif

static float ToPixelSize(uint64_t value)
{
    return (double) timeline_inner_bb.GetWidth() * value / timeline_length;
}

static uint64_t ToLength(float value)
{
    return (double) value * timeline_length / timeline_inner_bb.GetWidth();
}

static uint64_t NextLength(uint64_t value)
{
    if (value % 2 == 0)
        return value / 2;
    if (value % 5 == 0)
        return value / 5;
    return 0;
}

bool EndTimeline(const char **units, int n_units,
                 const char **row_labels,
                 int64_t *zoom_start, uint64_t *zoom_end)
{
    ImGuiWindow* window = GetCurrentWindow();
    if (window->SkipItems)
        return false;

    ImGuiContext& g = *GImGui;
    bool hovered = ItemHoverable(timeline_frame_bb, timeline_id);
    const bool clicked = hovered && IsMouseClicked(0);
    if (clicked)
    {
        SetActiveID(timeline_id, window);
        FocusWindow(window);
    }

    /* Row labels */
    if (row_labels)
    {
        for (int row = 0; row < timeline_n_rows; row++)
        {
            const ImVec2 label_size = CalcTextSize(row_labels[row]); /* Top & bottom labels */
            ImVec2 label_pos(timeline_inner_bb.GetTL().x,
                             timeline_inner_bb.GetTL().y + timeline_row_height * row +
                             timeline_row_height / 2 - label_size.y / 2);
            window->DrawList->AddText(label_pos, GetColorU32(ImGuiCol_Text), row_labels[row]);
        }
    }

    /* Ruler */
    {
        struct {
            uint64_t divider;
            bool enabled;
            uint64_t increment;
            uint64_t window;
            float px_increment;
        } ruler_levels[4] = {
            { 1ULL, false, 0, 0, 0.0f, },
            { 1000ULL, false, 0, 0, 0.0f, },
            { 1000000ULL, false, 0, 0, 0.0f, },
            { 1000000000ULL, false, 0, 0, 0.0f, }
        };
        const ImVec2 label_size = CalcTextSize("9999999");
        const uint64_t label_length = ToLength(label_size.x);
        uint64_t current_length = timeline_length;
        for (int u = n_units - 1; u >= 0; u--)
        {
            if (ruler_levels[u].divider > current_length ||
                (ruler_levels[u].divider * 500) < label_length)
                continue;

            ruler_levels[u].window = ALIGN(current_length, ruler_levels[u].divider);
            current_length = 1000ULL;
            while (current_length >= 2 &&
                   (NextLength(current_length) * ruler_levels[u].divider) > label_length)
                current_length = NextLength(current_length);
            current_length *= ruler_levels[u].divider;

            ruler_levels[u].enabled = true;
            ruler_levels[u].increment = current_length;
            ruler_levels[u].px_increment = ToPixelSize(ruler_levels[u].increment);
        }

        /* Now draw at the specified intervals */
        bool zero_done = false;
        for (int u = n_units - 1; u >= 0; u--)
        {
            if (!ruler_levels[u].enabled)
                continue;

            for (int i = 0; i * ruler_levels[u].px_increment < timeline_inner_bb.Max.x; i++)
            {
                if (i == 0 && !zero_done)
                    zero_done = true;
                else if ((ruler_levels[u].increment * i) % ruler_levels[u].window == 0)
                    continue;

                float marker_pos = timeline_inner_bb.Min.x + i * ruler_levels[u].px_increment;
                ImRect line(marker_pos, timeline_frame_bb.Min.y,
                            marker_pos, timeline_frame_bb.Min.y + 5.0f);
                window->DrawList->AddLine(line.GetTL(), line.GetBR(), ImColor(255, 255, 255));

                uint64_t value = ((i * ruler_levels[u].increment) % ruler_levels[u].window) / ruler_levels[u].divider;
                char ruler_value[20];
                snprintf(ruler_value, sizeof(ruler_value), "%" PRIu64 "%s", value, units[u]);
                window->DrawList->AddText(line.GetBR(), GetColorU32(ImGuiCol_Text), ruler_value);
            }
        }
    }

    bool zoom_changed = false;
    bool in_zoom = false;
    bool in_range = false;
    bool need_clear = false;
    if (g.ActiveId == timeline_id)
    {
        /* Zoom */
        static bool last_in_zoom = false;
        in_zoom = RangeDragBehavior(timeline_inner_bb, timeline_id,
                                    &timeline_zoom_pos, last_in_zoom, g.IO.KeyCtrl);
        if (in_zoom)
            DrawRange(timeline_zoom_pos, units, n_units);

        zoom_changed = last_in_zoom && !in_zoom;
        last_in_zoom = in_zoom;

        float tl_start = timeline_inner_bb.GetTL().x;
        float tl_width = timeline_inner_bb.GetWidth();
        if (zoom_changed)
        {
            if (zoom_start)
                *zoom_start = (double) timeline_length * (ImMin(timeline_zoom_pos.x, timeline_zoom_pos.y) - tl_start) / tl_width;
            if (zoom_end)
                *zoom_end = (double) timeline_length * (ImMax(timeline_zoom_pos.x, timeline_zoom_pos.y) - tl_start) / tl_width;
        }

        /* Range */
        static bool last_in_range = false;
        in_range = RangeDragBehavior(timeline_inner_bb, timeline_id,
                                     &timeline_range_pos, last_in_range, g.IO.KeyShift);
        if (in_range)
            DrawRange(timeline_range_pos, units, n_units);

        last_in_range = in_range;

        if (hovered && !timeline_item_hovered && !last_in_zoom && !last_in_range)
        {
            SetTooltip("Ctrl-click to zoom\n"
                       "Shift-click to measure");
        }

        if (!last_in_zoom && !last_in_range)
            need_clear = true;
    }

    if (!in_zoom && !in_range && IsItemHovered() &&
        timeline_inner_bb.Contains(g.IO.MousePos) && g.IO.MouseWheel != 0.0f)
    {
        const float scroll_pos((g.IO.MousePos.x - timeline_inner_bb.GetTL().x) /
                               timeline_inner_bb.GetWidth());
        const float wheel = fabs(g.IO.MouseWheel) == 1.0f ?
            (g.IO.MouseWheel * GetProperty(GputopProp_TimelineScrollScaleFactor) / 100.0f) :
            g.IO.MouseWheel;
        int64_t delta = timeline_length * wheel;
        uint64_t length = MAX2(timeline_length - delta, 5ULL);

        if (length != timeline_length)
        {
            int64_t start = delta * scroll_pos;

            if (zoom_start) *zoom_start = start;
            if (zoom_end) *zoom_end = start + length;
            zoom_changed = true;
        }

        // Capture
        //g.IO.MouseWheel = 0.0f;
    }

    ImVec2 drag_offset(0.0f, 0.0f);
    if (!in_zoom && !in_range && DragBehavior(timeline_inner_bb, timeline_id, &drag_offset))
    {
        int64_t drag_timeline_offset =
            timeline_length * drag_offset.x / timeline_inner_bb.GetWidth();
        if (zoom_start) *zoom_start = -drag_timeline_offset;
        if (zoom_end) *zoom_end = -drag_timeline_offset + timeline_length;
        zoom_changed = true;
        need_clear = false;
    }

    if (need_clear)
        ClearActiveID();

    return zoom_changed;
}

} // namespace Gputop
