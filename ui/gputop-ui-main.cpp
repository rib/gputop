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

#include <stdlib.h>

#include "imgui.h"
#include "gputop-ui-multilines.h"
#include "gputop-ui-piechart.h"
#include "gputop-ui-plots.h"
#include "gputop-ui-timeline.h"
#include "gputop-ui-topology.h"
#include "gputop-ui-utils.h"

#include "util/hash_table.h"
#include "util/list.h"

#include "gputop-client-context.h"
#include "gputop-util.h"

#ifdef EMSCRIPTEN
#include "imgui_impl_sdl_gles2.h"
#include <GLES2/gl2.h>
#include <SDL.h>
#include <emscripten/emscripten.h>
#include <emscripten/trace.h>
#define ImGui_ScheduleFrame() ImGui_ImplSdlGLES2_ScheduleFrame()
#define ImGui_RenderDrawData(data) ImGui_ImplSdlGLES2_RenderDrawData()
#elif defined(GPUTOP_UI_GTK)
#include "imgui_impl_gtk3_cogl.h"
#include <libsoup/soup.h>
#define ImGui_ScheduleFrame() ImGui_ImplGtk3Cogl_ScheduleFrame()
#define ImGui_RenderDrawData(data) ImGui_ImplGtk3Cogl_RenderDrawData(data)
#elif defined(GPUTOP_UI_GLFW)
#include "imgui_impl_glfw_gl3.h"
#include <epoxy/gl.h>
#include <GLFW/glfw3.h>
#include <uv.h>
#include <getopt.h>
#define ImGui_ScheduleFrame() ImGui_ImplGlfwGL3_ScheduleFrame()
#define ImGui_RenderDrawData(data) ImGui_ImplGlfwGL3_RenderDrawData(data)
#endif

/**/

struct window {
    struct list_head link;

    char name[128];
    bool opened;

    ImVec2 position;
    ImVec2 size;

    void (*display)(struct window*);
    void (*destroy)(struct window*);
};

struct i915_perf_window_counter {
    struct list_head link;

    const struct gputop_metric_set_counter *counter;
    bool use_samples_max;
};

struct i915_perf_window {
    struct window base;

    struct list_head link;
    struct list_head counters;
};

struct timeline_window {
    struct window base;

    uint64_t zoom_start, zoom_length;

    struct gputop_accumulated_samples selected_sample;
    struct gputop_hw_context selected_context;
    char timestamp_search[40];
    int64_t searched_timestamp;
    char gt_timestamp_range[100];
    uint32_t n_accumulated_reports;
    int32_t hovered_report;
    float *accumulated_values;

    struct gputop_perf_tracepoint tracepoint;
    uint64_t tracepoint_selected_ts;

    struct window counters_window;
    struct window events_window;
    struct window reports_window;
    struct window report_window;
    struct window usage_window;

    /**/
    char gt_timestamp_highlight_str[100];
    uint64_t gt_timestamp_highlight;

    /* List of timestamps to display on the timeline */
    char gt_timestamps_list[1024];
    uint64_t gt_timestamps_display[100];
    int n_gt_timestamps_display;

    /* Used when timestamp correlation is not possible */
    uint64_t zoom_tp_start, zoom_tp_length;
};

static struct {
    char host_address[128];
    int host_port;
    gputop_connection_t *connection;
    char *connection_error;

    gputop_client_context ctx;

    struct {
        char *msg;
    } messages[100];
    int start_message;
    int n_messages;

    int n_cpu_colors;
    ImColor cpu_colors[100];

    /* UI */
    struct list_head windows;

    struct window main_window;
    struct window log_window;
    struct window style_editor_window;
    struct window report_window;
    struct window streams_window;
    struct window tracepoints_window;
    struct timeline_window timeline_window;
    struct i915_perf_window global_i915_perf_window;
    struct i915_perf_window contexts_i915_perf_window;
    struct window live_i915_perf_counters_window;
    struct window live_i915_perf_usage_window;

    ImVec4 clear_color;

     /**/
    void *temporary_buffer;
    size_t temporary_buffer_size;

#ifdef EMSCRIPTEN
    SDL_Window *window;
#elif defined(GPUTOP_UI_GLFW)
    GLFWwindow *window;
#endif
} context;

/**/

static void
hide_window(struct window *win)
{
    /* NOP */
}

static void
toggle_show_window(struct window *win)
{
    if (win->opened)
        win->opened = false;
    else {
        win->opened = true;
        list_add(&win->link, &context.windows);
    }
}

/**/

static void *
ensure_temporary_buffer(size_t size)
{
    if (context.temporary_buffer_size < size) {
        context.temporary_buffer_size = size;
        context.temporary_buffer = realloc(context.temporary_buffer, size);
    }

    return context.temporary_buffer;
}

#define ensure_plot_accumulator(n_plot) \
    ((float *) ensure_temporary_buffer(n_plot * sizeof(float)))

#define ensure_timeline_names(n_names) \
    ((char **) ensure_temporary_buffer(n_names * sizeof(char *)))

/**/

extern "C" void
gputop_cr_console_log(const char *format, ...)
{
    va_list ap;
    char str[1024];
    int l;

    va_start(ap, format);
    l = snprintf(str, sizeof(str), "CLIENT: ");
    vsnprintf(&str[l], sizeof(str) - l, format, ap);
    va_end(ap);

    if (context.n_messages < ARRAY_SIZE(context.messages)) {
        context.messages[context.n_messages].msg = strdup(str);
        context.n_messages++;
    } else {
        int idx = (++context.start_message + context.n_messages) % ARRAY_SIZE(context.messages);

        free(context.messages[idx].msg);
        context.messages[idx].msg = strdup(str);
    }
}

static void
clear_client_logs(void)
{
    for (int i = 0; i < context.n_messages; i++)
        free(context.messages[i].msg);
    context.start_message = context.n_messages = 0;
}

/**/

static bool
StartStopSamplingButton(struct gputop_client_context *ctx)
{
    return ImGui::Button(ctx->is_sampling ? "Stop sampling" : "Start sampling") && ctx->metric_set;
}

static void
toggle_start_stop_sampling(struct gputop_client_context *ctx)
{
    if (ctx->is_sampling)
        gputop_client_context_stop_sampling(ctx);
    else
        gputop_client_context_start_sampling(ctx);
}

/**/

static void
update_cpu_colors(int n_cpus)
{
    for (int cpu = 0; cpu < n_cpus; cpu++) {
        float r, g, b;
        ImGui::ColorConvertHSVtoRGB(cpu * 1.0f / n_cpus, 1.0f, 1.0f, r, g, b);
        context.cpu_colors[cpu] = ImColor(r, g, b);
    }
    context.n_cpu_colors = n_cpus;
}

static void
on_connection_data(gputop_connection_t *conn,
                   const void *payload, size_t payload_len,
                   void *user_data)
{
    struct gputop_client_context *ctx = &context.ctx;

    gputop_client_context_handle_data(ctx, payload, payload_len);

    if (ctx->features && context.n_cpu_colors != ctx->features->features->n_cpus)
        update_cpu_colors(ctx->features->features->n_cpus);

    ImGui_ScheduleFrame();
}

static void
on_connection_closed(gputop_connection_t *conn,
                     const char *error,
                     void *user_data)
{
    free(context.connection_error);
    context.connection_error = NULL;
    if (error)
        context.connection_error = strdup(error);
    else
        context.connection_error = strdup("Disconnected");
    context.ctx.connection = NULL;
}

static void
on_connection_ready(gputop_connection_t *conn,
                    void *user_data)
{
    context.connection = conn;
    clear_client_logs();
    gputop_client_context_reset(&context.ctx, conn);
}

static void
reconnect(void)
{
    struct gputop_client_context *ctx = &context.ctx;
    if (ctx->connection)
        gputop_connection_close(ctx->connection);
    free(context.connection_error);
    context.connection_error = NULL;
    ctx->connection = gputop_connect(context.host_address, context.host_port,
                                     on_connection_ready,
                                     on_connection_data,
                                     on_connection_closed, NULL);
}

/**/

static void
pretty_print_counter_value(const struct gputop_metric_set_counter *counter,
                           double value, char *buffer, size_t length)
{
    gputop_client_pretty_print_value(counter->units, value, buffer, length);
}

static double
read_counter_max(struct gputop_client_context *ctx,
                 struct gputop_accumulated_samples *sample,
                 const struct gputop_metric_set_counter *counter,
                 float max_value)
{
    switch (counter->data_type) {
    case GPUTOP_PERFQUERY_COUNTER_DATA_UINT64:
    case GPUTOP_PERFQUERY_COUNTER_DATA_UINT32:
    case GPUTOP_PERFQUERY_COUNTER_DATA_BOOL32:
        if (counter->max_uint64)
            return counter->max_uint64(&ctx->devinfo,
                                       ctx->metric_set,
                                       sample->accumulator.deltas);
        break;
    case GPUTOP_PERFQUERY_COUNTER_DATA_DOUBLE:
    case GPUTOP_PERFQUERY_COUNTER_DATA_FLOAT:
        if (counter->max_float)
            return counter->max_float(&ctx->devinfo,
                                      ctx->metric_set,
                                      sample->accumulator.deltas);
        break;
    }

    return max_value;
}

static void
add_counter_i915_perf_window(struct i915_perf_window *window,
                             const struct gputop_metric_set_counter *counter)
{
    struct i915_perf_window_counter *c =
        (struct i915_perf_window_counter *) calloc(1, sizeof(*c));

    c->counter = counter;
    list_addtail(&c->link, &window->counters);
}

static void
display_i915_perf_counters(struct gputop_client_context *ctx,
                           ImGuiTextFilter *filter,
                           struct gputop_accumulated_samples *samples,
                           bool add_buttons)
{
    if (!ctx->metric_set) {
        ImGui::Text("No metric set selected");
        return;
    }

    for (int c = 0; c < ctx->metric_set->n_counters; c++) {
        const struct gputop_metric_set_counter *counter = &ctx->metric_set->counters[c];

        if (!filter->PassFilter(counter->name)) continue;

        if (add_buttons) {
            ImGui::PushID(counter);
            if (ImGui::Button("+")) {
                if (context.global_i915_perf_window.base.opened)
                    add_counter_i915_perf_window(&context.global_i915_perf_window, counter);
                if (context.contexts_i915_perf_window.base.opened)
                    add_counter_i915_perf_window(&context.contexts_i915_perf_window, counter);
            } ImGui::SameLine();
            ImGui::PopID();
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Add counter to timeline windows");
        }

        double value = samples ? gputop_client_context_read_counter_value(ctx, samples, counter) : 0.0f;
        double max = samples ? read_counter_max(ctx, samples, counter, MAX2(1.0f, value)) : 1.0f;
        ImGui::ProgressBar(value / max, ImVec2(100, 0)); ImGui::SameLine();

        char text[100];
        if (ImGui::IsItemHovered()) {
            gputop_client_context_pretty_print_max(ctx, counter,
                                                   ctx->oa_aggregation_period_ns,
                                                   text, sizeof(text));
            ImGui::SetTooltip("Maximum : %s", text);
        }

        pretty_print_counter_value(counter, value, text, sizeof(text));
        ImGui::Text("%s : %s", counter->name, text);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", counter->desc);
        }
    }
}


static void
display_live_i915_perf_counters_window(struct window *win)
{
    struct gputop_client_context *ctx = &context.ctx;

    ImGui::Text("Metric set: %s", ctx->metric_set ? ctx->metric_set->name : "<None>");
    static ImGuiTextFilter filter;
    filter.Draw();

    if (!ctx->metric_set)
        return;

    struct gputop_accumulated_samples *last_sample =
        list_empty(&ctx->graphs) ? NULL :
        list_last_entry(&ctx->graphs, struct gputop_accumulated_samples, link);

    ImGui::BeginChild("##counters");
    display_i915_perf_counters(ctx, &filter, last_sample, true);
    ImGui::EndChild();
}

static void
show_live_i915_perf_counters_window(void)
{
    struct window *window = &context.live_i915_perf_counters_window;

    if (window->opened) {
        window->opened = false;
        return;
    }

    snprintf(window->name, sizeof(window->name),
             "i915 perf counters (live)##%p", window);
    window->size = ImVec2(400, 600);
    window->display = display_live_i915_perf_counters_window;
    window->destroy = hide_window;
    window->opened = true;

    list_add(&window->link, &context.windows);
}

static void
display_live_i915_perf_usage_window(struct window *win)
{
    struct gputop_client_context *ctx = &context.ctx;
    ImVec2 pb_size(ImGui::GetWindowContentRegionWidth() * 2.0f / 3.0f, 0);
    double idle = 1.0f;

    list_for_each_entry(struct gputop_hw_context, context, &ctx->hw_contexts, link) {
        ImGui::ProgressBar(context->usage_percent, pb_size); ImGui::SameLine();
        ImGui::Text("%s", context->name);

        idle -= context->usage_percent;
    }

    ImGui::ProgressBar(idle, pb_size); ImGui::SameLine(); ImGui::Text("Idle");
}

static void
show_live_i915_perf_usage_window(void)
{
    struct window *window = &context.live_i915_perf_usage_window;

    if (window->opened) {
        window->opened = false;
        return;
    }

    snprintf(window->name, sizeof(window->name),
             "i915 perf usage (live)##%p", window);
    window->size = ImVec2(400, 300);
    window->display = display_live_i915_perf_usage_window;
    window->destroy = hide_window;
    window->opened = true;

    list_add(&window->link, &context.windows);
}

/**/

static float *
get_counter_samples(struct gputop_client_context *ctx,
                    int max_graphs,
                    struct gputop_accumulated_samples *first_samples,
                    struct i915_perf_window_counter *counter,
                    float *max_value)
{
    float *values = ensure_plot_accumulator(max_graphs);
    int i;

    for (i = 0; i < (max_graphs - ctx->n_graphs); i++)
        values[i] = 0.0f;

    *max_value = 0.0f;
    struct gputop_accumulated_samples *sample = first_samples;
    for (; i < max_graphs; i++) {
        values[i] = gputop_client_context_read_counter_value(ctx, sample, counter->counter);
        *max_value = MAX2(*max_value, values[i]);
        sample = list_first_entry(&sample->link,
                                  struct gputop_accumulated_samples, link);
    }

    return values;
}

static void
remove_counter_i915_perf_window(struct i915_perf_window_counter *counter)
{
    list_del(&counter->link);
    free(counter);
}

static bool
select_i915_perf_counter(struct gputop_client_context *ctx,
                         const struct gputop_metric_set_counter **out_counter)
{
    bool selected = false;
    static ImGuiTextFilter filter;
    filter.Draw();

    if (!ctx->metric_set) return false;

    struct gputop_accumulated_samples *last_sample =
        list_last_entry(&ctx->graphs, struct gputop_accumulated_samples, link);

    ImGui::BeginChild("##block", ImVec2(0, 300));
    for (int c = 0; c < ctx->metric_set->n_counters; c++) {
        bool hovered;
        const struct gputop_metric_set_counter *counter =
            &ctx->metric_set->counters[c];
        if (!filter.PassFilter(counter->name)) continue;
        if (ImGui::Selectable(counter->name)) {
            *out_counter = counter;
            selected = true;
        }
        hovered = ImGui::IsItemHovered();
        double value = gputop_client_context_read_counter_value(ctx, last_sample, counter);
        ImGui::ProgressBar(value / read_counter_max(ctx, last_sample, counter, MAX2(1.0f, value)),
                           ImVec2(100, 0)); ImGui::SameLine();
        char svalue[100];
        pretty_print_counter_value(counter, value, svalue, sizeof(svalue));
        if (ImGui::Selectable(svalue, hovered)) {
            *out_counter = counter;
            selected = true;
        }
    }
    ImGui::EndChild();

    return selected;
}

static void
cleanup_counters_i915_perf_window(struct i915_perf_window *window)
{
    list_for_each_entry_safe(struct i915_perf_window_counter, c,
                             &window->counters, link) {
        list_del(&c->link);
        free(c);
    }
}

static void
display_global_i915_perf_window(struct window *win)
{
    struct i915_perf_window *window = (struct i915_perf_window *) win;
    struct gputop_client_context *ctx = &context.ctx;
    uint32_t max_graphs =
        (ctx->oa_visible_timeline_s * 1000000000.0f) / ctx->oa_aggregation_period_ns;

    bool open_popup = ImGui::Button("Select counters");
    if (open_popup)
        ImGui::OpenPopup("counter picker");
    if (ImGui::BeginPopup("counter picker")) {
        const struct gputop_metric_set_counter *counter = NULL;
        if (select_i915_perf_counter(ctx, &counter))
            add_counter_i915_perf_window(window, counter);
        ImGui::EndPopup();
    } ImGui::SameLine();
    if (ImGui::Button("Clear counters")) {
        cleanup_counters_i915_perf_window(window);
    } ImGui::SameLine();
    if (StartStopSamplingButton(ctx)) { toggle_start_stop_sampling(ctx); }
    if (ctx->n_graphs < max_graphs) {
        ImGui::SameLine(); ImGui::Text("Loading:"); ImGui::SameLine();
        ImGui::ProgressBar((float) ctx->n_graphs / max_graphs);
    }


    ImGui::BeginChild("##block");
    list_for_each_entry_safe(struct i915_perf_window_counter, c, &window->counters, link) {
        /* Hide previous selected counters on metric set that isn't
         * currently used. */
        if (c->counter->metric_set != ctx->metric_set)
            continue;

        ImGui::Text("%s", c->counter->desc);
        ImGui::PushID(c);
        if (ImGui::Button("X")) { remove_counter_i915_perf_window(c); }
        ImGui::PopID();
        if (ImGui::IsItemHovered()) { ImGui::SetTooltip("Remove counter"); } ImGui::SameLine();
        ImGui::PushID(c);
        if (ImGui::Button("M")) { c->use_samples_max ^= 1; }
        ImGui::PopID();
        if (ImGui::IsItemHovered()) { ImGui::SetTooltip("Change max behavior"); } ImGui::SameLine();

        struct gputop_accumulated_samples *first_samples =
          list_first_entry(&ctx->graphs, struct gputop_accumulated_samples, link);
        float max_value = 0.0f;
        const float *values =
            get_counter_samples(ctx, max_graphs, first_samples, c, &max_value);
        int hovered =
            Gputop::PlotLines("", values, max_graphs, 0, -1,
                              c->counter->name,
                              0, c->use_samples_max ? max_value : read_counter_max(ctx, first_samples,
                                                                                   c->counter, max_value),
                              ImVec2(ImGui::GetContentRegionAvailWidth() - 10, 50.0f));
        if (hovered >= 0) {
            char tooltip_tex[100];
            pretty_print_counter_value(c->counter,
                                       values[hovered],
                                       tooltip_tex, sizeof(tooltip_tex));
            ImGui::SetTooltip("%s", tooltip_tex);
        }
    }
    ImGui::EndChild();
}

static void
show_global_i915_perf_window(void)
{
    struct i915_perf_window *window = &context.global_i915_perf_window;

    if (window->base.opened) {
        window->base.opened = false;
        return;
    }

    snprintf(window->base.name, sizeof(window->base.name),
             "i915 perf counters (timeline)##%p", window);
    window->base.size = ImVec2(400, 600);
    window->base.display = display_global_i915_perf_window;
    window->base.destroy = hide_window;
    window->base.opened = true;

    list_inithead(&window->counters);

    list_add(&window->base.link, &context.windows);
}

/**/

static void
select_hw_contexts(struct gputop_client_context *ctx)
{
    list_for_each_entry(struct gputop_hw_context, context, &ctx->hw_contexts, link) {
        if (ImGui::Selectable(context->name, context->selected)) {
            context->selected = context->selected ? false : true;
        }
    }
}

static void
display_contexts_i915_perf_window(struct window *win)
{
    struct i915_perf_window *window = (struct i915_perf_window *) win;
    struct gputop_client_context *ctx = &context.ctx;
    uint32_t max_graphs =
        (ctx->oa_visible_timeline_s * 1000000000.0f) / ctx->oa_aggregation_period_ns;

    bool open_popup = ImGui::Button("Select contexts");
    if (open_popup)
        ImGui::OpenPopup("context picker");
    if (ImGui::BeginPopup("context picker")) {
        select_hw_contexts(ctx);
        ImGui::EndPopup();
    } ImGui::SameLine();
    open_popup = ImGui::Button("Select counters");
    if (open_popup)
        ImGui::OpenPopup("counter picker");
    if (ImGui::BeginPopup("counter picker")) {
        const struct gputop_metric_set_counter *counter = NULL;
        if (select_i915_perf_counter(ctx, &counter))
            add_counter_i915_perf_window(window, counter);
        ImGui::EndPopup();
    } ImGui::SameLine();
    if (ImGui::Button("Clear counters")) {
        cleanup_counters_i915_perf_window(window);
    } ImGui::SameLine();
    if (StartStopSamplingButton(ctx)) { toggle_start_stop_sampling(ctx); }
    if (ctx->n_graphs < max_graphs) {
        ImGui::SameLine(); ImGui::Text("Loading:"); ImGui::SameLine();
        ImGui::ProgressBar((float) ctx->n_graphs / max_graphs);
    }

    ImGui::BeginChild("##block");
    list_for_each_entry_safe(struct i915_perf_window_counter, c, &window->counters, link) {
        /* Hide previous selected counters on metric set that isn't
         * currently used. */
        if (c->counter->metric_set != ctx->metric_set)
            continue;

        ImGui::PushID(c);
        if (ImGui::Button("X")) { remove_counter_i915_perf_window(c); }
        ImGui::PopID();
        if (ImGui::IsItemHovered()) { ImGui::SetTooltip("Remove counter"); } ImGui::SameLine();
        ImGui::PushID(c);
        if (ImGui::Button("M")) { c->use_samples_max ^= 1; }
        ImGui::PopID();
        if (ImGui::IsItemHovered()) { ImGui::SetTooltip("Change max behavior"); } ImGui::SameLine();

        ImGui::Text("%s", c->counter->name);

        list_for_each_entry(struct gputop_hw_context, context, &ctx->hw_contexts, link) {
            if (!context->selected)
                continue;

            ImGui::Text("%s", context->name);
            struct gputop_accumulated_samples *first_samples =
              list_first_entry(&context->graphs, struct gputop_accumulated_samples, link);
            float max_value = 0.0f;
            const float *values =
                get_counter_samples(ctx, max_graphs, first_samples, c, &max_value);
            int hovered =
                Gputop::PlotLines("", values, max_graphs, 0, -1,
                                  "",
                                  0, c->use_samples_max ? max_value : read_counter_max(ctx, first_samples,
                                                                                       c->counter, max_value),
                                  ImVec2(ImGui::GetContentRegionAvailWidth() - 10, 50.0f));
            if (hovered >= 0 ) {
                char tooltip_tex[100];
                pretty_print_counter_value(c->counter,
                                           values[hovered],
                                           tooltip_tex, sizeof(tooltip_tex));
                ImGui::SetTooltip("%s", tooltip_tex);
            }
        }
    }
    ImGui::EndChild();
}

static void
show_contexts_i915_perf_window(void)
{
    struct i915_perf_window *window = &context.contexts_i915_perf_window;

    if (window->base.opened) {
        window->base.opened = false;
        return;
    }

    snprintf(window->base.name, sizeof(window->base.name),
             "Per context i915 perf counters (timeline)##%p", window);
    window->base.size = ImVec2(400, 600);
    window->base.display = display_contexts_i915_perf_window;
    window->base.destroy = hide_window;
    window->base.opened = true;

    list_inithead(&window->counters);

    list_add(&window->base.link, &context.windows);
}

/**/

// static void
// display_accumulated_reports(struct gputop_client_context *ctx,
//                             struct gputop_accumulated_samples *samples,
//                             bool default_opened)
// {
//     struct gputop_record_iterator iter;

//     ImGui::BeginChild("##reports");

//     gputop_record_iterator_init(&iter, samples);
//     while (gputop_record_iterator_next(&iter)) {
//         switch (iter.header->type) {
//         case DRM_I915_PERF_RECORD_OA_BUFFER_LOST:
//             ImGui::Text("OA buffer lost");
//             break;
//         case DRM_I915_PERF_RECORD_OA_REPORT_LOST:
//             ImGui::Text("OA report lost");
//             break;

//         case DRM_I915_PERF_RECORD_SAMPLE: {
//             const uint64_t *cpu_timestamp = (const uint64_t *)
//                 gputop_i915_perf_record_field(&ctx->i915_perf_config, iter.header,
//                                               GPUTOP_I915_PERF_FIELD_CPU_TIMESTAMP);
//             const uint64_t *gpu_timestamp = (const uint64_t *)
//                 gputop_i915_perf_record_field(&ctx->i915_perf_config, iter.header,
//                                               GPUTOP_I915_PERF_FIELD_GPU_TIMESTAMP);
//             const uint8_t *report = (const uint8_t *)
//                 gputop_i915_perf_record_field(&ctx->i915_perf_config, iter.header,
//                                               GPUTOP_I915_PERF_FIELD_OA_REPORT);

//             ImGui::Text("rcs=%08" PRIx64 "(%" PRIx64 " scaled) rcs64=%016" PRIx64
//                         " cpu=%" PRIx64 " id=0x%x reason=%s",
//                         gputop_cc_oa_report_get_timestamp(report),
//                         gputop_timebase_scale_ns(&ctx->devinfo,
//                                                  gputop_cc_oa_report_get_timestamp(report)),
//                         gpu_timestamp ? *gpu_timestamp : 0UL,
//                         cpu_timestamp ? *cpu_timestamp : 0UL,
//                         gputop_cc_oa_report_get_ctx_id(&ctx->devinfo, report),
//                         gputop_cc_oa_report_get_reason(&ctx->devinfo, report));
//             break;
//         }

//         default:
//             ImGui::Text("i915 perf: Spurious header type = %d", iter.header->type);
//             iter.done = true;
//             break;
//         }
//     }

//     ImGui::EndChild();
// }

static void
display_report_window(struct window *win)
{
    struct gputop_client_context *ctx = &context.ctx;

    ImGui::Columns(5);
    if (ctx->features) {
        ImGui::Text("OA available: %s",
                    ctx->features->features->has_i915_oa ? "true" : "false");
        ImGui::Text("OA CPU timestamps available: %s",
                    ctx->features->features->has_i915_oa_cpu_timestamps ? "true" : "false");
        ImGui::Text("OA GPU timestamps available: %s",
                    ctx->features->features->has_i915_oa_gpu_timestamps ? "true" : "false");
        ImGui::Text("Server PID: %u", ctx->features->features->server_pid);
        ImGui::Text("Fake mode: %s", ctx->features->features->fake_mode ? "true" : "false");
    }

    ImGui::NextColumn();
    ImGui::Text("Available metrics: ");
    if (ctx->features) {
        ImGui::PushItemWidth(ImGui::GetContentRegionAvailWidth());
        for (int u = 0; u < ctx->features->features->n_supported_oa_uuids; u++) {
            char input[100];
            snprintf(input, sizeof(input), "%s", ctx->features->features->supported_oa_uuids[u]);
            ImGui::PushID(ctx->features->features->supported_oa_uuids[u]);
            ImGui::InputText("", input, sizeof(input), ImGuiInputTextFlags_ReadOnly);
            ImGui::PopID();
        }
    }
    ImGui::PopItemWidth();

    ImGui::NextColumn();
    if (ctx->last_header) {
        const uint32_t *last_report = (const uint32_t *)
            gputop_i915_perf_record_field(&ctx->i915_perf_config, ctx->last_header,
                                          GPUTOP_I915_PERF_FIELD_OA_REPORT);
        for (uint32_t i = 0; i < 64; i++)
            ImGui::Text("%u\t: 0x%08x", i, last_report[i]);
        ImGui::Text("last_oa_timestamp: %" PRIx64, ctx->last_oa_timestamp);
    }

    // ImGui::NextColumn();
    // if (ctx->n_timelines > 0) {
    //     struct gputop_accumulated_samples *first =
    //         list_first_entry(&ctx->timelines, struct gputop_accumulated_samples, link);
    //     struct gputop_accumulated_samples *last =
    //         list_last_entry(&ctx->timelines, struct gputop_accumulated_samples, link);
    //     uint64_t total_time = last->timestamp_start - first->timestamp_end;

    //     list_for_each_entry_safe(struct gputop_hw_context, context,
    //                              &ctx->hw_contexts, link) {
    //         ImGui::Text("hw_id %" PRIx64 " : %.2f", context->hw_id,
    //                     100.0f * ((double)context->time_spent / total_time));
    //     }
    // }

    ImGui::NextColumn();
    ImGui::Text("HW Contexts");
    // struct gputop_hw_context *hovered_context = NULL;
    list_for_each_entry(struct gputop_hw_context, context, &ctx->hw_contexts, link) {
        if (ImGui::TreeNode(context, "hw_id=%u/0x%x name=%s row=%i",
                            context->hw_id, context->hw_id, context->name,
                            context->timeline_row)) {
            ImGui::TreePop();
        }
        // if (ImGui::IsItemHovered()) hovered_context = context;
    }

    ImGui::NextColumn();
    ImGui::Text("Process infos");
    list_for_each_entry(struct gputop_process_info, process_info, &ctx->process_infos, link) {
        ImGui::Text("pid=%u comm=%s", process_info->pid, process_info->cmd);
    }

#if 0
    ImGui::NextColumn();
    struct {
        uint64_t start;
        uint64_t end;
    } hovered_window = { 0ULL, 0ULL };
    list_for_each_entry(struct gputop_accumulated_samples, samples, &ctx->timelines, link) {
        const uint64_t *cpu_ts0 = (const uint64_t *)
            gputop_i915_perf_record_field(&ctx->i915_perf_config,
                                          samples->start_report.header,
                                          GPUTOP_I915_PERF_FIELD_CPU_TIMESTAMP);
        const uint64_t *cpu_ts1 = (const uint64_t *)
            gputop_i915_perf_record_field(&ctx->i915_perf_config,
                                          samples->end_report.header,
                                          GPUTOP_I915_PERF_FIELD_CPU_TIMESTAMP);
        char cpu_time_length[20];
        gputop_client_pretty_print_value(GPUTOP_PERFQUERY_COUNTER_UNITS_NS,
                                         cpu_ts0 ? (*cpu_ts1 - *cpu_ts0) : 0UL,
                                         cpu_time_length, sizeof(cpu_time_length));
        char gpu_time_length[20];
        gputop_client_pretty_print_value(GPUTOP_PERFQUERY_COUNTER_UNITS_NS,
                                         samples->accumulator.last_timestamp -
                                         samples->accumulator.first_timestamp,
                                         gpu_time_length, sizeof(gpu_time_length));
        const uint8_t *report0 = (const uint8_t *)
            gputop_i915_perf_record_field(&ctx->i915_perf_config,
                                          samples->start_report.header,
                                          GPUTOP_I915_PERF_FIELD_OA_REPORT);
        const uint8_t *report1 = (const uint8_t *)
            gputop_i915_perf_record_field(&ctx->i915_perf_config,
                                          samples->end_report.header,
                                          GPUTOP_I915_PERF_FIELD_OA_REPORT);

        if (ImGui::TreeNode(samples, "%s oa_ts=%" PRIx64 "-%" PRIx64 " scaled_ts=%" PRIx64 "-%" PRIx64 "(%" PRIx64 ") time=%s/%s",
                            samples->context->name,
                            gputop_cc_oa_report_get_timestamp(report0),
                            gputop_cc_oa_report_get_timestamp(report1),
                            samples->timestamp_start, samples->timestamp_end,
                            gputop_timebase_scale_ns(&ctx->devinfo,
                                                     gputop_cc_oa_report_get_timestamp(report1) -
                                                     gputop_cc_oa_report_get_timestamp(report0)),
                            cpu_time_length, gpu_time_length)) {
            display_accumulated_reports(ctx, samples, false);
            ImGui::TreePop();
        }
        if (ImGui::IsItemHovered() && cpu_ts0) { hovered_window.start = *cpu_ts0; hovered_window.end = *cpu_ts1; }
    }

    ImGui::NextColumn();
    const bool has_filter = (hovered_window.start != hovered_window.end) || hovered_context;
    list_for_each_entry(struct gputop_i915_perf_chunk, chunk, &ctx->i915_perf_chunks, link) {
        for (const struct drm_i915_perf_record_header *header = (const struct drm_i915_perf_record_header *) chunk->data;
             (const uint8_t *) header < (chunk->data + chunk->length);
             header = (const struct drm_i915_perf_record_header *) (((const uint8_t *)header) + header->size)) {
            switch (header->type) {
            case DRM_I915_PERF_RECORD_OA_BUFFER_LOST:
                ImGui::Text("OA buffer lost");
                break;
            case DRM_I915_PERF_RECORD_OA_REPORT_LOST:
                ImGui::Text("OA report lost");
                break;

            case DRM_I915_PERF_RECORD_SAMPLE: {
                const uint64_t *cpu_timestamp = (const uint64_t *)
                    gputop_i915_perf_record_field(&ctx->i915_perf_config, header,
                                                  GPUTOP_I915_PERF_FIELD_CPU_TIMESTAMP);
                const uint64_t *gpu_timestamp = (const uint64_t *)
                    gputop_i915_perf_record_field(&ctx->i915_perf_config, header,
                                                  GPUTOP_I915_PERF_FIELD_GPU_TIMESTAMP);
                const uint8_t *report = (const uint8_t *)
                    gputop_i915_perf_record_field(&ctx->i915_perf_config, header,
                                                  GPUTOP_I915_PERF_FIELD_OA_REPORT);
                const bool visible = !has_filter ||
                    (cpu_timestamp && *cpu_timestamp >= hovered_window.start && *cpu_timestamp <= hovered_window.end) ||
                    (hovered_context && gputop_cc_oa_report_get_ctx_id(&ctx->devinfo, report) == hovered_context->hw_id);

                if (visible &&
                    ImGui::TreeNodeEx(report,
                                      has_filter ? (ImGuiTreeNodeFlags_Selected | ImGuiTreeNodeFlags_DefaultOpen) : 0,
                                      "rcs=%08" PRIx64 " rcs64=%016" PRIx64 " cpu=%" PRIx64,
                                      gputop_cc_oa_report_get_timestamp(report),
                                      gpu_timestamp ? *gpu_timestamp : 0UL,
                                      cpu_timestamp ? *cpu_timestamp : 0UL)) {
                    /* Display report fields */
                    ImGui::Text("hw_id=0x%x reason=%s",
                                gputop_cc_oa_report_get_ctx_id(&ctx->devinfo, report),
                                gputop_cc_oa_report_get_reason(&ctx->devinfo, report));
                    ImGui::TreePop();
                }
                break;
            }

            default:
                ImGui::Text("i915 perf: Spurious header type = %d", header->type);
                break;
            }
        }
    }
#endif
}

static void
show_report_window(void)
{
    struct window *window = &context.report_window;

    if (window->opened) {
        window->opened = false;
        return;
    }

    snprintf(window->name, sizeof(window->name),
             "Last perf report##%p", window);
    window->size = ImVec2(400, 400);
    window->display = display_report_window;
    window->destroy = hide_window;
    window->opened = true;

    list_add(&window->link, &context.windows);
}

/**/

static uint64_t
get_end_timeline_ts(struct gputop_client_context *ctx,
                    bool for_i915_perf)
{
    struct gputop_accumulated_samples *oa_end = list_empty(&ctx->timelines) ?
        NULL : list_last_entry(&ctx->timelines, struct gputop_accumulated_samples, link);
    struct gputop_perf_tracepoint_data *tp_end = list_empty(&ctx->perf_tracepoints_data) ?
        NULL : list_last_entry(&ctx->perf_tracepoints_data,
                               struct gputop_perf_tracepoint_data, link);

    if (for_i915_perf && !ctx->i915_perf_config.cpu_timestamps)
        tp_end = NULL;

    return MAX2(oa_end ? oa_end->timestamp_end : 0,
                tp_end ? tp_end->data.time : 0);
}

static void
get_timeline_bounds(struct timeline_window *window,
                    struct gputop_client_context *ctx,
                    bool for_i915_perf,
                    uint64_t *start, uint64_t *end,
                    uint64_t zoom_start, uint64_t zoom_length)
{
    uint64_t merged_end_ts = get_end_timeline_ts(ctx, for_i915_perf);

    const uint64_t max_length = ctx->oa_visible_timeline_s * 1000000000ULL;
    uint64_t start_ts = merged_end_ts - MIN2(max_length, merged_end_ts) + zoom_start;
    uint64_t end_ts = zoom_length == 0 ?
        merged_end_ts : start_ts + zoom_length;

    *start = start_ts;
    *end = end_ts;
}

static void
tracepoint_print_prev_next(struct gputop_client_context *ctx,
                           char *buf, size_t len,
                           struct gputop_perf_tracepoint_data *data)
{
    struct gputop_perf_tracepoint *tp = data->tp;
    struct gputop_perf_tracepoint_data *other;
    char pretty_value[100];

    other = (data->tp_link.prev == &tp->data) ? NULL :
        LIST_ENTRY(struct gputop_perf_tracepoint_data, data->tp_link.prev, tp_link);
    if (other) {
        gputop_client_pretty_print_value(GPUTOP_PERFQUERY_COUNTER_UNITS_NS,
                                         data->data.time - other->data.time,
                                         pretty_value, sizeof(pretty_value));

        int l = snprintf(buf, len, "\nprev @ %s", pretty_value);
        buf += l;
        len -= l;
    }

    other = (data->tp_link.next == &tp->data) ? NULL :
        LIST_ENTRY(struct gputop_perf_tracepoint_data, data->tp_link.next, tp_link);
    if (other) {
        gputop_client_pretty_print_value(GPUTOP_PERFQUERY_COUNTER_UNITS_NS,
                                         other->data.time - data->data.time,
                                         pretty_value, sizeof(pretty_value));

        int l = snprintf(buf, len, "\nnext @ %s", pretty_value);
        buf += l;
        len -= l;
    }
}

static bool
timeline_select_context(struct timeline_window *window,
                        struct gputop_client_context *ctx,
                        struct gputop_hw_context **out_context)
{
    bool selected = false;
    if (ImGui::Button("Move to context"))
        ImGui::OpenPopup("Move to context");
    if (ImGui::BeginPopup("Move to context")) {
        list_for_each_entry(struct gputop_hw_context, context, &ctx->hw_contexts, link) {
            if (ImGui::Selectable(context->name)) {
                *out_context = context;
                selected = true;
            }
        }
        ImGui::EndPopup();
    }
    return selected;
}

static void
timeline_focus_on_first_sample(struct timeline_window *window,
                               struct gputop_client_context *ctx,
                               struct gputop_hw_context *context)
{
    uint64_t start_ts, end_ts;
    get_timeline_bounds(window, ctx, true, &start_ts, &end_ts,
                        window->zoom_start, window->zoom_length);

    list_for_each_entry(struct gputop_accumulated_samples, samples, &ctx->timelines, link) {
        if (samples->timestamp_end < start_ts)
            continue;
        if (samples->timestamp_start > end_ts)
            break;
        if (samples->context != context)
            continue;

        const uint64_t max_length = ctx->oa_visible_timeline_s * 1000000000ULL;
        uint64_t total_end_ts = get_end_timeline_ts(ctx, true);

        uint64_t ts_length = samples->timestamp_end - samples->timestamp_start;
        ts_length *= 2;

        window->zoom_start = (samples->timestamp_start - ts_length / 4) - (total_end_ts - max_length);
        window->zoom_length = ts_length;

        break;
    }
}

static bool
timeline_select_gt_timestamp(struct timeline_window *window,
                             struct gputop_client_context *ctx)
{
    bool modified = false;
    if (ImGui::Button("Move to timestamp"))
        ImGui::OpenPopup("Move to timestamp");
    if (ImGui::BeginPopup("Move to timestamp")) {
        modified = ImGui::InputText("Timestamp (hexa)",
                                    window->gt_timestamp_highlight_str,
                                    sizeof(window->gt_timestamp_highlight_str),
                                    ImGuiInputTextFlags_EnterReturnsTrue);
        if (modified) {
            window->gt_timestamp_highlight =
                strtol(window->gt_timestamp_highlight_str, NULL, 16);
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    return modified;
}

static void
timeline_focus_on_gt_timestamp(struct timeline_window *window,
                               struct gputop_client_context *ctx,
                               uint64_t gt_timestamp)
{
    list_for_each_entry(struct gputop_accumulated_samples, samples, &ctx->timelines, link) {
        if (gputop_i915_perf_record_timestamp(&ctx->i915_perf_config,
                                              samples->end_report.header) < gt_timestamp)
            continue;
        if (gputop_i915_perf_record_timestamp(&ctx->i915_perf_config,
                                              samples->start_report.header) > gt_timestamp)
            break;

        const uint64_t max_length = ctx->oa_visible_timeline_s * 1000000000ULL;
        uint64_t total_end_ts = get_end_timeline_ts(ctx, true);

        uint64_t ts_length = samples->timestamp_end - samples->timestamp_start;
        ts_length *= 2;

        window->zoom_start = (samples->timestamp_start - ts_length / 4) - (total_end_ts - max_length);
        window->zoom_length = ts_length;

        break;
    }
}

static void
timeline_highlight_timestamps(struct timeline_window *window,
                              struct gputop_client_context *ctx)
{
    if (ImGui::Button("Highlight timestamps"))
        ImGui::OpenPopup("Highlight timestamps");
    if (ImGui::BeginPopup("Highlight timestamps")) {
        if (ImGui::InputTextMultiline("Timestamps",
                                      window->gt_timestamps_list,
                                      sizeof(window->gt_timestamps_list))) {
            char *str = window->gt_timestamps_list, *str_end =
                window->gt_timestamps_list + strlen(window->gt_timestamps_list);
            window->n_gt_timestamps_display = 0;
            while (str < str_end &&
                   window->n_gt_timestamps_display < ARRAY_SIZE(window->gt_timestamps_display)) {
                char *next_str = NULL;
                uint32_t gt_timestamp = strtol(str, &next_str, 16);

                window->gt_timestamps_display[window->n_gt_timestamps_display++] =
                    gputop_client_context_convert_gt_timestamp(ctx, gt_timestamp);

                if (!next_str || next_str == str)
                    break;

                str = next_str;
            }
        }

        for (int i = 0; i < window->n_gt_timestamps_display; i++) {
            ImGui::Text("%i: %lu\n", i, window->gt_timestamps_display[i]);
        }

        ImGui::EndPopup();
    }
}

static void
update_timeline_report_range(struct timeline_window *window,
                             struct gputop_client_context *ctx,
                             const struct drm_i915_perf_record_header *start,
                             const struct drm_i915_perf_record_header *end)
{
    if (!start) {
        snprintf(window->gt_timestamp_range, sizeof(window->gt_timestamp_range),
                 "no selection");
        return;
    }

    snprintf(window->gt_timestamp_range, sizeof(window->gt_timestamp_range),
             "ts: 0x%x(%s) - 0x%x(%s)",
             gputop_i915_perf_record_timestamp(&ctx->i915_perf_config, start),
             gputop_i915_perf_record_reason(&ctx->i915_perf_config, &ctx->devinfo, start),
             gputop_i915_perf_record_timestamp(&ctx->i915_perf_config, end),
             gputop_i915_perf_record_reason(&ctx->i915_perf_config, &ctx->devinfo, end));
}

static void
search_timeline_reports_for_timestamp(struct timeline_window *window,
                                      struct gputop_client_context *ctx)
{
    if (strlen(window->timestamp_search) < 1) {
        window->searched_timestamp = -1;
        window->hovered_report = -1;
        return;
    }

    const struct drm_i915_perf_record_header *last = NULL;
    struct gputop_record_iterator iter;
    uint32_t ts = strtol(window->timestamp_search, NULL, 16);
    int report = 0;
    gputop_record_iterator_init(&iter, &window->selected_sample);
    while (gputop_record_iterator_next(&iter)) {
        if (iter.header->type != DRM_I915_PERF_RECORD_SAMPLE)
            continue;

        uint32_t report_ts =
            gputop_i915_perf_record_timestamp(&ctx->i915_perf_config,
                                              iter.header);

        if (ts < report_ts) {
            window->searched_timestamp = report_ts;
            window->hovered_report = report;
            update_timeline_report_range(window, ctx, last, iter.header);
            return;
        }
        last = iter.header;
        report++;
    }

    window->searched_timestamp = -1;
    window->hovered_report = -1;
}

static void
update_timeline_selected_reports(struct timeline_window *window,
                                 struct gputop_client_context *ctx,
                                 struct gputop_accumulated_samples *sample)
{
    if (window->selected_sample.start_report.header == sample->start_report.header)
        return;

    memcpy(&window->selected_sample, sample, sizeof(window->selected_sample));
    memcpy(&window->selected_context, sample->context, sizeof(window->selected_context));

    struct gputop_record_iterator iter;
    int n_reports = 0;
    gputop_record_iterator_init(&iter, sample);
    while (gputop_record_iterator_next(&iter)) {
        if (iter.header->type == DRM_I915_PERF_RECORD_SAMPLE)
            n_reports++;
    }

    assert(n_reports > 1);
    int n_accumulated_reports = n_reports - 1;

    int n_counters = ctx->metric_set->n_counters;

    free(window->accumulated_values);
    window->accumulated_values = (float *)
        calloc(n_accumulated_reports * n_counters, sizeof(float));
    window->n_accumulated_reports = n_accumulated_reports;
    window->hovered_report = -1;

    const uint8_t *last_report = NULL;
    int i = 0;
    gputop_record_iterator_init(&iter, sample);
    while (gputop_record_iterator_next(&iter)) {
        if (iter.header->type != DRM_I915_PERF_RECORD_SAMPLE)
            continue;

        const uint8_t *report = (const uint8_t *)
            gputop_i915_perf_record_field(&ctx->i915_perf_config, iter.header,
                                          GPUTOP_I915_PERF_FIELD_OA_REPORT);
        if (!last_report) {
            last_report = report;
            continue;
        }

        struct gputop_cc_oa_accumulator accumulator;
        gputop_cc_oa_accumulator_init(&accumulator, &ctx->devinfo,
                                      ctx->metric_set, 0, NULL);
        gputop_cc_oa_accumulate_reports(&accumulator, last_report, report);

        for (int c = 0; c < n_counters; c++) {
            struct gputop_metric_set_counter *counter =
                &ctx->metric_set->counters[c];

            float *value = &window->accumulated_values[c * n_accumulated_reports + i];

            switch (counter->data_type) {
            case GPUTOP_PERFQUERY_COUNTER_DATA_UINT64:
            case GPUTOP_PERFQUERY_COUNTER_DATA_UINT32:
            case GPUTOP_PERFQUERY_COUNTER_DATA_BOOL32:
                *value = counter->oa_counter_read_uint64(&ctx->devinfo,
                                                         ctx->metric_set,
                                                         accumulator.deltas);
                break;
            case GPUTOP_PERFQUERY_COUNTER_DATA_DOUBLE:
            case GPUTOP_PERFQUERY_COUNTER_DATA_FLOAT:
                *value = counter->oa_counter_read_float(&ctx->devinfo,
                                                        ctx->metric_set,
                                                        accumulator.deltas);
                break;
            }
        }

        i++;
        last_report = report;
    }

    search_timeline_reports_for_timestamp(window, ctx);
}

static void
display_timeline_window(struct window *win)
{
    struct gputop_client_context *ctx = &context.ctx;
    struct timeline_window *window = (struct timeline_window *) win;

    const uint64_t max_length = ctx->oa_visible_timeline_s * 1000000000ULL;

    if (StartStopSamplingButton(ctx)) { toggle_start_stop_sampling(ctx); }
    ImGui::Text("RCS/Render timeline:"); ImGui::SameLine();
    {
        char time[20];
        gputop_client_pretty_print_value(GPUTOP_PERFQUERY_COUNTER_UNITS_NS,
                                         window->zoom_length == 0 ?
                                         max_length : window->zoom_length,
                                         time, sizeof(time));
        ImGui::Text("time interval : %s", time);
    }

    if (ImGui::Button("Reset zoom##i915")) {
        window->zoom_start = 0;
        window->zoom_length = max_length;
    } ImGui::SameLine();
    if (ImGui::Button("Zoom out##i915")) {
        uint64_t half_zoom = window->zoom_length / 2;
        window->zoom_start = window->zoom_start < half_zoom ? 0ULL :
            (window->zoom_start - half_zoom);
        window->zoom_length = MIN2(2 * window->zoom_length, max_length);
    } ImGui::SameLine();
    if (ImGui::Button("Zoom in##i915")) {
        window->zoom_start += window->zoom_length / 4;
        window->zoom_length /= 2;
    } ImGui::SameLine();
    struct gputop_hw_context *selected_context = NULL;
    if (timeline_select_context(window, ctx, &selected_context)) {
        timeline_focus_on_first_sample(window, ctx, selected_context);
    } ImGui::SameLine();
    if (timeline_select_gt_timestamp(window, ctx)) {
        timeline_focus_on_gt_timestamp(window, ctx, window->gt_timestamp_highlight);
    } ImGui::SameLine();
    timeline_highlight_timestamps(window, ctx);

    if (ImGui::Button("Counters")) { toggle_show_window(&window->counters_window); } ImGui::SameLine();
    if (ImGui::Button("Events")) { toggle_show_window(&window->events_window); } ImGui::SameLine();
    if (ImGui::Button("RCS usage")) { toggle_show_window(&window->usage_window); } ImGui::SameLine();
    if (ImGui::Button("OA reports")) { toggle_show_window(&window->reports_window); }

    uint64_t start_ts, end_ts;
    get_timeline_bounds(window, ctx, true, &start_ts, &end_ts,
                        window->zoom_start, window->zoom_length);

    ImVec2 new_zoom;
    static const char *units[] = { "ns", "us", "ms", "s" };
    char **row_names =
        ensure_timeline_names(_mesa_hash_table_num_entries(ctx->hw_contexts_table));
    uint32_t n_rows = 0;
    list_for_each_entry(struct gputop_hw_context, context, &ctx->hw_contexts, link)
        row_names[n_rows++] = context->name;
    int n_tps = list_length(&ctx->perf_tracepoints);
    Gputop::BeginTimeline("i915-perf-timeline", n_rows, n_tps,
                          end_ts - start_ts,
                          ImVec2(ImGui::GetContentRegionAvailWidth(), 300.0f));

    list_for_each_entry(struct gputop_hw_context, context, &ctx->hw_contexts, link) {
        context->visible_time = end_ts - start_ts;
        context->visible_time_spent = 0UL;
    }

    uint32_t n_entries = 0;
    list_for_each_entry(struct gputop_accumulated_samples, samples, &ctx->timelines, link) {
        if (samples->timestamp_end < start_ts)
            continue;
        if (samples->timestamp_start > end_ts)
            break;

        if (samples->context) {
            samples->context->visible_time_spent +=
                MIN2(samples->timestamp_end, end_ts) -
                MAX2(samples->timestamp_start, start_ts);
        }

        n_entries++;
        assert(samples->context->timeline_row < n_rows);

        if (Gputop::TimelineItem(samples->context->timeline_row,
                                 MAX2(samples->timestamp_start, start_ts) - start_ts,
                                 samples->timestamp_end - start_ts, false)) {
            update_timeline_selected_reports(window, ctx, samples);

            char pretty_time[20];
            gputop_client_pretty_print_value(GPUTOP_PERFQUERY_COUNTER_UNITS_NS,
                                             samples->timestamp_end - samples->timestamp_start,
                                             pretty_time, sizeof(pretty_time));
            ImGui::SetTooltip("%s : %s",
                              samples->context->name, pretty_time);
        }
    }

    for (int i = 0; i < window->n_gt_timestamps_display; i++) {
        if (window->gt_timestamps_display[i] < start_ts ||
            window->gt_timestamps_display[i] > end_ts) {
            continue;
        }
        Gputop::TimelineCustomEvent(window->gt_timestamps_display[i] - start_ts,
                                    Gputop::GetColor(GputopCol_TimelineHighlightedTs));
    }

    const bool separate_timeline =
      !ctx->i915_perf_config.cpu_timestamps && list_length(&ctx->perf_tracepoints) > 0;

    if (separate_timeline) {
        int64_t zoom_start;
        uint64_t zoom_end;
        if (Gputop::EndTimeline(units, ARRAY_SIZE(units),
                                (const char **) row_names,
                            &zoom_start, &zoom_end)) {
            window->zoom_start = MAX2(window->zoom_length == 0 ?
                                      zoom_start : (window->zoom_start + zoom_start),
                                      0);
            window->zoom_length = MIN2(zoom_end - zoom_start, max_length);
        }

        ImGui::Text("Tracepoint timeline:"); ImGui::SameLine();
        if (ImGui::Button("Reset zoom##tp")) {
            window->zoom_tp_start = 0;
            window->zoom_tp_length = max_length;
        } ImGui::SameLine();
        if (ImGui::Button("Zoom out##tp")) {
            uint64_t half_zoom = window->zoom_tp_length / 2;
            window->zoom_tp_start = window->zoom_tp_start < half_zoom ? 0ULL :
                (window->zoom_tp_start - half_zoom);
            window->zoom_tp_length = MIN2(2 * window->zoom_tp_length, max_length);
        } ImGui::SameLine();
        if (ImGui::Button("Zoom in##tp")) {
            window->zoom_tp_start += window->zoom_tp_length / 4;
            window->zoom_tp_length /= 2;
        } ImGui::SameLine();
        {
            char time[20];
            gputop_client_pretty_print_value(GPUTOP_PERFQUERY_COUNTER_UNITS_NS,
                                             window->zoom_tp_length == 0 ?
                                             max_length : window->zoom_tp_length,
                                             time, sizeof(time));
            ImGui::Text("time interval : %s", time);
        }

        get_timeline_bounds(window, ctx, false, &start_ts, &end_ts,
                            window->zoom_tp_start, window->zoom_tp_length);

        Gputop::BeginTimeline("tp-timeline", n_rows, n_tps,
                              end_ts - start_ts,
                              ImVec2(ImGui::GetContentRegionAvailWidth(), 300.0f));
    }

    list_for_each_entry(struct gputop_perf_tracepoint_data, data,
                        &ctx->perf_tracepoints_data, link) {
        if (data->data.time < start_ts)
            continue;
        if (data->data.time > end_ts)
            break;

        if (Gputop::TimelineEvent(data->tp->idx,
                                  MAX2(data->data.time, start_ts) - start_ts,
                                  data->data.time == window->tracepoint_selected_ts)) {
            struct gputop_perf_tracepoint *tp = data->tp;

            char point_desc[200];
            gputop_client_context_print_tracepoint_data(ctx, point_desc, sizeof(point_desc),
                                                        data, true);
            if (!strcmp(tp->name, "drm/drm_vblank_event")) {
                char prev_next[100];
                tracepoint_print_prev_next(ctx, prev_next, sizeof(prev_next), data);
                ImGui::SetTooltip("%s\n%s", point_desc, prev_next);
            } else {
                ImGui::SetTooltip("%s", point_desc);
            }

            memcpy(&window->tracepoint, data->tp, sizeof(window->tracepoint));
        }
    }

    int64_t zoom_start;
    uint64_t zoom_end;
    if (Gputop::EndTimeline(units, ARRAY_SIZE(units),
                            (const char **) row_names,
                            &zoom_start, &zoom_end)) {

        uint64_t *p_z_start = separate_timeline ? &window->zoom_tp_start : &window->zoom_start,
            *p_z_length =  separate_timeline ? &window->zoom_tp_length : &window->zoom_length;

        *p_z_start = MAX2(*p_z_length == 0 ?
                          *p_z_start : (*p_z_start + zoom_start),
                          0);
        *p_z_length = MIN2(zoom_end - zoom_start, max_length);
    }
}

static void
display_timeline_counters(struct window *win)
{
    struct timeline_window *window =
      (struct timeline_window *) container_of(win, window, counters_window);
    struct gputop_client_context *ctx = &context.ctx;

    int n_contexts = _mesa_hash_table_num_entries(ctx->hw_contexts_table);
    ImGui::ColorButton("##selected_context",
                       Gputop::GetHueColor(window->selected_context.timeline_row, n_contexts),
                       ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoTooltip); ImGui::SameLine();
    ImGui::Text("%s", window->selected_context.name); ImGui::SameLine();
    static ImGuiTextFilter filter;
    filter.Draw();

    ImGui::BeginChild("##counters");
    display_i915_perf_counters(ctx, &filter, &window->selected_sample, false);
    ImGui::EndChild();
}

static void
display_timeline_events(struct window *win)
{
    struct timeline_window *window =
      (struct timeline_window *) container_of(win, window, events_window);
    struct gputop_client_context *ctx = &context.ctx;

    uint64_t start_ts, end_ts;
    if (ctx->i915_perf_config.cpu_timestamps) {
        get_timeline_bounds(window, ctx, false, &start_ts, &end_ts,
                            window->zoom_start, window->zoom_length);
    } else {
        get_timeline_bounds(window, ctx, false, &start_ts, &end_ts,
                            window->zoom_tp_start, window->zoom_tp_length);
    }

    int n_items = 0, n_tps = list_length(&ctx->perf_tracepoints);
    window->tracepoint_selected_ts = 0ULL;
    list_for_each_entry(struct gputop_perf_tracepoint_data, data,
                        &ctx->perf_tracepoints_data, link) {
        if (data->data.time < start_ts)
            continue;
        if (data->data.time > end_ts || n_items > 100)
            break;

        char desc[20];
        snprintf(desc, sizeof(desc), "##%p", data);
        ImGui::ColorButton(desc,
                           Gputop::GetHueColor(data->tp->idx, n_tps),
                           ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoTooltip); ImGui::SameLine();
        char point_desc[200];
        gputop_client_context_print_tracepoint_data(ctx, point_desc, sizeof(point_desc), data, true);
        ImGui::Selectable(point_desc);
        if (ImGui::IsItemHovered()) window->tracepoint_selected_ts = data->data.time;

        n_items++;
    }
}

static void
search_timeline_reports_for_column(struct timeline_window *window,
                                   struct gputop_client_context *ctx,
                                   int column)
{
    if (window->searched_timestamp != -1)
        return;

    const struct drm_i915_perf_record_header *last = NULL;
    struct gputop_record_iterator iter;
    int c = 0;
    gputop_record_iterator_init(&iter, &window->selected_sample);
    while (c <= column && gputop_record_iterator_next(&iter)) {
        if (iter.header->type != DRM_I915_PERF_RECORD_SAMPLE)
            continue;

        last = iter.header;
        c++;
    }
    gputop_record_iterator_next(&iter);

    window->hovered_report = column;
    update_timeline_report_range(window, ctx, last, iter.header);
}

static void
display_timeline_reports(struct window *win)
{
    struct timeline_window *window =
      (struct timeline_window *) container_of(win, window, reports_window);
    struct gputop_client_context *ctx = &context.ctx;

    if (ctx->is_sampling || !ctx->metric_set)
        return;

    int n_contexts = _mesa_hash_table_num_entries(ctx->hw_contexts_table);
    ImGui::ColorButton("##selected_context",
                       Gputop::GetHueColor(window->selected_context.timeline_row, n_contexts),
                       ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoTooltip); ImGui::SameLine();
    char pretty_time[20];
    gputop_client_pretty_print_value(GPUTOP_PERFQUERY_COUNTER_UNITS_NS,
                                     window->selected_sample.timestamp_end - window->selected_sample.timestamp_start,
                                     pretty_time, sizeof(pretty_time));
    ImGui::Text("%s: %u reports, %s, %s",
                window->selected_context.name,
                window->n_accumulated_reports, pretty_time,
                window->gt_timestamp_range);

    ImGui::Text("Timestamp search:");
    if (ImGui::InputText("(hexadecimal)",
                         window->timestamp_search, sizeof(window->timestamp_search)))
        search_timeline_reports_for_timestamp(window, ctx);
    ImGui::SameLine();
    if (ImGui::Button("Show OA report")) { toggle_show_window(&window->report_window); }

    static ImGuiTextFilter filter;
    ImGui::Text("Filter counters:"); ImGui::SameLine(); filter.Draw();

    if (window->n_accumulated_reports < 1)
        return;

    ImGui::BeginChild("##reports");

    int32_t new_hovered_column = -1;
    for (int c = 0; c < ctx->metric_set->n_counters; c++) {
        struct gputop_metric_set_counter *counter =
            &ctx->metric_set->counters[c];

        if (!filter.PassFilter(counter->name))
            continue;

        float *values = &window->accumulated_values[c * window->n_accumulated_reports];

        ImGui::PushID(counter);
        int hovered = Gputop::PlotHistogram("", values, window->n_accumulated_reports,
                                            0, window->hovered_report);
        ImGui::PopID();
        ImGui::SameLine();
        if (hovered >= 0) {
            char tooltip_text[80];
            pretty_print_counter_value(counter, values[hovered],
                                       tooltip_text, sizeof(tooltip_text));
            ImGui::SetTooltip("%s", tooltip_text);
            new_hovered_column = hovered;
        }

        if (window->hovered_report >= 0) {
            char hovered_text[80];
            pretty_print_counter_value(counter, values[window->hovered_report],
                                       hovered_text, sizeof(hovered_text));
            ImGui::Text("%s - %s", counter->name, hovered_text);
        } else {
            ImGui::Text("%s", counter->name);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", counter->desc);
        }
    }

    if (window->hovered_report != new_hovered_column)
        search_timeline_reports_for_column(window, ctx, new_hovered_column);

    ImGui::EndChild();
}

static void
display_timeline_report(struct window *win)
{
    struct timeline_window *window =
      (struct timeline_window *) container_of(win, window, report_window);
    struct gputop_client_context *ctx = &context.ctx;

    if (window->hovered_report < 0)
        return;

    struct gputop_record_iterator iter;
    int c = 0;
    gputop_record_iterator_init(&iter, &window->selected_sample);
    while (c <= window->hovered_report && gputop_record_iterator_next(&iter)) {
        if (iter.header->type != DRM_I915_PERF_RECORD_SAMPLE)
            continue;

        c++;
    }

    const uint32_t *report = (const uint32_t *)
        gputop_i915_perf_record_field(&ctx->i915_perf_config, iter.header,
                                      GPUTOP_I915_PERF_FIELD_OA_REPORT);

    for (int i = 0; i < 64; i++)
        ImGui::Text("0x%x", report[i]);
}

static void
display_timeline_usage(struct window *win)
{
    struct timeline_window *window =
      (struct timeline_window *) container_of(win, window, usage_window);
    struct gputop_client_context *ctx = &context.ctx;
    char pretty_time[80];

    gputop_client_pretty_print_value(GPUTOP_PERFQUERY_COUNTER_UNITS_NS,
                                     window->zoom_length,
                                     pretty_time, sizeof(pretty_time));
    ImGui::Text("Total time: %s", pretty_time);

    if (window->zoom_length < 1)
        return;

    uint64_t contexts_time = 0ULL, visible_time = window->zoom_length;
    float pie_ray = MIN2(ImGui::GetContentRegionAvail().y,
                            ImGui::GetWindowContentRegionWidth() / 2);
    int n_clients = list_length(&ctx->hw_contexts) + 1;
    Gputop::BeginPieChart(n_clients, ImVec2(pie_ray, pie_ray)); ImGui::SameLine();
    list_for_each_entry(struct gputop_hw_context, context, &ctx->hw_contexts, link) {
        if (context->visible_time == 0) /* Data not computed yet. */
            continue;

        visible_time = context->visible_time;
        double percent = (double) context->visible_time_spent / context->visible_time;
        if (Gputop::PieChartItem(percent)) {
            gputop_client_pretty_print_value(GPUTOP_PERFQUERY_COUNTER_UNITS_NS,
                                             context->visible_time_spent,
                                             pretty_time, sizeof(pretty_time));
            ImGui::SetTooltip("%s: %s", context->name, pretty_time);
        }

        contexts_time += context->visible_time_spent;
    }

    uint64_t idle_time = visible_time - contexts_time;
    double idle_percent = (double) idle_time / visible_time;
    if (Gputop::PieChartItem(idle_percent)) {
        gputop_client_pretty_print_value(GPUTOP_PERFQUERY_COUNTER_UNITS_NS,
                                         idle_time, pretty_time, sizeof(pretty_time));
        ImGui::SetTooltip("Idle: %s", pretty_time);
    }
    Gputop::EndPieChart();

    ImGui::BeginChild("#contextlist");
    contexts_time = 0ULL;
    list_for_each_entry(struct gputop_hw_context, context, &ctx->hw_contexts, link) {
        ImGui::ProgressBar((double) context->visible_time_spent / context->visible_time,
                           ImVec2(ImGui::GetWindowContentRegionWidth() / 2.0f, 0));
        ImGui::SameLine();
        gputop_client_pretty_print_value(GPUTOP_PERFQUERY_COUNTER_UNITS_NS,
                                         context->visible_time_spent,
                                         pretty_time, sizeof(pretty_time));
        ImGui::Text("%s: %s", context->name, pretty_time);
    }

    ImGui::ProgressBar(idle_percent, ImVec2(ImGui::GetWindowContentRegionWidth() / 2.0f, 0));
    ImGui::SameLine();
    gputop_client_pretty_print_value(GPUTOP_PERFQUERY_COUNTER_UNITS_NS,
                                     idle_time, pretty_time, sizeof(pretty_time));
    ImGui::Text("Idle: %s", pretty_time);
    ImGui::EndChild();
}

static void
hide_timeline_window(struct window *win)
{
    struct timeline_window *window = (struct timeline_window *) win;

    window->reports_window.opened = false;
    window->usage_window.opened = false;
}

static void
show_timeline_window(void)
{
    struct timeline_window *window = &context.timeline_window;

    if (window->base.opened) {
        window->base.opened = false;
        window->reports_window.opened = false;
        return;
    }

    snprintf(window->base.name, sizeof(window->base.name),
             "i915 perf timeline##%p", window);
    window->base.size = ImVec2(800, 400);
    window->base.display = display_timeline_window;
    window->base.destroy = hide_timeline_window;
    window->base.opened = true;

    snprintf(window->counters_window.name, sizeof(window->counters_window.name),
             "i915 perf timeline counters##%p", &window->counters_window);
    window->counters_window.size = ImVec2(400, 400);
    window->counters_window.display = display_timeline_counters;
    window->counters_window.destroy = hide_window;
    window->counters_window.opened = false;

    snprintf(window->events_window.name, sizeof(window->events_window.name),
             "i915 perf timeline events##%p", &window->events_window);
    window->events_window.size = ImVec2(400, 400);
    window->events_window.display = display_timeline_events;
    window->events_window.destroy = hide_window;
    window->events_window.opened = false;

    snprintf(window->reports_window.name, sizeof(window->reports_window.name),
             "i915 perf timeline reports##%p", &window->reports_window);
    window->reports_window.size = ImVec2(400, 400);
    window->reports_window.display = display_timeline_reports;
    window->reports_window.destroy = hide_window;
    window->reports_window.opened = false;

    snprintf(window->report_window.name, sizeof(window->report_window.name),
             "i915 perf report view##%p", &window->report_window);
    window->report_window.size = ImVec2(400, 400);
    window->report_window.display = display_timeline_report;
    window->report_window.destroy = hide_window;
    window->report_window.opened = false;

    snprintf(window->usage_window.name, sizeof(window->usage_window.name),
             "i915 perf timeline usage##%p", &window->usage_window);
    window->usage_window.size = ImVec2(500, 300);
    window->usage_window.display = display_timeline_usage;
    window->usage_window.destroy = hide_window;
    window->usage_window.opened = false;

    window->searched_timestamp = -1;

    window->zoom_start = window->zoom_tp_start = 0;
    struct gputop_client_context *ctx = &context.ctx;
    window->zoom_length = window->zoom_tp_length =
        ctx->oa_visible_timeline_s * 1000000000ULL;

    list_add(&window->base.link, &context.windows);
}

/**/

static void
display_tracepoints_window(struct window *win)
{
    struct gputop_client_context *ctx = &context.ctx;

    ImGui::Columns(3);

    ImGui::BeginChild("##column1");
    ImGui::Text("Available tracepoints");
    ImGui::Separator();
    static ImGuiTextFilter filter;
    filter.Draw();
    ImGui::BeginChild("##tracepoints");
    if (ctx->features) {
        for (unsigned i = 0; i < ctx->features->features->n_tracepoints; i++) {
            if (filter.PassFilter(ctx->features->features->tracepoints[i]) &&
                ImGui::Selectable(ctx->features->features->tracepoints[i]))
                gputop_client_context_add_tracepoint(ctx, ctx->features->features->tracepoints[i]);
        }
    }
    ImGui::EndChild();
    ImGui::EndChild();

    ImGui::NextColumn();
    ImGui::BeginChild("##column2");
    ImGui::Text("Tracepoint format");
    ImGui::Separator();
    if (ctx->tracepoint_info) {
        ImGui::Text("event_id=%u", ctx->tracepoint_info->tracepoint_info->event_id);
        ImGui::InputTextMultiline("##format",
                                  ctx->tracepoint_info->tracepoint_info->sample_format,
                                  strlen(ctx->tracepoint_info->tracepoint_info->sample_format),
                                  ImGui::GetContentRegionAvail(),
                                  ImGuiInputTextFlags_ReadOnly);
    }
    ImGui::EndChild();

    ImGui::NextColumn();
    ImGui::BeginChild("##column3");
    ImGui::Text("Selected tracepoints");
    ImGui::Separator();
    list_for_each_entry_safe(struct gputop_perf_tracepoint, tp, &ctx->perf_tracepoints, link) {
        if (ImGui::Selectable(tp->name)) {
            gputop_client_context_remove_tracepoint(ctx, tp);
        }
    }
    ImGui::EndChild();
}

static void
show_tracepoints_window(void)
{
    struct window *window = &context.tracepoints_window;

    if (window->opened) {
        window->opened = false;
        return;
    }

    snprintf(window->name, sizeof(window->name),
             "Tracepoints##%p", window);
    window->size = ImVec2(800, 300);
    window->display = display_tracepoints_window;
    window->destroy = hide_window;
    window->opened = true;

    list_add(&window->link, &context.windows);
}

/**/

static void
display_events_window(struct window *win)
{
    struct gputop_client_context *ctx = &context.ctx;

    ImGui::Text("Available events");
    ImGui::Separator();
    static ImGuiTextFilter filter;
    filter.Draw();
    ImGui::BeginChild("##events");
    if (ctx->features) {
        for (unsigned i = 0; i < ctx->features->features->n_events; i++) {
            if (filter.PassFilter(ctx->features->features->events[i]) &&
                ImGui::Selectable(ctx->features->features->events[i])) {
            }
        }
    }
    ImGui::EndChild();
}

static void
show_events_window(void)
{
    struct window *window = &context.tracepoints_window;

    if (window->opened) {
        window->opened = false;
        return;
    }

    snprintf(window->name, sizeof(window->name),
             "Events##%p", window);
    window->size = ImVec2(800, 300);
    window->display = display_events_window;
    window->destroy = hide_window;
    window->opened = true;

    list_add(&window->link, &context.windows);
}

/**/

static void
display_log_window(struct window *win)
{
    struct gputop_client_context *ctx = &context.ctx;

    if (ImGui::Button("Clear")) { gputop_client_context_clear_logs(ctx); clear_client_logs(); }

    ImGui::Columns(2);
    ImGui::Text("Server:");
    ImGui::BeginChild(ImGui::GetID("##server"));
    for (int i = 0; i < ctx->n_messages; i++) {
        int idx = (ctx->start_message + i) % ARRAY_SIZE(ctx->messages);
        ImGui::Text("%s", ctx->messages[idx].msg);
    }
    ImGui::EndChild();

    ImGui::NextColumn();
    ImGui::Text("Client:");
    ImGui::BeginChild(ImGui::GetID("##client"));
    for (int i = 0; i < context.n_messages; i++) {
        int idx = (context.start_message + i) % ARRAY_SIZE(ctx->messages);
        ImGui::Text("%s", context.messages[idx].msg);
    }
    ImGui::EndChild();
}

static void
show_log_window(void)
{
    struct window *window = &context.log_window;

    if (window->opened) {
        window->opened = false;
        return;
    }

    snprintf(window->name, sizeof(window->name), "Server logs");
    window->size = ImVec2(400, 200);
    window->display = display_log_window;
    window->opened = true;
    window->destroy = hide_window;

    list_add(&window->link, &context.windows);
}

/**/

static void
display_style_editor_window(struct window *win)
{
    ImGuiColorEditFlags cflags = (ImGuiColorEditFlags_NoAlpha |
                                  ImGuiColorEditFlags_NoInputs);
    ImGui::ColorEdit3("background", (float *)&context.clear_color, cflags);
    Gputop::DisplayColorsProperties();
}

static void
show_style_editor_window(void)
{
    struct window *window = &context.style_editor_window;

    if (window->opened) {
        window->opened = false;
        return;
    }

    snprintf(window->name, sizeof(window->name), "Style editor");
    window->size = ImVec2(400, 200);
    window->display = display_style_editor_window;
    window->opened = true;
    window->destroy = hide_window;

    list_add(&window->link, &context.windows);
}

/**/

static void
display_streams_window(struct window *win)
{
    struct gputop_client_context *ctx = &context.ctx;

    ImGui::Columns(2);
    ImGui::Text("Streams:");
    list_for_each_entry(struct gputop_stream, stream, &ctx->streams, link) {
        ImGui::Text("id=%i", stream->id); ImGui::SameLine();
        ImGui::ProgressBar(stream->fill / 100.0);
    }
    ImGui::NextColumn();
    list_for_each_entry(struct gputop_perf_tracepoint, tp, &ctx->perf_tracepoints, link) {
        ImGui::Text("tp=%s id=%i", tp->name, tp->event_id);
    }
    ImGui::Text("n_timelines=%i", ctx->n_timelines);
    ImGui::Text("n_graphs=%i", ctx->n_graphs);
    ImGui::Text("n_cpu_stats=%i", ctx->n_cpu_stats);

    list_for_each_entry(struct gputop_perf_tracepoint_data, data,
                        &ctx->perf_tracepoints_data, link) {
        ImGui::Text("%s time=%" PRIx64, data->tp->name, data->data.time);
    }
}

static void
show_streams_window(void)
{
    struct window *window = &context.streams_window;

    if (window->opened) {
        window->opened = false;
        return;
    }

    snprintf(window->name, sizeof(window->name), "Streams");
    window->size = ImVec2(400, 200);
    window->display = display_streams_window;
    window->opened = true;
    window->destroy = hide_window;

    list_add(&window->link, &context.windows);
}

/**/

static float *
get_cpus_stats(struct gputop_client_context *ctx, int max_cpu_stats)
{
    int n_cpus = ctx->features ? ctx->features->features->n_cpus : 1;
    float *values = ensure_plot_accumulator(n_cpus * max_cpu_stats);
    int i;

    for (i = 0; i < max_cpu_stats - ctx->n_cpu_stats; i++) {
        for (int cpu = 0; cpu < n_cpus; cpu++)
            values[n_cpus * i + cpu] = 0.0f;
    }

    struct gputop_cpu_stat *stat = list_first_entry(&ctx->cpu_stats,
                                                    struct gputop_cpu_stat, link);
    for (; i < (max_cpu_stats - 1); i++) {
        struct gputop_cpu_stat *next = list_first_entry(&stat->link,
                                                        struct gputop_cpu_stat, link);
        assert(&next->link != &ctx->cpu_stats);

        for (int cpu = 0; cpu < n_cpus; cpu++) {
            Gputop__CpuStats *cpu_stat0 = stat->stat->cpu_stats->cpus[cpu];
            Gputop__CpuStats *cpu_stat1 = next->stat->cpu_stats->cpus[cpu];
            uint32_t total = ((cpu_stat1->user       - cpu_stat0->user) +
                              (cpu_stat1->nice       - cpu_stat0->nice) +
                              (cpu_stat1->system     - cpu_stat0->system) +
                              (cpu_stat1->idle       - cpu_stat0->idle) +
                              (cpu_stat1->iowait     - cpu_stat0->iowait) +
                              (cpu_stat1->irq        - cpu_stat0->irq) +
                              (cpu_stat1->softirq    - cpu_stat0->softirq) +
                              (cpu_stat1->steal      - cpu_stat0->steal) +
                              (cpu_stat1->guest      - cpu_stat0->guest) +
                              (cpu_stat1->guest_nice - cpu_stat0->guest_nice));
            if (total == 0)
                values[n_cpus * i + cpu] = 0.0f;
            else {
                values[n_cpus * i + cpu] =
                    100.0f - 100.f * (float) (cpu_stat1->idle - cpu_stat0->idle) / total;
            }
        }

        stat = next;
    }

    return values;
}

struct cpu_stat_getter {
    float *values;
    int n_cpus;
};

static float
get_cpu_stat_item(void *data, int line, int idx)
{
    struct cpu_stat_getter *getter = (struct cpu_stat_getter *) data;

    return getter->values[idx * getter->n_cpus + line];
}

static void
display_cpu_stats(void)
{
    struct gputop_client_context *ctx = &context.ctx;
    int n_cpus = ctx->features ? ctx->features->features->n_cpus : 1;
    int max_cpu_stats =
        (int) (ctx->cpu_stats_visible_timeline_s * 1000.0f) /
        ctx->cpu_stats_sampling_period_ms;

    struct cpu_stat_getter getter = { get_cpus_stats(ctx, max_cpu_stats), n_cpus };
    char title[20];
    snprintf(title, sizeof(title), "%i CPU(s)", n_cpus);
    Gputop::PlotMultilines("",
                           &get_cpu_stat_item, &getter,
                           n_cpus, max_cpu_stats - 1, 0,
                           context.cpu_colors,
                           title, 0.0f, 100.0f,
                           ImVec2(ImGui::GetContentRegionAvailWidth(), 100.0f));
}

static bool
select_metric_set(struct gputop_client_context *ctx,
                  const struct gputop_metric_set **out_metric_set)
{
    bool selected = false;
    static ImGuiTextFilter filter;
    filter.Draw();

    if (!ctx->features) return false;

    ImGui::BeginChild("##block");
    for (unsigned u = 0; u < ctx->features->features->n_supported_oa_uuids; u++) {
        const struct gputop_metric_set *metric_set =
            gputop_client_context_uuid_to_metric_set(ctx,
                                                     ctx->features->features->supported_oa_uuids[u]);
        if (!metric_set) continue;

        if (filter.PassFilter(metric_set->name) &&
            ImGui::Selectable(metric_set->name)) {
            *out_metric_set = metric_set;
            selected = true;
        }
    }
    ImGui::EndChild();

    return selected;
}

static bool
select_metric_set_from_group_counter(struct gputop_counter_group *group,
                                     ImGuiTextFilter *filter,
                                     const struct gputop_metric_set **out_metric_set)
{
    bool selected = false;
    ImGuiTreeNodeFlags flags =
        ImGuiTreeNodeFlags_Framed |
        (filter->IsActive() ? ImGuiTreeNodeFlags_DefaultOpen : 0);

    list_for_each_entry(struct gputop_counter_group, subgroup, &group->groups, link) {
        if (ImGui::TreeNodeEx(subgroup, flags, "%s", subgroup->name)) {
            if (select_metric_set_from_group_counter(subgroup, filter, out_metric_set)) {
                selected = true;
            }
            ImGui::TreePop();
        }
    }

    list_for_each_entry(struct gputop_metric_set_counter, counter,
                        &group->counters, link) {
        if (!filter->PassFilter(counter->name))
            continue;

        char counter_name[120];
        snprintf(counter_name, sizeof(counter_name),
                 "%s (%s)", counter->name, counter->metric_set->name);

        if (ImGui::Selectable(counter_name)) {
            *out_metric_set = counter->metric_set;
            selected = true;
        }
    }

    return selected;
}

static bool
select_metric_set_from_counter(struct gputop_client_context *ctx,
                               const struct gputop_metric_set **out_metric_set)
{
    static ImGuiTextFilter filter;
    filter.Draw();

    if (!ctx->features) return false;

    ImGui::BeginChild("##block");
    bool selected =
        select_metric_set_from_group_counter(ctx->gen_metrics->root_group,
                                             &filter,
                                             out_metric_set);
    ImGui::EndChild();

    return selected;
}

static void
maybe_restart_sampling(struct gputop_client_context *ctx)
{
    if (ctx->is_sampling) {
        gputop_client_context_stop_sampling(ctx);
        gputop_client_context_start_sampling(ctx);
    }
}

static bool
select_oa_exponent(struct gputop_client_context *ctx, uint64_t *ns)
{
    bool open_popup = ImGui::Button("OA exponents");
    if (open_popup)
        ImGui::OpenPopup("oa exponent picker");
    ImGui::SetNextWindowSize(ImVec2(200, 400));

    bool selected = false;
    if (ImGui::BeginPopup("oa exponent picker")) {
        if (ctx->connection) {
            for (uint32_t e = 1; e < 32; e++) {
                uint64_t duration_ns = gputop_oa_exponent_to_period_ns(&ctx->devinfo, e);
                char pretty_duration[200];
                gputop_client_pretty_print_value(GPUTOP_PERFQUERY_COUNTER_UNITS_NS,
                                                 duration_ns,
                                                 pretty_duration, sizeof(pretty_duration));

                if (ImGui::Selectable(pretty_duration)) {
                    ImGui::CloseCurrentPopup();
                    *ns = duration_ns;
                    selected = true;
                }
            }
        }

        ImGui::EndPopup();
    }

    return selected;
}

static void
display_main_window(struct window *win)
{
    struct gputop_client_context *ctx = &context.ctx;
    char buf[80];

    if (ImGui::Button("Style editor")) { show_style_editor_window(); } ImGui::SameLine();
    int n_messages = context.n_messages + ctx->n_messages;
    if (n_messages > 0)
        snprintf(buf, sizeof(buf), "Logs (%i)", n_messages);
    else
        snprintf(buf, sizeof(buf), "Logs");
    if (ImGui::Button(buf)) { show_log_window(); } ImGui::SameLine();
    if (ImGui::Button("Report")) { show_report_window(); } ImGui::SameLine();
    if (ImGui::Button("Streams")) { show_streams_window(); }

    if (ImGui::InputText("Address", context.host_address,
                         sizeof(context.host_address),
                         ImGuiInputTextFlags_EnterReturnsTrue)) {
        reconnect();
    }
    if (ImGui::InputInt("Port", &context.host_port, 1, 100,
                        ImGuiInputTextFlags_EnterReturnsTrue)) {
        reconnect();
    }
    if (ImGui::Button("Connect")) { reconnect(); } ImGui::SameLine();
    ImGui::Text("Status:"); ImGui::SameLine();
    bool is_connected = (ctx->connection &&
                         gputop_connection_connected(ctx->connection));
    ImColor color = is_connected ? ImColor(0.0f, 1.0f, 0.0f) : ImColor(0.9f, 0.0f, 0.0f);
    const char *connection_status = ctx->connection ?
        (is_connected ? "Connected" : "Connecting...") :
        (context.connection_error ? context.connection_error : "Not connected");
    ImGui::TextColored(color, "%s", connection_status);

    /* CPU */
    ImGui::Separator();
    if (ctx->features) {
        ImGui::Text("CPU model: %s", ctx->features->features->cpu_model);
        ImGui::Text("Kernel release: %s", ctx->features->features->kernel_release);
        ImGui::Text("Kernel build: %s", ctx->features->features->kernel_build);
    }
    int vcpu = ctx->cpu_stats_sampling_period_ms;
    if (ImGui::InputInt("CPU sampling period (ms)", &vcpu)) {
        vcpu = CLAMP(vcpu, 1, 1000);
        if (vcpu != ctx->cpu_stats_sampling_period_ms) {
            gputop_client_context_update_cpu_stream(ctx, vcpu);
        }
    }
    ImGui::SliderFloat("CPU visible sampling (s)",
                       &ctx->cpu_stats_visible_timeline_s, 0.1f, 15.0f);
    if (ImGui::Button("Select events")) { show_events_window(); } ImGui::SameLine();
    snprintf(buf, sizeof(buf), "Select tracepoints (%i)", list_length(&ctx->perf_tracepoints));
    if (ImGui::Button(buf)) { show_tracepoints_window(); } ImGui::SameLine();
    if (ImGui::Button("Default tracepoints") && ctx->connection) {
        gputop_client_context_add_tracepoint(ctx, "drm/drm_vblank_event");
        gputop_client_context_add_tracepoint(ctx, "i915/i915_request_add");
        gputop_client_context_add_tracepoint(ctx, "i915/i915_request_retire");
    }
    display_cpu_stats();


    /* GPU */
    ImGui::Separator();
    if (ctx->features) {
        const struct gputop_devinfo *devinfo = &ctx->devinfo;
        ImGui::Text("GT name: %s (Gen %u, PCI 0x%x)",
                    devinfo->prettyname, devinfo->gen, devinfo->devid);
        ImGui::Text("%" PRIu64 " threads, %" PRIu64 " EUs, %" PRIu64 " slices, %" PRIu64 " subslices",
                    devinfo->eu_threads_count, devinfo->n_eus,
                    devinfo->n_eu_slices, devinfo->n_eu_sub_slices);
        ImGui::Text("GT frequency range %.1fMHz / %.1fMHz",
                    (double) devinfo->gt_min_freq / 1000000.0f,
                    (double) devinfo->gt_max_freq / 1000000.0f);
        ImGui::Text("CS timestamp frequency %" PRIu64 " Hz / %.2f ns",
                    devinfo->timestamp_frequency,
                    1000000000.0f / devinfo->timestamp_frequency);

        ImGui::Text("RCS busyness:"); ImGui::SameLine();
        ImGui::ProgressBar(gputop_client_context_calc_busyness(ctx),
                           ImVec2(ImGui::GetContentRegionAvailWidth(), 0.0f));

        bool open_popup = ImGui::Button("Select metric");
        if (open_popup)
            ImGui::OpenPopup("metric picker");
        ImGui::SetNextWindowSize(ImVec2(400, 400));
        if (ImGui::BeginPopup("metric picker")) {
            if (select_metric_set(ctx, &ctx->metric_set)) {
                maybe_restart_sampling(ctx);
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        } ImGui::SameLine();
        open_popup = ImGui::Button("Select metric set from counter");
        if (open_popup)
            ImGui::OpenPopup("metric counter picker");
        ImGui::SetNextWindowSize(ImVec2(600, 400));
        if (ImGui::BeginPopup("metric counter picker")) {
            if (select_metric_set_from_counter(ctx, &ctx->metric_set)) {
                maybe_restart_sampling(ctx);
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        } ImGui::SameLine();
        ImColor color = ctx->metric_set ? ImColor(0.0f, 1.0f, 0.0f) : ImColor(0.9f, 0.0f, 0.0f);
        ImGui::TextColored(color, "%s",
                           ctx->metric_set ? ctx->metric_set->name : "No metric set selected");
    }
    int oa_sampling_period_ms = ctx->oa_aggregation_period_ns / 1000000ULL;
    if (ImGui::InputInt("OA sampling period (ms)", &oa_sampling_period_ms))
        ctx->oa_aggregation_period_ns = CLAMP(oa_sampling_period_ms, 1, 1000) * 1000000ULL;
    uint64_t oa_sampling_period_ns = 0ULL;
    if (select_oa_exponent(ctx, &oa_sampling_period_ns))
        ctx->oa_sampling_period_ns = oa_sampling_period_ns;
    ImGui::SameLine();
    char pretty_bandwidth[20];
    gputop_client_pretty_print_value(GPUTOP_PERFQUERY_COUNTER_UNITS_BYTES,
                                     gputop_i915_perf_record_max_size(&ctx->i915_perf_config) *
                                     (1000000000ULL / ctx->oa_sampling_period_ns),
                                     pretty_bandwidth, sizeof(pretty_bandwidth));
    char pretty_sampling[20];
    gputop_client_pretty_print_value(GPUTOP_PERFQUERY_COUNTER_UNITS_NS,
                                     ctx->oa_sampling_period_ns,
                                     pretty_sampling, sizeof(pretty_sampling));
    ImGui::Text("OA sampling at %s, bandwidth from server: %s/s",
                pretty_sampling, pretty_bandwidth);
    ImGui::SliderFloat("OA visible sampling (s)",
                       &ctx->oa_visible_timeline_s, 0.1f, 15.0f);
    if (StartStopSamplingButton(ctx)) { toggle_start_stop_sampling(ctx); } ImGui::SameLine();
    if (ImGui::Button("Live counters")) { show_live_i915_perf_counters_window(); } ImGui::SameLine();
    if (ImGui::Button("Live usage")) { show_live_i915_perf_usage_window(); }
    ImGui::Text("Timelines:"); ImGui::SameLine();
    if (ImGui::Button("Global")) { show_global_i915_perf_window(); } ImGui::SameLine();
    if (ImGui::Button("Per contexts")) { show_contexts_i915_perf_window(); } ImGui::SameLine();
    if (ImGui::Button("Multi contexts")) { show_timeline_window(); }

    if (ctx->features) {
        static bool show_topology = true;
        ImGui::Checkbox("Show topology", &show_topology);
        if (show_topology) {
            ImGui::BeginChild("##topology");
            const Gputop__DevInfo *devinfo = ctx->features->features->devinfo;

            const char *engine_names[] = {
                "other", "rcs", "blt", "vcs", "vecs",
            };
            assert(devinfo->topology->n_engines <= ARRAY_SIZE(engine_names));
            Gputop::EngineTopology("##engines",
                                   devinfo->topology->n_engines,
                                   devinfo->topology->engines,
                                   engine_names,
                                   ImGui::GetWindowContentRegionWidth());
            Gputop::RcsTopology("##topology",
                                devinfo->topology->max_slices,
                                devinfo->topology->max_subslices,
                                devinfo->topology->max_eus_per_subslice,
                                devinfo->topology->slices_mask.data,
                                devinfo->topology->subslices_mask.data,
                                devinfo->topology->eus_mask.data,
                                ImGui::GetWindowContentRegionWidth());
            ImGui::EndChild();
        }
    }
}

static void
show_main_window(void)
{
    struct window *window = &context.main_window;

    if (window->opened)
        return;

    snprintf(window->name, sizeof(window->name), "GPUTop");
    window->size = ImVec2(-1, 600);
    window->position = ImVec2(0, 0);
    window->opened = true;
    window->display = display_main_window;
    window->destroy = NULL;

    list_add(&window->link, &context.windows);
}

/**/

static void
display_windows(void)
{
    list_for_each_entry(struct window, window, &context.windows, link) {
        ImGui::SetNextWindowPos(window->position, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(window->size, ImGuiCond_FirstUseEver);

        ImGui::Begin(window->name, &window->opened);
        window->display(window);
        window->position = ImGui::GetWindowPos();
        window->size = ImGui::GetWindowSize();
        ImGui::End();
    }

    list_for_each_entry_safe(struct window, window, &context.windows, link) {
        if (window->opened) continue;

        if (window->destroy) {
            list_del(&window->link);
            window->destroy(window);
        } else
            window->opened = true;
    }
}

static void
init_ui(const char *host, int port)
{
    memset(&context.ctx, 0, sizeof(context.ctx));

    list_inithead(&context.windows);

    ImGuiIO& io = ImGui::GetIO();
    io.NavFlags |= ImGuiNavFlags_EnableKeyboard;

    context.clear_color = ImColor(114, 144, 154);

    Gputop::InitColorsProperties();

    gputop_client_context_init(&context.ctx);

    snprintf(context.host_address, sizeof(context.host_address),
             "%s", host ? host : "localhost");
    context.host_port = port != 0 ? port : 7890;

    if (host != NULL)
        reconnect();
}

/**/

#ifdef EMSCRIPTEN
static void
repaint_window(void *user_data)
{
    SDL_Event event;
    while (SDL_PollEvent(&event))
    {
        ImGui_ImplSdlGLES2_ProcessEvent(&event);
    }

    ImGui_ImplSdlGLES2_NewFrame(context.window);

    show_main_window();

    display_windows();

    glViewport(0, 0, (int)ImGui::GetIO().DisplaySize.x, (int)ImGui::GetIO().DisplaySize.y);
    glClearColor(context.clear_color.x, context.clear_color.y, context.clear_color.z, 1.0);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui::Render();
    ImGui_ImplSdlGLES2_RenderDrawData(ImGui::GetDrawData());
    SDL_GL_SwapWindow(context.window);
}

extern "C" EMSCRIPTEN_KEEPALIVE void
resize_window_callback(int width, int height)
{
    SDL_SetWindowSize(context.window, width, height);
}
#elif defined(GPUTOP_UI_GTK)
static void
repaint_window(CoglOnscreen *onscreen, void *user_data)
{
    ImGui_ImplGtk3Cogl_NewFrame();

    show_main_window();

    display_windows();

    /* Rendering */
    {
        CoglFramebuffer *fb = COGL_FRAMEBUFFER(onscreen);
        cogl_framebuffer_set_viewport(fb,
                                      0, 0,
                                      cogl_framebuffer_get_width(fb),
                                      cogl_framebuffer_get_height(fb));

        cogl_framebuffer_clear4f(fb, COGL_BUFFER_BIT_COLOR | COGL_BUFFER_BIT_DEPTH,
                                 context.clear_color.x,
                                 context.clear_color.y,
                                 context.clear_color.z, 1.0);
        ImGui::Render();
        ImGui_ImplGtk3Cogl_RenderDrawData(ImGui::GetDrawData());
        cogl_onscreen_swap_buffers(onscreen);
    }
}
#elif defined(GPUTOP_UI_GLFW)
static void
repaint_window(void *user_data)
{
    if (glfwWindowShouldClose(context.window)) {
        ImGui_ImplGlfwGL3_Shutdown();
        glfwTerminate();
        uv_stop(uv_default_loop());
        return;
    }

    ImGui_ImplGlfwGL3_NewFrame();

    show_main_window();

    display_windows();

    int display_w, display_h;
    glfwGetFramebufferSize(context.window, &display_w, &display_h);
    glViewport(0, 0, display_w, display_h);
    glClearColor(context.clear_color.x,
                 context.clear_color.y,
                 context.clear_color.z, 1.0);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui::Render();
    ImGui_ImplGlfwGL3_RenderDrawData(ImGui::GetDrawData());
    glfwSwapBuffers(context.window);
}
#endif

/* Native part */

int
main(int argc, char *argv[])
{
#ifdef EMSCRIPTEN
    /* Setup SDL */
    if (SDL_Init(SDL_INIT_VIDEO|SDL_INIT_TIMER) != 0)
    {
        printf("Error: %s\n", SDL_GetError());
        return -1;
    }

    /* Setup window */
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_DisplayMode current;
    SDL_GetCurrentDisplayMode(0, &current);
    context.window = SDL_CreateWindow("GPUTop",
                                      SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 720,
                                      SDL_WINDOW_OPENGL|SDL_WINDOW_RESIZABLE);
    SDL_GL_CreateContext(context.window);

    ImGui::CreateContext();
    ImGui_ImplSdlGLES2_Init(context.window, repaint_window, NULL);

    char host[128];
    memset(host, 0, sizeof(host));
    EM_ASM({
        var uriParams = new URLSearchParams(window.location.search);
        if (!uriParams.has('remoteHost'))
          return;
        var host = uriParams.get('remoteHost');
        var numBytesWritten = stringToUTF8(host, $0, $1);
      }, host, sizeof(host));

    int port = EM_ASM_INT({
        var uriParams = new URLSearchParams(window.location.search);
        if (!uriParams.has('remotePort'))
          return 0;
        return parseInt(uriParams.get('remotePort'));
      });
    EM_ASM({
            var resizeFunc = function() {
                Module['_resize_window_callback'](window.innerWidth, window.innerHeight);
            };
            window.addEventListener("resize", resizeFunc);
            resizeFunc();
        }, context.window);

    init_ui(host[0] != '\0' ? host : NULL, port);
#elif defined(GPUTOP_UI_GTK)
    g_autofree gchar *host = NULL;
    GOptionEntry entries[] = {
        { "host", 'h', 0, G_OPTION_ARG_STRING, &host, "Connect a host", "hostname", },
        {   NULL,   0, 0, G_OPTION_ARG_NONE,    NULL,             NULL,       NULL, },
    };

    gtk_init(&argc, &argv);

    g_autoptr(GOptionContext) context = NULL;
    context = g_option_context_new(NULL);
    g_option_context_set_ignore_unknown_options(context, TRUE);
    g_option_context_set_help_enabled(context, FALSE);
    g_option_context_add_main_entries(context, entries, NULL);
    g_option_context_parse(context, &argc, &argv, NULL);

    GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "GPUTop");
    g_signal_connect(window, "delete-event", G_CALLBACK(gtk_main_quit), NULL);
    gtk_window_resize(GTK_WINDOW(window), 1280, 720);

    GtkWidget *box = gtk_event_box_new();
    gtk_widget_set_can_focus(box, TRUE);
    gtk_container_add(GTK_CONTAINER(window), box);
    gtk_widget_show_all(window);

    ImGui::CreateContext();
    ImGui_ImplGtk3Cogl_Init(box, repaint_window, NULL);

    if (host) {
        g_autofree gchar *url = g_strdup_printf("gputop://%s", host);
        g_autoptr(SoupURI) uri = soup_uri_new(url);
        init_ui(soup_uri_get_host(uri), soup_uri_get_port(uri));
    } else
        init_ui(NULL, 0);

    gtk_main();

    ImGui::DestroyContext();
#elif defined(GPUTOP_UI_GLFW)
    const struct option long_options[] = {
        { "host",              required_argument,  0, 'h' },
        { 0, 0, 0, 0 }
    };
    char *host = NULL;
    int opt, port = 0;

    while ((opt = getopt_long(argc, argv, "h:", long_options, NULL)) != -1) {
        switch (opt) {
        case 'h': {
            char *port_str;

            if ((port_str = strstr(optarg, ":")) != NULL) {
                host = (char *) calloc(port_str - optarg, sizeof(char));
                memcpy(host, optarg, port_str - optarg);
                port = atoi(port_str + 1);
            }
            break;
        }
        }
    }

    if (!glfwInit())
        return -1;

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    context.window = glfwCreateWindow(1280, 720, "GPUTop", NULL, NULL);
    glfwMakeContextCurrent(context.window);
    glfwSwapInterval(1); // Enable vsync

    ImGui::CreateContext();
    if (!ImGui_ImplGlfwGL3_Init(context.window, repaint_window, NULL))
        return -1;

    init_ui(host, port);

    uv_run(uv_default_loop(), UV_RUN_DEFAULT);

    ImGui::DestroyContext();
#endif

    return EXIT_SUCCESS;
}
