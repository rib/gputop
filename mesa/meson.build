mesa_src = [
  'src/intel/dev/gen_device_info.c',
  'src/util/bitscan.c',
  'src/util/hash_table.c',
  'src/util/ralloc.c',
]
mesa_inc = include_directories('include',
                               'include/drm-uapi',
                               'src',
                               'src/mesa',
                               'src/intel',
                               'src/util')

mesa_config = [ '-DHAVE_PTHREAD' ]
if cc.has_function('timespec_get')
  mesa_config += [ '-DHAVE_TIMESPEC_GET' ]
endif

mesa_flags = mesa_config + [ '-Wno-unused-parameter',
                             '-Wno-initializer-overrides' ]
if c.has_argument('-Wno-override-init')
  mesa_flags += [ '-Wno-override-init' ]
endif

mesa = static_library('mesa',
                      mesa_src,
                      c_args : mesa_flags,
                      include_directories : mesa_inc)

mesa_dep = declare_dependency(compile_args: mesa_config,
                              link_with : mesa,
                              include_directories : mesa_inc)
