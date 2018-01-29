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

using namespace ImGui;

namespace Gputop {

static void PlotMultilinesEx(const char* label,
                             float (*values_getter)(void* data, int line, int idx), void* data,
                             int lines, int values_count, int values_offset,
                             const ImColor *colors,
                             const char* overlay_text,
                             float scale_min, float scale_max, ImVec2 graph_size)
{
    ImGuiWindow* window = GetCurrentWindow();
    if (window->SkipItems)
        return;

    ImGuiContext& g = *GImGui;
    const ImGuiStyle& style = g.Style;

    const ImVec2 label_size = CalcTextSize(label, NULL, true);
    if (graph_size.x == 0.0f)
        graph_size.x = CalcItemWidth();
    if (graph_size.y == 0.0f)
        graph_size.y = label_size.y + (style.FramePadding.y * 2);

    const ImRect frame_bb(window->DC.CursorPos, window->DC.CursorPos + ImVec2(graph_size.x, graph_size.y));
    const ImRect inner_bb(frame_bb.Min + style.FramePadding, frame_bb.Max - style.FramePadding);
    const ImRect total_bb(frame_bb.Min, frame_bb.Max + ImVec2(label_size.x > 0.0f ? style.ItemInnerSpacing.x + label_size.x : 0.0f, 0));
    ItemSize(total_bb, style.FramePadding.y);
    if (!ItemAdd(total_bb, window->GetID(label)))
        return;
    const bool hovered = ItemHoverable(inner_bb, 0);

    // Determine scale from values if not specified
    if (scale_min == FLT_MAX || scale_max == FLT_MAX)
    {
        float v_min = FLT_MAX;
        float v_max = -FLT_MAX;
        for (int i = 0; i < values_count; i++)
        {
            const float v = values_getter(data, 0, i);
            v_min = ImMin(v_min, v);
            v_max = ImMax(v_max, v);
        }
        if (scale_min == FLT_MAX)
            scale_min = v_min;
        if (scale_max == FLT_MAX)
            scale_max = v_max;
    }

    RenderFrame(frame_bb.Min, frame_bb.Max, GetColorU32(ImGuiCol_FrameBg), true, style.FrameRounding);

    if (values_count > 0)
    {
        int res_w = ImMin((int)graph_size.x, values_count) -1;
        int item_count = values_count -1;

        // Tooltip on hover
        int v_hovered = -1;
        int v_idx = -1;
        if (hovered)
        {
            const float t = ImClamp((g.IO.MousePos.x - inner_bb.Min.x) / (inner_bb.Max.x - inner_bb.Min.x), 0.0f, 0.9999f);
            v_idx = (int)(t * item_count);
            IM_ASSERT(v_idx >= 0 && v_idx < values_count);

            BeginTooltip();
            for (int l = 0; l < lines; l++)
            {
                const float v0 = values_getter(data, l, (v_idx + values_offset) % values_count);
                const float v1 = values_getter(data, l, (v_idx + 1 + values_offset) % values_count);
                ColorButton("", colors[l], ImGuiColorEditFlags_NoInputs); SameLine();
                Text("%d: %d: %8.4g - %d: %8.4g", l, v_idx, v0, v_idx+1, v1);
            }
            EndTooltip();

            v_hovered = v_idx;
        }

        const float t_step = 1.0f / (float)res_w;

        for (int l = 0; l < lines; l++)
        {
            float v0 = values_getter(data, l, (0 + values_offset) % values_count);
            float t0 = 0.0f;
            ImVec2 tp0 = ImVec2( t0, 1.0f - ImSaturate((v0 - scale_min) / (scale_max - scale_min)) );                       // Point in the normalized space of our target rectangle

            for (int n = 0; n < res_w; n++)
            {
                const float t1 = t0 + t_step;
                const int v1_idx = (int)(t0 * item_count + 0.5f);
                IM_ASSERT(v1_idx >= 0 && v1_idx < values_count);
                const float v1 = values_getter(data, l, (v1_idx + values_offset + 1) % values_count);
                const ImVec2 tp1 = ImVec2( t1, 1.0f - ImSaturate((v1 - scale_min) / (scale_max - scale_min)) );

                // NB: Draw calls are merged together by the DrawList system. Still, we should render our batch are lower level to save a bit of CPU.
                ImVec2 pos0 = ImLerp(inner_bb.Min, inner_bb.Max, tp0);
                ImVec2 pos1 = ImLerp(inner_bb.Min, inner_bb.Max, tp1);
                window->DrawList->AddLine(pos0, pos1, v_hovered == v1_idx ? colors[l] : colors[l]);

                t0 = t1;
                tp0 = tp1;
            }
        }

        if (hovered)
        {
            ImVec2 pos0 = ImLerp(inner_bb.Min, inner_bb.Max, ImVec2(t_step * (v_idx + 0.5f), 0.0f));
            ImVec2 pos1 = ImLerp(inner_bb.Min, inner_bb.Max, ImVec2(t_step * (v_idx + 0.5f), 1.0f));
            window->DrawList->AddLine(pos0, pos1, GetColor(GputopCol_MultilineHover));
        }
    }

    // Text overlay
    if (overlay_text)
        RenderTextClipped(ImVec2(frame_bb.Min.x, frame_bb.Min.y + style.FramePadding.y), frame_bb.Max, overlay_text, NULL, NULL, ImVec2(0.5f,0.0f));

    if (label_size.x > 0.0f)
        RenderText(ImVec2(frame_bb.Max.x + style.ItemInnerSpacing.x, inner_bb.Min.y), label);
}

void PlotMultilines(const char* label,
                    float (*values_getter)(void* data, int line, int idx), void* data,
                    int lines, int values_count, int values_offset,
                    const ImColor *colors,
                    const char* overlay_text,
                    float scale_min, float scale_max, ImVec2 graph_size)
{
    PlotMultilinesEx(label, values_getter, data, lines, values_count, values_offset,
                     colors, overlay_text, scale_min, scale_max, graph_size);
}

} // namespace Gputop
