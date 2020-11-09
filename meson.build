project('GPUTop', 'c', 'cpp',
        version : '0.0.2',
        default_options : [
          'warning_level=2',
          'c_std=gnu99',
          'default_library=static',
        ],
        license : 'MIT',
        meson_version : '>= 0.44.0')

add_project_arguments(['-Wno-unused-parameter',
                       '-Wno-sign-compare'],
                      language: ['c', 'cpp'])

c = meson.get_compiler('c')
cc = meson.get_compiler('cpp')

protobuf_c_dep = c.find_library('protobuf-c', required : false)
protoc_c = find_program('protoc', required: false)

prog_python2 = find_program('python2')

build_native_ui = get_option('native_ui')
build_webui = get_option('webui')
if build_webui and build_native_ui
  error('Cannot build webui and native tools in the same build')
endif

if meson.is_cross_build()
  protobuf_c_dep = subproject('protobuf-c').get_variable('protobuf_c_dep')
  if not protoc.found()
    protoc_c = subproject('protobuf-c').get_variable('protoc_c_native')
  endif
  leg = subproject('peg').get_variable('leg_native')
else
  if not protobuf_c_dep.found() or not protoc_c.found()
    protobuf_c_dep = subproject('protobuf-c').get_variable('protobuf_c_dep')
    protoc_c = subproject('protobuf-c').get_variable('protoc_c')
  endif
  leg = subproject('peg').get_variable('leg')
endif


subdir('mesa')
subdir('lib')
if not build_webui
  libuv_dep = subproject('libuv').get_variable('libuv_dep')
  wslay_dep = subproject('wslay').get_variable('wslay_dep')
  h2o_dep = subproject('h2o').get_variable('h2o_dep')

  subdir('server')
  subdir('wrapper')
  subdir('utils')
endif
subdir('ui')
