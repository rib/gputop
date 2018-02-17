// ImGui GLFW binding with OpenGL3 + shaders
// In this binding, ImTextureID is used to store an OpenGL 'GLuint' texture identifier. Read the FAQ about ImTextureID in imgui.cpp.

// You can copy and use unmodified imgui_impl_* files in your project. See main.cpp for an example of using this.
// If you use this binding you'll need to call 4 functions: ImGui_ImplXXXX_Init(), ImGui_ImplXXXX_NewFrame(), ImGui::Render() and ImGui_ImplXXXX_Shutdown().
// If you are new to ImGui, see examples/README.txt and documentation at the top of imgui.cpp.
// https://github.com/ocornut/imgui

#include <stdio.h>

#include <imgui.h>
#include "imgui_impl_gtk3_cogl.h"

#if defined(GDK_WINDOWING_WAYLAND)
#include <gdk/gdkwayland.h>
#endif
#if defined(GDK_WINDOWING_X11)
#include <gdk/gdkx.h>
#endif
#if defined(GDK_WINDOWING_WIN32)
#include <gdk/win32/gdkwin32.h>
#endif

#if defined(COGL_HAS_EGL_PLATFORM_WAYLAND_SUPPORT)
#include <cogl/cogl-wayland-client.h>
#endif
#if defined(COGL_HAS_XLIB_SUPPORT)
#include <cogl/cogl-xlib.h>
#endif

#define ARRAY_SIZE(arg) (sizeof(arg) / sizeof((arg)[0]))

#define EVENT_MASK \
    ((GdkEventMask)                  \
    (GDK_STRUCTURE_MASK |            \
     GDK_FOCUS_CHANGE_MASK |	     \
     GDK_EXPOSURE_MASK |             \
     GDK_PROPERTY_CHANGE_MASK |	     \
     GDK_ENTER_NOTIFY_MASK |	     \
     GDK_LEAVE_NOTIFY_MASK |	     \
     GDK_KEY_PRESS_MASK |            \
     GDK_KEY_RELEASE_MASK |	     \
     GDK_BUTTON_PRESS_MASK |	     \
     GDK_BUTTON_RELEASE_MASK |	     \
     GDK_POINTER_MOTION_MASK |       \
     GDK_SMOOTH_SCROLL_MASK |        \
     GDK_SCROLL_MASK))

// Data
static GtkWidget*                      g_GtkWidget = NULL;
static GdkWindow*                      g_GdkWindow = NULL;
static CoglContext*                    g_Context = NULL;
static CoglFramebuffer*                g_Framebuffer = NULL;
static guint64                         g_Time = 0;
static bool                            g_MousePressed[5] = { false, false, false, false, false };
static ImVec2                          g_MousePosition = ImVec2(-1, -1);
static float                           g_MouseWheel = 0.0f;
static CoglPipeline*                   g_ColorPipeline = NULL;
static CoglPipeline*                   g_ImagePipeline = NULL;
static int                             g_NumRedraws = 0;
static const struct backend_callbacks* g_Callbacks;
static void                          (*g_Callback)(CoglOnscreen *onscreen, void *data);
static void*                           g_CallbackData;
static guint                           g_RedrawTimeout = 0;

// Some Gdk backend specific stuff.
struct backend_callbacks
{
    CoglWinsysID winsys;
    void   (*init)       (CoglRenderer *renderer, GdkDisplay *display, GdkWindow *window);
    ImVec2 (*get_scale)  (GdkWindow *window);
    void   (*set_window) (CoglOnscreen* onscreen, GdkWindow *window);
    void   (*resize)     (GdkWindow *window, CoglOnscreen* onscreen,
                          int width, int height, int x, int y);
};

static cairo_region_t *get_window_region(GdkWindow *window)
{
    cairo_rectangle_int_t rect;
    memset(&rect, 0, sizeof(rect));
    gdk_window_get_geometry(window,
                            NULL, NULL,
                            &rect.width, &rect.height);
    return cairo_region_create_rectangle (&rect);
}

#if defined(GDK_WINDOWING_WAYLAND) && defined(COGL_HAS_EGL_PLATFORM_WAYLAND_SUPPORT)
static void wayland_init(CoglRenderer *renderer, GdkDisplay *display, GdkWindow *window)
{
    cogl_wayland_renderer_set_foreign_display(renderer,
                                              gdk_wayland_display_get_wl_display(display));
    cogl_wayland_renderer_set_event_dispatch_enabled(renderer, FALSE);
}

static void wayland_set_window(CoglOnscreen* onscreen, GdkWindow *window)
{
    cogl_wayland_onscreen_set_foreign_surface(onscreen,
                                              gdk_wayland_window_get_wl_surface(GDK_WAYLAND_WINDOW(window)));

}

static void wayland_resize(GdkWindow *window,
                           CoglOnscreen* onscreen,
                           int width, int height,
                           int x, int y)
{
    cogl_wayland_onscreen_resize(onscreen, width, height, 0, 0);
}

#endif

#if defined(GDK_WINDOWING_X11) && defined(COGL_HAS_XLIB_SUPPORT)
static void x11_init(CoglRenderer *renderer, GdkDisplay *display, GdkWindow *window)
{
    cogl_xlib_renderer_set_foreign_display(renderer,
                                           gdk_x11_display_get_xdisplay(display));
}

static ImVec2 x11_get_scale(GdkWindow *window)
{
    int scale = gdk_window_get_scale_factor(window);
    return ImVec2(scale, scale);
}

static void x11_window_update_foreign_event_mask(CoglOnscreen *onscreen,
                                                 guint32 event_mask,
                                                 void *user_data)
{
    GdkWindow *window = GDK_WINDOW(user_data);

    /* we assume that a GDK event mask is bitwise compatible with X11
       event masks */
    gdk_window_set_events(window, (GdkEventMask) (event_mask | EVENT_MASK));
}

static GdkFilterReturn
cogl_gdk_filter (GdkXEvent  *xevent,
                 GdkEvent   *event,
                 gpointer    data)
{
  CoglRenderer *renderer = cogl_context_get_renderer(g_Context);
  CoglFilterReturn ret;

  ret = cogl_xlib_renderer_handle_event (renderer, (XEvent *) xevent);
  switch (ret)
    {
    case COGL_FILTER_REMOVE:
      return GDK_FILTER_REMOVE;

    case COGL_FILTER_CONTINUE:
    default:
      return GDK_FILTER_CONTINUE;
    }

  return GDK_FILTER_CONTINUE;
}

static void x11_set_window(CoglOnscreen* onscreen, GdkWindow *window)
{
    cogl_x11_onscreen_set_foreign_window_xid(onscreen,
                                             GDK_WINDOW_XID(window),
                                             x11_window_update_foreign_event_mask,
                                             window);
    gdk_window_add_filter(window, cogl_gdk_filter, onscreen);
}

static void x11_resize(GdkWindow *window,
                       CoglOnscreen* onscreen,
                       int width, int height,
                       int x, int y)
{
    int scale = gdk_window_get_scale_factor(window);
    XConfigureEvent xevent;
    memset(&xevent, 0, sizeof(xevent));
    xevent.type = ConfigureNotify;
    xevent.window = GDK_WINDOW_XID (window);
    xevent.width = width * scale;
    xevent.height = height * scale;

    /* Ensure cogl knows about the new size immediately, as we will
     * draw before we get the ConfigureNotify response. */
    cogl_xlib_renderer_handle_event(cogl_context_get_renderer(g_Context),
                                    (XEvent *)&xevent);
}
#endif

#if defined(GDK_WINDOWING_WIN32) && defined(COGL_HAS_WIN32_SUPPORT)
static void win32_set_window(CoglOnscreen* onscreen, GdkWindow *window)
{
    cogl_win32_onscreen_set_foreign_window(onscreen,
                                           gdk_win32_window_get_handle(window));
}
#endif

static void noop_init(CoglRenderer *renderer, GdkDisplay *display, GdkWindow *window)
{
    /* NOOP */
}

static ImVec2 noop_get_scale(GdkWindow *window)
{
    return ImVec2(1, 1);
}

static void noop_set_window(CoglOnscreen* onscreen, GdkWindow *window)
{
    /* NOOP */
}

static void noop_resize(GdkWindow *window, CoglOnscreen* onscreen,
                        int width, int height, int x, int y)
{
    /* NOOP */
}

static const struct backend_callbacks *
get_backend_callbacks(GdkWindow *window)
{
#if defined(GDK_WINDOWING_WAYLAND) && defined(COGL_HAS_EGL_PLATFORM_WAYLAND_SUPPORT)
    if (GDK_IS_WAYLAND_WINDOW(window)) {
        static const struct backend_callbacks cbs =
            {
                COGL_WINSYS_ID_EGL_WAYLAND,
                wayland_init,
                noop_get_scale,
                wayland_set_window,
                wayland_resize,
            };
        return &cbs;
    }
#endif
#if defined(GDK_WINDOWING_X11) && defined(COGL_HAS_XLIB_SUPPORT)
    if (GDK_IS_X11_WINDOW(window)) {
        static const struct backend_callbacks cbs =
            {
              //COGL_WINSYS_ID_EGL_XLIB,
                COGL_WINSYS_ID_GLX,
                x11_init,
                x11_get_scale,
                x11_set_window,
                x11_resize,
            };
        return &cbs;
    }
#endif
#if defined(GDK_WINDOWING_WIN32) && defined(COGL_HAS_WIN32_SUPPORT)
    if (GDK_IS_WIN32_WINDOW(window)) {
        static const struct backend_callbacks cbs =
            {
                COGL_WINSYS_ID_WGL,
                noop_init,
                noop_get_scale,
                win32_set_window,
                noop_resize,
            };
        return &cbs;
    }
#endif

    static const struct backend_callbacks cbs =
    {
        COGL_WINSYS_ID_STUB,
        noop_init,
        noop_get_scale,
        noop_set_window,
        noop_resize,
    };
    return &cbs;
}

// This is the main rendering function that you have to implement and provide to ImGui (via setting up 'RenderDrawListsFn' in the ImGuiIO structure)
// If text or lines are blurry when integrating ImGui in your engine:
// - in your Render function, try translating your projection matrix by (0.5f,0.5f) or (0.375f,0.375f)
void ImGui_ImplGtk3Cogl_RenderDrawData(ImDrawData* draw_data)
{
    // Avoid rendering when minimized, scale coordinates for retina displays (screen coordinates != framebuffer coordinates)
    ImGuiIO& io = ImGui::GetIO();
    int fb_width = (int)(io.DisplaySize.x * io.DisplayFramebufferScale.x);
    int fb_height = (int)(io.DisplaySize.y * io.DisplayFramebufferScale.y);
    if (fb_width == 0 || fb_height == 0)
        return;
    draw_data->ScaleClipRects(io.DisplayFramebufferScale);

    cogl_framebuffer_orthographic(g_Framebuffer, 0, 0,
                                  io.DisplaySize.x, io.DisplaySize.y,
                                  -1, 1);

    CoglContext *context = cogl_framebuffer_get_context(g_Framebuffer);
    for (int n = 0; n < draw_data->CmdListsCount; n++)
    {
        const ImDrawList* cmd_list = draw_data->CmdLists[n];
        int idx_buffer_offset = 0;

        CoglAttributeBuffer *vertices =
            cogl_attribute_buffer_new(context,
                                      cmd_list->VtxBuffer.Size * sizeof(ImDrawVert),
                                      cmd_list->VtxBuffer.Data);

#define OFFSETOF(TYPE, ELEMENT) ((size_t)&(((TYPE *)0)->ELEMENT))
        CoglAttribute *attrs[3] = {
            cogl_attribute_new(vertices, "cogl_position_in",
                               sizeof(ImDrawVert), OFFSETOF(ImDrawVert, pos),
                               2, COGL_ATTRIBUTE_TYPE_FLOAT),
            cogl_attribute_new(vertices, "cogl_tex_coord0_in",
                               sizeof(ImDrawVert), OFFSETOF(ImDrawVert, uv),
                               2, COGL_ATTRIBUTE_TYPE_FLOAT),
            cogl_attribute_new(vertices, "cogl_color_in",
                               sizeof(ImDrawVert), OFFSETOF(ImDrawVert, col),
                               4, COGL_ATTRIBUTE_TYPE_UNSIGNED_BYTE)
        };
#undef OFFSETOF

        CoglPrimitive *primitive =
            cogl_primitive_new_with_attributes(COGL_VERTICES_MODE_TRIANGLES,
                                               cmd_list->VtxBuffer.Size,
                                               attrs, 3);

        CoglIndices *indices = cogl_indices_new(context,
                                                sizeof(ImDrawIdx) == 2 ?
                                                COGL_INDICES_TYPE_UNSIGNED_SHORT :
                                                COGL_INDICES_TYPE_UNSIGNED_INT,
                                                cmd_list->IdxBuffer.Data,
                                                cmd_list->IdxBuffer.Size);

        for (int cmd_i = 0; cmd_i < cmd_list->CmdBuffer.Size; cmd_i++)
        {
            const ImDrawCmd* pcmd = &cmd_list->CmdBuffer[cmd_i];

            cogl_indices_set_offset(indices, sizeof(ImDrawIdx) * idx_buffer_offset);
            cogl_primitive_set_indices(primitive, indices, pcmd->ElemCount);

            if (pcmd->UserCallback)
            {
                pcmd->UserCallback(cmd_list, pcmd);
            }
            else
            {
                bool has_texture = pcmd->TextureId != NULL;
                CoglPipeline *pipeline =
                    has_texture ?
                    (cogl_is_pipeline(pcmd->TextureId) ?
                     (CoglPipeline *) pcmd->TextureId : g_ImagePipeline) :
                    g_ColorPipeline;

                if (has_texture && pipeline == g_ImagePipeline) {
                    cogl_pipeline_set_layer_texture(g_ImagePipeline, 0,
                                                    COGL_TEXTURE(pcmd->TextureId));
                }

                cogl_framebuffer_push_scissor_clip(g_Framebuffer,
                                                   pcmd->ClipRect.x,
                                                   pcmd->ClipRect.y,
                                                   pcmd->ClipRect.z - pcmd->ClipRect.x,
                                                   pcmd->ClipRect.w - pcmd->ClipRect.y);
                cogl_primitive_draw(primitive, g_Framebuffer, pipeline);
                cogl_framebuffer_pop_clip(g_Framebuffer);
            }
            idx_buffer_offset += pcmd->ElemCount;
        }

        for (int i = 0; i < 3; i++)
            cogl_object_unref(attrs[i]);
        cogl_object_unref(primitive);
        cogl_object_unref(vertices);
        cogl_object_unref(indices);
    }
}

static const char* ImGui_ImplGtk3Cogl_GetClipboardText(void* user_data)
{
    static char *last_clipboard = NULL;

    g_clear_pointer(&last_clipboard, g_free);
    last_clipboard = gtk_clipboard_wait_for_text(GTK_CLIPBOARD(user_data));
    return last_clipboard;
}

static void ImGui_ImplGtk3Cogl_SetClipboardText(void* user_data, const char* text)
{
    gtk_clipboard_set_text(GTK_CLIPBOARD(user_data), text, -1);
}

bool ImGui_ImplGtk3Cogl_CreateFontsTexture()
{
    // Build texture atlas
    ImGuiIO& io = ImGui::GetIO();
    unsigned char* pixels;
    int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);   // Load as RGBA 32-bits (75% of the memory is wasted, but default font is so small) because it is more likely to be compatible with user's existing shaders. If your ImTextureId represent a higher-level concept than just a GL texture id, consider calling GetTexDataAsAlpha8() instead to save on GPU memory.

    g_clear_pointer(&io.Fonts->TexID, cogl_object_unref);
    io.Fonts->TexID =
        cogl_texture_2d_new_from_data(g_Context,
                                      width, height,
                                      COGL_PIXEL_FORMAT_RGBA_8888,
                                      width * 4, pixels, NULL);

    return true;
}

bool ImGui_ImplGtk3Cogl_CreateDeviceObjects()
{
    g_ColorPipeline = cogl_pipeline_new(g_Context);
    g_ImagePipeline = cogl_pipeline_new(g_Context);

    CoglError *error = NULL;
    if (!cogl_pipeline_set_blend(g_ColorPipeline,
                                 "RGB = ADD(SRC_COLOR*(SRC_COLOR[A]), DST_COLOR*(1-SRC_COLOR[A]))"
                                 "A = ADD(SRC_COLOR[A], DST_COLOR*(1-SRC_COLOR[A]))",
                                 &error))
    {
        g_warning("Blending: %s", error->message);
        g_error_free(error);
        return false;
    }
    cogl_pipeline_set_cull_face_mode(g_ColorPipeline, COGL_PIPELINE_CULL_FACE_MODE_NONE);

    CoglDepthState depth_state;

    cogl_depth_state_init(&depth_state);
    cogl_depth_state_set_test_enabled(&depth_state, FALSE);
    if (!cogl_pipeline_set_depth_state(g_ColorPipeline, &depth_state, &error))
    {
        g_warning("Depth: %s", error->message);
        g_error_free(error);
        return false;
    }

    if (!cogl_pipeline_set_blend(g_ImagePipeline,
                                 "RGB = ADD(SRC_COLOR*(SRC_COLOR[A]), DST_COLOR*(1-SRC_COLOR[A]))"
                                 "A = ADD(SRC_COLOR[A], DST_COLOR*(1-SRC_COLOR[A]))",
                                 &error))
    {
        g_warning("Blending: %s", error->message);
        g_error_free(error);
        return false;
    }
    cogl_pipeline_set_cull_face_mode(g_ImagePipeline, COGL_PIPELINE_CULL_FACE_MODE_NONE);

    if (!cogl_pipeline_set_depth_state(g_ImagePipeline, &depth_state, &error))
    {
        g_warning("Depth: %s", error->message);
        g_error_free(error);
        return false;
    }

    ImGui_ImplGtk3Cogl_CreateFontsTexture();

    /* Disable depth buffer since we're not using it. */
    //cogl_framebuffer_set_depth_write_enabled(g_Framebuffer, FALSE);

    return true;
}

void    ImGui_ImplGtk3Cogl_InvalidateDeviceObjects()
{
    g_clear_pointer(&g_ColorPipeline, cogl_object_unref);
    g_clear_pointer(&g_ImagePipeline, cogl_object_unref);
    g_clear_pointer(&ImGui::GetIO().Fonts->TexID, cogl_object_unref);
}

void   ImGui_ImplGtk3Cogl_HandleEvent(GdkEvent *event)
{
    ImGuiIO& io = ImGui::GetIO();

    GdkEventType type = gdk_event_get_event_type(event);
    switch (type)
    {
    case GDK_MOTION_NOTIFY:
    {
        gdouble x = 0.0f, y = 0.0f;
        if (gdk_event_get_coords(event, &x, &y))
            g_MousePosition = ImVec2(x, y);
        break;
    }
    case GDK_BUTTON_PRESS:
    case GDK_BUTTON_RELEASE:
    {
        guint button = 0;
        if (gdk_event_get_button(event, &button) && button > 0 && button <= 5)
        {
            if (type == GDK_BUTTON_PRESS)
                g_MousePressed[button - 1] = true;
        }
        break;
    }
    case GDK_SCROLL:
    {
        gdouble x, y;
        if (gdk_event_get_scroll_deltas(event, &x, &y))
            g_MouseWheel = -y;
        break;
    }
    case GDK_KEY_PRESS:
    case GDK_KEY_RELEASE:
    {
        GdkEventKey *e = (GdkEventKey *) event;

        static const struct
        {
            enum ImGuiKey_ imgui;
            guint gdk;
        } gdk_key_to_imgui_key[] =
              {
                  { ImGuiKey_Tab, GDK_KEY_Tab },
                  { ImGuiKey_Tab, GDK_KEY_ISO_Left_Tab },
                  { ImGuiKey_LeftArrow, GDK_KEY_Left },
                  { ImGuiKey_RightArrow, GDK_KEY_Right },
                  { ImGuiKey_UpArrow, GDK_KEY_Up },
                  { ImGuiKey_DownArrow, GDK_KEY_Down },
                  { ImGuiKey_PageUp, GDK_KEY_Page_Up },
                  { ImGuiKey_PageDown, GDK_KEY_Page_Down },
                  { ImGuiKey_Home, GDK_KEY_Home },
                  { ImGuiKey_End, GDK_KEY_End },
                  { ImGuiKey_Delete, GDK_KEY_Delete },
                  { ImGuiKey_Backspace, GDK_KEY_BackSpace },
                  { ImGuiKey_Space, GDK_KEY_space },
                  { ImGuiKey_Enter, GDK_KEY_Return },
                  { ImGuiKey_Escape, GDK_KEY_Escape },
                  { ImGuiKey_A, GDK_KEY_a },
                  { ImGuiKey_C, GDK_KEY_c },
                  { ImGuiKey_V, GDK_KEY_v },
                  { ImGuiKey_X, GDK_KEY_x },
                  { ImGuiKey_Y, GDK_KEY_y },
                  { ImGuiKey_Z, GDK_KEY_z },
              };
        for (unsigned i = 0; i < ARRAY_SIZE(gdk_key_to_imgui_key); i++)
        {
            if (e->keyval == gdk_key_to_imgui_key[i].gdk)
                io.KeysDown[gdk_key_to_imgui_key[i].imgui] = type == GDK_KEY_PRESS;
        }
        gunichar c = gdk_keyval_to_unicode(e->keyval);
        if (g_unichar_isprint(c) && ImGuiKey_COUNT + c < ARRAY_SIZE(io.KeysDown))
            io.KeysDown[ImGuiKey_COUNT + c] = type == GDK_KEY_PRESS;

        if (type == GDK_KEY_PRESS && e->string)
            io.AddInputCharactersUTF8(e->string);

        struct {
            bool *var;
            GdkModifierType modifier;
            guint keyvals[3];
        } mods[] = {
            { &io.KeyCtrl, GDK_CONTROL_MASK,
              { GDK_KEY_Control_L, GDK_KEY_Control_R, 0 }, },
            { &io.KeyShift, GDK_SHIFT_MASK,
              { GDK_KEY_Shift_L, GDK_KEY_Shift_R, 0 }, },
            { &io.KeyAlt, GDK_MOD1_MASK,
              { GDK_KEY_Alt_L, GDK_KEY_Alt_R, 0 }, },
            { &io.KeySuper, GDK_SUPER_MASK,
              { GDK_KEY_Super_L, GDK_KEY_Super_R, 0 }, }
        };
        for (unsigned i = 0; i < ARRAY_SIZE(mods); i++)
        {
            *mods[i].var = (mods[i].modifier & e->state);

            bool match = false;
            for (int j = 0; mods[i].keyvals[j] != 0; j++)
                if (e->keyval == mods[i].keyvals[j])
                    match = true;

            if (match)
                *mods[i].var = type == GDK_KEY_PRESS;
        }
        break;
    }
    default:
        break;
    }

    // We trigger 2 subsequent redraws for each event because of the
    // way some ImGui widgets work. For example a Popup menu will only
    // appear a frame after a click happened.
    g_NumRedraws = 2;

    GdkFrameClock *clock = gdk_window_get_frame_clock(g_GdkWindow);
    gdk_frame_clock_request_phase(clock, GDK_FRAME_CLOCK_PHASE_PAINT);
}

static gboolean handle_gdk_event(GtkWidget *widget, GdkEvent *event, void *data)
{
    ImGui_ImplGtk3Cogl_HandleEvent(event);
    return TRUE;
}

static void handle_allocate(GtkWidget    *widget,
                            GdkRectangle *allocation,
                            gpointer      user_data)
{
    gdk_window_resize(g_GdkWindow,
                      allocation->width, allocation->height);
    g_Callbacks->resize(g_GdkWindow, COGL_ONSCREEN(g_Framebuffer),
                        allocation->width, allocation->height,
                        allocation->x, allocation->y);

}

static void handle_repaint(GdkFrameClock *clock, void *data)
{
    g_Callback(COGL_ONSCREEN(g_Framebuffer), g_CallbackData);
}

CoglOnscreen* ImGui_ImplGtk3Cogl_Init(GtkWidget* widget,
                                      void (*callback)(CoglOnscreen *onscreen, void *data),
                                      void *data)
{
    g_clear_pointer(&g_GtkWidget, g_object_unref);
    g_clear_pointer(&g_GdkWindow, g_object_unref);
    g_clear_pointer(&g_Framebuffer, cogl_object_unref);
    g_clear_pointer(&g_Context, cogl_object_unref);

    g_GtkWidget = GTK_WIDGET(g_object_ref(widget));
    gtk_widget_realize(widget);
    GdkWindow *parent_window = gtk_widget_get_window(widget);

    GtkAllocation allocation;
    gtk_widget_get_allocation(widget, &allocation);

    g_Callbacks = get_backend_callbacks(parent_window);

    GdkWindowAttr attributes;
    memset(&attributes, 0, sizeof(attributes));
    attributes.x = 0;
    attributes.y = 0;
    attributes.width = allocation.width;
    attributes.height = allocation.height;
    attributes.wclass = GDK_INPUT_OUTPUT;
    attributes.window_type = g_Callbacks->winsys == COGL_WINSYS_ID_EGL_WAYLAND ?
        GDK_WINDOW_SUBSURFACE : GDK_WINDOW_CHILD;

    GdkDisplay *display = gdk_window_get_display(parent_window);
    attributes.visual = gtk_widget_get_visual(widget);

    g_GdkWindow = gdk_window_new(parent_window, &attributes,
                                 GDK_WA_X | GDK_WA_Y | GDK_WA_VISUAL);
    gdk_window_set_transient_for(g_GdkWindow, parent_window);
    gdk_window_set_pass_through(g_GdkWindow, TRUE);

    cairo_rectangle_int_t empty_rect;
    memset(&empty_rect, 0, sizeof(empty_rect));
    cairo_region_t *input_region = cairo_region_create_rectangle(&empty_rect);
    gdk_window_input_shape_combine_region(g_GdkWindow, input_region, 0, 0);
    cairo_region_destroy(input_region);

    cairo_region_t *region = get_window_region(g_GdkWindow);
    gdk_window_set_opaque_region(g_GdkWindow, region);
    cairo_region_destroy(region);

    CoglRenderer *renderer = cogl_renderer_new();
    cogl_renderer_set_winsys_id(renderer, g_Callbacks->winsys);
    g_Callbacks->init(renderer, display, g_GdkWindow);

    gdk_window_ensure_native(g_GdkWindow);

    g_Context = cogl_context_new(cogl_display_new(renderer, NULL), NULL);
    CoglOnscreen *onscreen = cogl_onscreen_new(g_Context, 1, 1);
    cogl_object_unref(renderer);

    g_Callbacks->resize(g_GdkWindow, onscreen,
                        allocation.width, allocation.height,
                        allocation.x, allocation.y);

    gtk_widget_add_events(widget, EVENT_MASK);
    g_signal_connect(widget, "event", G_CALLBACK(handle_gdk_event), NULL);
    g_signal_connect(widget, "size-allocate", G_CALLBACK(handle_allocate), NULL);

    g_Callbacks->set_window(onscreen, g_GdkWindow);

    if (!cogl_framebuffer_allocate(COGL_FRAMEBUFFER(onscreen), NULL))
        g_warning("Unable to allocate framebuffer");

    g_Framebuffer = COGL_FRAMEBUFFER(onscreen);

    ImGuiIO& io = ImGui::GetIO();
    for (int i = 0; i < ImGuiKey_COUNT; i++)
    {
        io.KeyMap[i] = i;
    }

    io.SetClipboardTextFn = ImGui_ImplGtk3Cogl_SetClipboardText;
    io.GetClipboardTextFn = ImGui_ImplGtk3Cogl_GetClipboardText;
    io.ClipboardUserData = gtk_widget_get_clipboard(g_GtkWidget,
                                                    GDK_SELECTION_CLIPBOARD);

    g_Callback = callback;
    g_CallbackData = data;
    GdkFrameClock *clock = gdk_window_get_frame_clock(g_GdkWindow);
    g_signal_connect(clock, "paint", G_CALLBACK(handle_repaint), NULL);

    gdk_frame_clock_request_phase(clock, GDK_FRAME_CLOCK_PHASE_PAINT);

    gdk_window_show(g_GdkWindow);

    return COGL_ONSCREEN(g_Framebuffer);
}

CoglContext*  ImGui_ImplGtk3Cogl_GetContext()
{
    return g_Context;
}

void ImGui_ImplGtk3Cogl_Shutdown()
{
    ImGui_ImplGtk3Cogl_InvalidateDeviceObjects();
    g_clear_pointer(&g_Framebuffer, cogl_object_unref);
    g_clear_pointer(&g_Context, cogl_object_unref);
}

static gboolean timeout_callback(gpointer data)
{
    GdkFrameClock *clock = gdk_window_get_frame_clock(g_GdkWindow);
    gdk_frame_clock_request_phase(clock, GDK_FRAME_CLOCK_PHASE_PAINT);
    g_RedrawTimeout = 0;
    return FALSE;
}

static void kick_timeout_redraw(float timeout)
{
    if (g_RedrawTimeout)
        return;
    g_RedrawTimeout = g_timeout_add(timeout * 1000, timeout_callback, NULL);
}

void ImGui_ImplGtk3Cogl_NewFrame()
{
    if (!g_ColorPipeline)
        ImGui_ImplGtk3Cogl_CreateDeviceObjects();

    bool next_redraw = false;
    if (g_NumRedraws > 0)
    {
        GdkFrameClock *clock = gdk_window_get_frame_clock(g_GdkWindow);
        gdk_frame_clock_request_phase(clock, GDK_FRAME_CLOCK_PHASE_PAINT);
        g_NumRedraws--;
        next_redraw = true;
    }

    ImGuiIO& io = ImGui::GetIO();

    // Setup display size (every frame to accommodate for window resizing)
    int w, h;
    gdk_window_get_geometry(g_GdkWindow, NULL, NULL, &w, &h);
    io.DisplaySize = ImVec2((float)w, (float)h);
    io.DisplayFramebufferScale = g_Callbacks->get_scale(g_GdkWindow);

    // Setup time step
    guint64 current_time =  g_get_monotonic_time();
    io.DeltaTime = g_Time > 0 ? ((float)(current_time - g_Time) / 1000000) : (float)(1.0f/60.0f);
    g_Time = current_time;

    // Setup inputs
    if (gdk_window_get_state(g_GdkWindow) & GDK_WINDOW_STATE_FOCUSED)
    {
        io.MousePos = g_MousePosition;   // Mouse position in screen coordinates (set to -1,-1 if no mouse / on another screen, etc.)
    }
    else
    {
        io.MousePos = ImVec2(-1,-1);
    }

    GdkDevice *pointer = gdk_device_manager_get_client_pointer(gdk_display_get_device_manager(gdk_display_get_default()));
    GdkModifierType modifiers;
    gdk_device_get_state(pointer, g_GdkWindow, NULL, &modifiers);

    for (int i = 0; i < 3; i++)
    {
        io.MouseDown[i] = g_MousePressed[i] || (modifiers & (GDK_BUTTON1_MASK << i)) != 0;
        g_MousePressed[i] = false;
    }

    io.MouseWheel = g_MouseWheel;
    g_MouseWheel = 0.0f;

    // Hide OS mouse cursor if ImGui is drawing it
    GdkDisplay *display = gdk_window_get_display(g_GdkWindow);
    GdkCursor *cursor =
      gdk_cursor_new_from_name(display, io.MouseDrawCursor ? "none" : "default");
    gdk_window_set_cursor(g_GdkWindow, cursor);
    g_object_unref(cursor);

    // Start the frame
    ImGui::NewFrame();

    if (!next_redraw && io.WantTextInput)
        kick_timeout_redraw(0.2);
}

void ImGui_ImplGtk3Cogl_ScheduleFrame()
{
    GdkFrameClock *clock = gdk_window_get_frame_clock(g_GdkWindow);
    gdk_frame_clock_request_phase(clock, GDK_FRAME_CLOCK_PHASE_PAINT);
}
