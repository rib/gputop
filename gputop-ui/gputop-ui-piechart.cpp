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

#include "imgui.h"
#include "imgui_internal.h"

#include "gputop-ui-topology.h"
#include "gputop-ui-utils.h"

#include <math.h>

using namespace ImGui;

namespace Gputop {

bool in_circle(const ImVec2& center, float radius, const ImVec2& mouse)
{
    const double a = mouse.x - center.x, b = mouse.y - center.y;
    return sqrt(a * a + b * b) <= radius;
}

static ImVec2 pie_center = ImVec2(0.0f, 0.0f);
static float pie_radius = 0.0f;
static float pie_focus_pos = -1.0f;
static float pie_accumulated_pos = 0.0f;
static int pie_i_value = 0;
static int pie_n_values = 1;

void BeginPieChart(int n_values, ImVec2 graph_size)
{
    ImGuiWindow* window = GetCurrentWindow();
    if (window->SkipItems)
        return;

    if (graph_size.x == 0.0f)
        graph_size.x = CalcItemWidth();
    if (graph_size.y == 0.0f)
        graph_size.y = graph_size.x;

    ImGuiContext& g = *GImGui;

    const ImRect total_bb(window->DC.CursorPos,
                          window->DC.CursorPos + ImVec2(graph_size.x, graph_size.y));
    ItemSize(total_bb, 0);
    if (!ItemAdd(total_bb, 0))
        return;

    pie_center = total_bb.GetCenter();
    pie_radius = graph_size.x / 2;
    const ImVec2 mouse(g.IO.MousePos);

    if (ItemHoverable(total_bb, 0) && in_circle(pie_center, pie_radius, mouse))
    {
        if (mouse.x <= pie_center.x)
        {
            if (mouse.y <= pie_center.y) // left-top
                pie_focus_pos = atan((pie_center.y - mouse.y) / (pie_center.x - mouse.x));
            else // left-bottom
                pie_focus_pos = 3 * M_PI / 2 + atan((pie_center.x - mouse.x) /
                                                    (mouse.y - pie_center.y));
        }
        else
        {
            if (mouse.y <= pie_center.y) // right-top
                pie_focus_pos = M_PI / 2 + atan((mouse.x - pie_center.x) /
                                                (pie_center.y - mouse.y));
            else // right-bottom
                pie_focus_pos = M_PI + atan((mouse.y - pie_center.y) /
                                            (mouse.x - pie_center.x));
        }
    } else
        pie_focus_pos = -1.0f;

    pie_i_value = 0;
    pie_n_values = n_values;
    pie_accumulated_pos = 0.0f;
}

bool PieChartItem(float value)
{
    ImGuiWindow* window = GetCurrentWindow();
    const double max_sides = 60;

    const float start = pie_accumulated_pos * 2 * M_PI;
    const float end = (pie_accumulated_pos + value) * 2 * M_PI;
    const bool hovered = (pie_focus_pos >= start && pie_focus_pos <= end);

    assert(pie_i_value < pie_n_values);

    window->DrawList->PathLineTo(pie_center);
    window->DrawList->PathArcTo(pie_center, pie_radius,
                                M_PI + start, M_PI + end,
                                ImMax(value * max_sides, 4.0f));
    window->DrawList->PathLineTo(pie_center);
    window->DrawList->PathFillConvex(GetHueColor(pie_i_value,
                                                 pie_n_values, hovered ? 0.8f : 0.5f));

    pie_accumulated_pos += value;
    pie_i_value++;

    return hovered;
}


void EndPieChart()
{
    pie_i_value = 0;
    pie_n_values = 0;
    pie_accumulated_pos = 0.0f;
}

} // namespace Gputop
