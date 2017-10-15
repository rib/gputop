// ImGui Gtk3 binding with Cogl
// In this binding, ImTextureID is used to store an OpenGL 'GLuint' texture identifier. Read the FAQ about ImTextureID in imgui.cpp.

// You can copy and use unmodified imgui_impl_* files in your project. See main.cpp for an example of using this.
// If you use this binding you'll need to call 4 functions: ImGui_ImplXXXX_Init(), ImGui_ImplXXXX_NewFrame(), ImGui::Render() and ImGui_ImplXXXX_Shutdown().
// If you are new to ImGui, see examples/README.txt and documentation at the top of imgui.cpp.
// https://github.com/ocornut/imgui

#include <gtk/gtk.h>

#define COGL_ENABLE_EXPERIMENTAL_API 1

#include <cogl/cogl2-experimental.h>

IMGUI_API CoglOnscreen* ImGui_ImplGtk3Cogl_Init(GtkWidget* widget,
                                                void (*callback)(CoglOnscreen *, void *),
                                                void *data);
IMGUI_API void          ImGui_ImplGtk3Cogl_HandleEvent(GdkEvent *event);

IMGUI_API CoglContext*  ImGui_ImplGtk3Cogl_GetContext();

IMGUI_API void          ImGui_ImplGtk3Cogl_Shutdown();
IMGUI_API void          ImGui_ImplGtk3Cogl_NewFrame();

// Use if you want to reset your rendering device without losing ImGui state.
IMGUI_API void          ImGui_ImplGtk3Cogl_InvalidateDeviceObjects();
IMGUI_API bool          ImGui_ImplGtk3Cogl_CreateDeviceObjects();

IMGUI_API void          ImGui_ImplGtk3Cogl_ScheduleFrame();
