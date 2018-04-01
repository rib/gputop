imgui_src = [
  'imgui/imgui.cpp',
  'imgui/imgui_demo.cpp',
  'imgui/imgui_draw.cpp',
]
imgui_inc = include_directories('imgui')
imgui_deps = []

ui_src = [
  'gputop-ui-main.cpp',
  'gputop-ui-multilines.cpp',
  'gputop-ui-piechart.cpp',
  'gputop-ui-plots.cpp',
  'gputop-ui-timeline.cpp',
  'gputop-ui-topology.cpp',
  'gputop-ui-utils.cpp',
]
ui_inc = include_directories('.', 'imgui')
ui_flags = [ '-Wno-invalid-offsetof' ]

imgui = static_library('imgui', imgui_src,
                       include_directories : imgui_inc,
                       cpp_args : ['-Wno-unused-parameter',
                                   '-Wno-unused-function',
                                   '-Wno-sign-compare'],
                       dependencies : imgui_deps)

if build_webui
  ui_src += [
    'imgui/imgui_impl_sdl_gles2.cpp',
    'gputop-emscripten-network.c',
  ]

  executable('gputop-ui.js', ui_src,
	     include_directories : ui_inc,
             cpp_args : ui_flags,
             link_with : imgui,
             dependencies : gputop_client_dep,
	     install : true)
elif build_native_ui
  glfw_ui_src = ui_src + [ 'imgui/imgui_impl_glfw_gl3.cpp',
                           'gputop-uv-network.c', ]
  glfw_ui_flags = ui_flags + [ '-DGPUTOP_UI_GLFW',
                               '-DGLFW_EXPOSE_NATIVE_EGL', ]
  glfw_ui_deps = [ dependency('glfw3'),
                   dependency('epoxy'),
                   libuv_dep, wslay_dep,
                   dependency('threads'), ]

  x11_dep = dependency('x11', required : false)
  if x11_dep.found()
    glfw_ui_flags += [ '-DGLFW_EXPOSE_NATIVE_X11', ]
    glfw_ui_deps += x11_dep
  endif
  wayland_dep = dependency('wayland-client', required : false)
  if wayland_dep.found()
    glfw_ui_flags += [ '-DGLFW_EXPOSE_NATIVE_WAYLAND', ]
    glfw_ui_deps += wayland_dep
  endif

  executable('gputop-ui', glfw_ui_src,
	     include_directories : ui_inc,
             cpp_args : glfw_ui_flags,
             link_with : imgui,
             dependencies : [gputop_client_dep, glfw_ui_deps],
	     install : true)

  want_ui_gtk = get_option('native_ui_gtk') == 'true'
  gtk_dep = dependency('gtk+-3.0', version : '>= 3.18.0', required : want_ui_gtk)
  cogl_dep = dependency('cogl-2.0-experimental', required : want_ui_gtk)
  libsoup_dep = dependency('libsoup-2.4', version : '>= 2.60.3', required : want_ui_gtk)

  if gtk_dep.found() and cogl_dep.found() and libsoup_dep.found()
    gtk_ui_src = ui_src + [ 'imgui/imgui_impl_gtk3_cogl.cpp',
                            'gputop-soup-network.c', ]
    gtk_ui_flags = ui_flags + [ '-DGPUTOP_UI_GTK', ]
    gtk_ui_deps = [ dependency('threads'),
                    gtk_dep, cogl_dep,
                    libsoup_dep, ]

    executable('gputop-ui-gtk', gtk_ui_src,
	       include_directories : ui_inc,
               cpp_args : gtk_ui_flags,
               link_with : imgui,
               dependencies : [gputop_client_dep, gtk_ui_deps],
	       install : true)
  endif
endif
