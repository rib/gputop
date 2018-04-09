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

#include "gputop-ui-utils.h"

using namespace ImGui;

namespace Gputop {

static struct ColorItem {
    const char *name;
    ImVec4 value;
} colors[GputopCol_COUNT];

static struct PropertyItem {
    const char *name;
    int value;
} properties[GputopProp_COUNT];

void InitColorsProperties()
{
    colors[GputopCol_OaGraph]                = (struct ColorItem){ "OaGraph",                ImVec4(0.15f, 0.85f, 0.20f, 1.00f) };
    colors[GputopCol_TopologySlice]          = (struct ColorItem){ "TopologySlice",          ImVec4(0.57f, 0.84f, 0.62f, 1.00f) };
    colors[GputopCol_TopologySubslice]       = (struct ColorItem){ "TopologySubslice",       ImVec4(1.00f, 0.89f, 0.62f, 1.00f) };
    colors[GputopCol_TopologySubsliceDis]    = (struct ColorItem){ "TopologySubsliceDis",    ImVec4(0.65f, 0.62f, 0.62f, 1.00f) };
    colors[GputopCol_TopologyEu]             = (struct ColorItem){ "TopologyEu",             ImVec4(0.77f, 0.65f, 0.97f, 1.00f) };
    colors[GputopCol_TopologyEuDis]          = (struct ColorItem){ "TopologyEuDis",          ImVec4(0.59f, 0.59f, 0.59f, 1.00f) };
    colors[GputopCol_TopologyText]           = (struct ColorItem){ "TopologyText",           ImVec4(0.00f, 0.00f, 0.00f, 1.00f) };
    colors[GputopCol_MultilineHover]         = (struct ColorItem){ "MultilineHover",         ImVec4(1.00f, 0.38f, 0.00f, 1.00f) };
    colors[GputopCol_TimelineZoomLeft]       = (struct ColorItem){ "TimelineZoomLeft",       ImVec4(0.31f, 0.81f, 0.96f, 0.50f) };
    colors[GputopCol_TimelineZoomRight]      = (struct ColorItem){ "TimelineZoomRight",      ImVec4(0.31f, 0.53f, 0.96f, 0.50f) };
    colors[GputopCol_TimelineEventSelect]    = (struct ColorItem){ "TimelineEventSelect",    ImVec4(0.31f, 0.53f, 0.96f, 1.00f) };
    colors[GputopCol_CounterSelectedText]    = (struct ColorItem){ "CounterSelectedText",    ImVec4(0.31f, 0.53f, 0.96f, 1.00f) };
    colors[GputopCol_PlotLines]              = (struct ColorItem){ "PlotLines",              ImVec4(0.61f, 0.61f, 0.61f, 1.00f) };
    colors[GputopCol_PlotLinesHovered]       = (struct ColorItem){ "PlotLinesHovered",       ImVec4(1.00f, 0.43f, 0.35f, 1.00f) };
    colors[GputopCol_PlotHistogram]          = (struct ColorItem){ "PlotHistogram",          ImVec4(0.90f, 0.70f, 0.00f, 1.00f) };
    colors[GputopCol_PlotHistogramHovered]   = (struct ColorItem){ "PlotHistogramHovered",   ImVec4(0.03f, 0.51f, 0.82f, 1.00f) };

    properties[GputopProp_TimelineScrollScaleFactor] = (struct PropertyItem){ "TimelineScrollScaleFactor", 20 };
    properties[GputopProp_TopologySliceHeight]       = (struct PropertyItem){ "TopologySliceHeight",       70 };
    properties[GputopProp_TopologySliceSpacing]      = (struct PropertyItem){ "TopologySliceSpacing",      5 };
    properties[GputopProp_TopologySubsliceSpacing]   = (struct PropertyItem){ "TopologySubsliceSpacing",   5 };
}

ImU32 GetColor(enum Colors color)
{
    ImGuiStyle& style = GImGui->Style;
    ImVec4 c = colors[color].value;
    c.w *= style.Alpha;
    return ColorConvertFloat4ToU32(c);
}

int GetProperty(enum Properties prop)
{
    return properties[prop].value;
}

void DisplayColorsProperties()
{
    static int output_dest = 0;
    ImGui::Combo("##output_type", &output_dest, "To Clipboard\0To TTY\0"); ImGui::SameLine();
    if (ImGui::Button("Copy Colors"))
    {
        if (output_dest == 0)
            ImGui::LogToClipboard();
        else
            ImGui::LogToTTY();
        for (int i = 0; i < GputopCol_COUNT; i++)
        {
            ImGui::LogText("colors[GputopCol_%s]%*s= (struct ColorItem){ \"%s\",%*sImVec4(%.2ff, %.2ff, %.2ff, %.2ff) };\n",
                           colors[i].name, 23-(int)strlen(colors[i].name), "",
                           colors[i].name, 23-(int)strlen(colors[i].name), "",
                           colors[i].value.x, colors[i].value.y, colors[i].value.z, colors[i].value.w);
        }
        for (int i = 0; i < GputopProp_COUNT; i++)
        {
            ImGui::LogText("properties[GputopProp_%s]%*s= (struct PropertyItem){ \"%s\",%*s%i };\n",
                           properties[i].name, 26-(int)strlen(properties[i].name), "",
                           properties[i].name, 26-(int)strlen(properties[i].name), "",
                           properties[i].value);
        }
        ImGui::LogFinish();
    }

    static ImGuiColorEditFlags alpha_flags = 0;
    ImGui::RadioButton("Opaque", &alpha_flags, 0); ImGui::SameLine();
    ImGui::RadioButton("Alpha", &alpha_flags, ImGuiColorEditFlags_AlphaPreview); ImGui::SameLine();
    ImGui::RadioButton("Both", &alpha_flags, ImGuiColorEditFlags_AlphaPreviewHalf);

    static ImGuiTextFilter filter;
    filter.Draw("Filter colors", 200);

    for (int i = 0; i < GputopCol_COUNT; i++)
    {
        if (!filter.PassFilter(colors[i].name))
            continue;
        ImGui::ColorEdit4(colors[i].name, (float*)&(colors[i].value),
                          ImGuiColorEditFlags_AlphaBar | alpha_flags);
    }
    for (int i = 0; i < GputopProp_COUNT; i++)
    {
        if (!filter.PassFilter(colors[i].name))
            continue;
        ImGui::DragInt(colors[i].name, &properties[i].value);
    }
}

ImColor GetHueColor(int element, int n_elements, float brightness)
{
    return ImColor::HSV(element * 1.0f / ImMax(1, n_elements),
                        brightness, brightness);
}

} // namespace Gputop
