libgputop_src = [
  'gputop-hash-table.c',
  'gputop-perf.c',
  'gputop-sysutil.c',
  'gputop-log.c',
  'gputop-ncurses.c',
  'gputop-cpu.c',
  'gputop-debugfs.c',
  'gputop-ioctl.c',
  'gputop-server.c',
]
libgputop_inc = include_directories('.')

gputop_deps = [
  dependency('threads'),
  cc.find_library('dl', required: false),
  libuv_dep,
  wslay_dep,
  h2o_dep,
  protobuf_c_dep,
]
gputop_deps += [ gputop_client_dep, mesa_dep, ]

gputop_flags = [ '-DHAVE_PTHREAD', '-D_GNU_SOURCE' ]


config_h = custom_target('config.h',
                         input : [],
                         output : ['config.h'],
                         command : [find_program('touch'), '@OUTPUT@'])

libgputop = shared_library('gputop',
                           [libgputop_src, config_h],
                           c_args : gputop_flags,
                           dependencies : gputop_deps,
	                   include_directories : libgputop_inc,
                           install : true)

gputop_system_src = [
  'gputop-system.c',
]

executable('gputop-system',
           [gputop_system_src, config_h],
	   include_directories : libgputop_inc,
           link_with : libgputop,
	   install : true)

gputop_src = [
  'gputop-main.c',
]

executable('gputop',
           [gputop_src, config_h],
           c_args : gputop_flags,
           include_directories : libgputop_inc,
           link_with : libgputop,
           dependencies: mesa_dep,
           install : true)
