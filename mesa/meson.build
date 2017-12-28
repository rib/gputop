mesa_src = [
  'src/intel/common/gen_device_info.c',
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
mesa_flags = [ '-DHAVE_PTHREAD',
               '-Wno-unused-parameter',
               '-Wno-initializer-overrides' ]
if c.has_argument('-Wno-override-init')
  mesa_flags += [ '-Wno-override-init' ]
endif

mesa = static_library('mesa',
                      mesa_src,
                      c_args : mesa_flags,
                      include_directories : mesa_inc)

mesa_dep = declare_dependency(link_with : mesa,
                              include_directories : mesa_inc)
