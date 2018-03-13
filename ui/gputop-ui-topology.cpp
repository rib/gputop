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

#include "util/macros.h"

using namespace ImGui;

namespace Gputop {

static bool IsHovered(const ImRect& box)
{
    return ItemHoverable(box, 0);
}

static bool DrawEu(ImGuiWindow* window,
                   const ImRect& eu_box, bool active, int eu)
{
    window->DrawList->AddRectFilled(eu_box.GetTL(), eu_box.GetBR(),
                                    GetColor(active ?
                                             GputopCol_TopologyEu :
                                             GputopCol_TopologyEuDis), 5);

    ImGuiContext& g = *GImGui;
    char name[20];
    snprintf(name, sizeof(name), "eu%i", eu);
    const ImVec2 text_size = CalcTextSize(name, NULL, true);
    window->DrawList->AddText(g.Font, g.FontSize, eu_box.GetCenter() - text_size / 2,
                              GetColor(GputopCol_TopologyText),
                              name, name + strlen(name));

    if (IsHovered(eu_box)) {
        SetTooltip("Eu%i %s", eu, active ? "available" : "fused off");
        return true;
    }

    return false;
}

static bool DrawSubslice(ImGuiWindow* window,
                         int ss_max, int eu_max,
                         const uint8_t *ss_mask, const uint8_t *eus_mask,
                         const ImRect& ss_box, int s, int ss)
{
    bool enabled = (ss_mask[ss / 8] >> (ss % 8) & 1) != 0;

    window->DrawList->AddRectFilled(ss_box.GetTL(), ss_box.GetBR(),
                                    GetColor(enabled ?
                                             GputopCol_TopologySubslice :
                                             GputopCol_TopologySubsliceDis), 5);

    if (!enabled) {
        if (IsHovered(ss_box)) {
            SetTooltip("Subslice%i fused off", ss);
            return true;
        }
        return false;
    }

    window->DrawList->AddRectFilled(ss_box.GetTL(), ss_box.GetBR(),
                                    GetColor(GputopCol_TopologySubslice), 5);

    //int eu_lines = eu_max / 2;
    int eu_per_line = eu_max / 2;
    const ImVec2 eu_box_size =
        ss_box.GetSize() / ImVec2(eu_per_line, eu_max / eu_per_line);

    bool eu_hovered = false;
    ImVec2 eu_tl(ss_box.GetTL());

    int subslice_stride = DIV_ROUND_UP(eu_max, 8);
    int slice_stride = ss_max * subslice_stride;

    for (int eu = 0; eu < eu_max; eu++) {
        const ImRect eu_box(eu_tl, eu_tl + eu_box_size);

        ImRect tmp = eu_box;
        tmp.Expand(-GetProperty(GputopProp_TopologySubsliceSpacing));
        eu_hovered |= DrawEu(window, tmp,
                             (eus_mask[slice_stride * s + subslice_stride * ss + eu / 8] >> (eu % 8)) & 1, eu);

        if (eu == 0 || (eu % eu_per_line) != (eu_per_line - 1))
            eu_tl.x += eu_box.GetWidth();
        else {
            eu_tl.x = ss_box.GetTL().x;
            eu_tl.y += eu_box.GetHeight();
        }
    }

    if (!eu_hovered && IsHovered(ss_box)) {
        SetTooltip("Subslice%i available", ss);
        return true;
    }

    return eu_hovered;
}

static bool DrawSlice(ImGuiWindow* window,
                      int s_max, int ss_max, int eu_max,
                      const uint8_t *s_mask, const uint8_t *ss_mask, const uint8_t *eus_mask,
                      const ImRect& s_box, int s)
{
    window->DrawList->AddRectFilled(s_box.GetTL(), s_box.GetBR(),
                                    GetColor(GputopCol_TopologySlice), 5);

    const int lines = ss_max / 2 + ((ss_max % 2) != 0 ? 1 : 0);
    const ImVec2 ss_box_size = s_box.GetSize() / ImVec2(2, lines);

    bool ss_hovered = false;
    ImVec2 ss_tl(s_box.GetTL());
    for (int ss = 0; ss < ss_max; ss++) {
        const ImRect ss_box(ss_tl, ss_tl + ss_box_size);

        ImRect tmp(ss_box);
        tmp.Expand(-GetProperty(GputopProp_TopologySliceSpacing));
        ss_hovered |= DrawSubslice(window, ss_max, eu_max, ss_mask, eus_mask, tmp, s, ss);

        if (ss == 0 || ss % 2 != 1)
            ss_tl.x += ss_box.GetWidth();
        else {
            ss_tl.x = s_box.GetTL().x;
            ss_tl.y += ss_box.GetHeight();
        }
    }

    if (!ss_hovered && IsHovered(s_box)) {
        SetTooltip("Slice%i", s);
        return true;
    }

    return false;
}

void RcsTopology(const char *label,
                 int s_max, int ss_max, int eu_max,
                 const uint8_t *s_mask, const uint8_t *ss_mask, const uint8_t *eus_mask,
                 float width)
{
    ImGuiWindow* window = GetCurrentWindow();
    if (window->SkipItems)
        return;

    if (width == 0)
        width = CalcItemWidth();

    ImGuiContext& g = *GImGui;
    const ImGuiStyle& style = g.Style;

    ImVec2 s_tl = window->DC.CursorPos;

    const int lines = ss_max / 2 + ((ss_max % 2) != 0 ? 1 : 0);

    int s_count = 0;
    for (int s = 0; s < s_max; s++)
        s_count += (s_mask[s / 8] >> (s % 8)) & 1;

    const ImVec2 s_box_size(width, GetProperty(GputopProp_TopologySliceHeight) * lines);
    const ImVec2 item_size(s_box_size.x, s_count * s_box_size.y);
    ItemSize(item_size, style.FramePadding.y);
    if (!ItemAdd(ImRect(s_tl, s_tl + item_size), window->GetID(label)))
        return;

    for (int s = 0; s < s_max; s++) {
        if (!(s_mask[s / 8] >> (s % 8) & 1))
            continue;

        const ImRect s_box(s_tl, s_tl + s_box_size);

        ImRect tmp(s_box);
        tmp.Expand(-10);
        DrawSlice(window, s_max, ss_max, eu_max,
                  s_mask, ss_mask, eus_mask, tmp, s);

        s_tl.y += s_box.GetHeight();
    }
}

void EngineTopology(const char *label,
                    uint32_t n_engines, const uint32_t *engines,
                    const char **names,
                    float width)
{
    ImGuiWindow* window = GetCurrentWindow();
    if (window->SkipItems)
        return;

    if (width == 0)
        width = CalcItemWidth();

    ImGuiContext& g = *GImGui;
    const ImGuiStyle& style = g.Style;

    const float engine_height = 40.0f;
    float engines_height = 0.0f;
    uint32_t engine_count = 0;
    for (int e = 0; e < n_engines; e++) {
        engines_height = ImMax(engines[e] * engine_height, engines_height);
        engine_count += engines[e];
    }
    if (engine_count == 0)
        return;

    const ImRect frame_bb(window->DC.CursorPos,
                          window->DC.CursorPos +
                          ImVec2(width, engines_height + style.FramePadding.y * 2));
    const ImRect inner_bb(frame_bb.Min + style.FramePadding,
                          frame_bb.Max - style.FramePadding);
    ItemSize(frame_bb, style.FramePadding.y);
    if (!ItemAdd(frame_bb, window->GetID(label)))
        return;

    ImRect class_box(inner_bb.GetTL(),
                     inner_bb.GetTL() + ImVec2(inner_bb.GetWidth() / n_engines,
                                               engine_height));

    for (uint32_t e = 0; e < n_engines; e++) {
        ImRect instance_box(class_box);

        for (uint32_t i = 0; i < engines[e]; i++) {
            ImRect tmp(instance_box);
            tmp.Expand(-5);

            window->DrawList->AddRectFilled(tmp.GetTL(), tmp.GetBR(),
                                            GetColor(GputopCol_TopologySlice), 5);

            char label[20];
            snprintf(label, sizeof(label), "%s%i", names[e], i);
            const ImVec2 label_size = CalcTextSize(label);
            window->DrawList->AddText(tmp.GetCenter() - label_size / 2,
                                      GetColor(GputopCol_TopologyText), label);

            instance_box.Translate(ImVec2(0.0f, instance_box.GetHeight()));
        }

        class_box.Translate(ImVec2(class_box.GetWidth(), 0));
    }
}

} // namespace Gputop
