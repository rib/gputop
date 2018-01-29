gputop_wrapper_src = [
  'gputop-wrapper-main.c',
  'gputop-uv-network.c',
]

gputop_wrapper_inc = include_directories('.')

gputop_wrapper_deps = [
  dependency('threads'),
  libuv_dep,
  wslay_dep,
  gputop_client_dep,
]

executable('gputop-wrapper', gputop_wrapper_src,
	   include_directories : gputop_wrapper_inc,
	   dependencies : gputop_wrapper_deps,
	   install : true)
