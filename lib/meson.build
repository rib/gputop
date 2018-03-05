gputop_client_src = [
  'gputop-client-context.c',
  'gputop-oa-counters.c',
]

gputop_client_src += custom_target(
  'proto-files',
  input : [join_paths(meson.current_source_dir(), '../data/gputop.proto')],
  output : ['gputop.pb-c.c', 'gputop.pb-c.h'],
  command : [protoc_c, '--c_out=@OUTDIR@',
             '-I' + join_paths(meson.current_source_dir(), '../data'),
             join_paths(meson.current_source_dir(), '../data/gputop.proto')])

foreach hw : ['hsw', 'bdw', 'chv', 'sklgt2', 'sklgt3', 'sklgt4', 'bxt',
              'kblgt2', 'kblgt3', 'glk', 'cflgt2', 'cflgt3', 'cnl', 'icl']
  _xml = '../data/oa-@0@.xml'.format(hw)
  _name = 'oa-@0@'.format(hw)
  gputop_client_src += custom_target(
    _name,
    input : ['../scripts/gputop-oa-codegen.py', _xml],
    output : ['@0@.c'.format(_name), '@0@.h'.format(_name)],
    command : [
      prog_python2, '@INPUT0@',
      '--chipset', hw,
      '--code', '@OUTPUT0@', '--header', '@OUTPUT1@', '@INPUT1@',
    ])
endforeach

gputop_client_src += custom_target(
  'tracepoint-parser',
  output: 'tracepoint_format.leg.h',
  input: 'tracepoint_format.leg',
  command: [leg, '-P', '-o', '@OUTPUT@', '@INPUT@'])

gputop_client_inc = include_directories('.')

gputop_client = static_library('gputop_client',
                               gputop_client_src,
                               dependencies : [mesa_dep, protobuf_c_dep],
	                       include_directories : gputop_client_inc)

gputop_client_dep = declare_dependency(link_with : gputop_client,
                                       dependencies : [mesa_dep, protobuf_c_dep],
				       include_directories : gputop_client_inc)
