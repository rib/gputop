gputop_client_src = [
  'gputop-client-context.c',
  'gputop-oa-counters.c',
]

gputop_client_src += custom_target(
  'proto-files',
  input : [join_paths(meson.current_source_dir(), '../gputop-data/gputop.proto')],
  output : ['gputop.pb-c.c', 'gputop.pb-c.h'],
  command : [protoc_c, '--c_out=@OUTDIR@',
             '-I' + join_paths(meson.current_source_dir(), '../gputop-data'),
             join_paths(meson.current_source_dir(), '../gputop-data/gputop.proto')])

oa_generator = generator(
  prog_python2,
  arguments : ['@CURRENT_SOURCE_DIR@/../scripts/gputop-oa-codegen.py', '@INPUT@',
               '--chipset', '@EXTRA_ARGS@', '--code', '@OUTPUT0@',
               '--header', '@OUTPUT1@'],
  output : ['@BASENAME@.c', '@BASENAME@.h'],
)

oa_sources = []
foreach hw : ['hsw', 'bdw', 'chv', 'sklgt2', 'sklgt3', 'sklgt4', 'bxt',
              'kblgt2', 'kblgt3', 'glk', 'cflgt2', 'cflgt3', 'cnl']
  _xml = '../gputop-data/oa-@0@.xml'.format(hw)
  oa_sources += oa_generator.process(_xml, extra_args : hw)
endforeach

leg_sp = subproject('peg')
leg = leg_sp.get_variable('leg_exe')
gputop_client_src += custom_target(
  'tracepoint-parser',
  output: 'tracepoint_format.leg.h',
  input: 'tracepoint_format.leg',
  command: [leg, '-P', '-o', '@OUTPUT@', '@INPUT@'])

gputop_client_oa = static_library('gputop_client_oa', oa_sources,
                                  c_args : ['-Wno-unused-parameter', '-Wno-unused-function'],
                                  dependencies : mesa_dep)

gputop_client_inc = [include_directories('.'), gputop_client_oa.private_dir_include()]

gputop_client = static_library('gputop_client',
                               gputop_client_src,
                               dependencies : [mesa_dep, protobuf_c_dep],
                               link_with : gputop_client_oa,
	                       include_directories : gputop_client_inc)

gputop_client_dep = declare_dependency(link_with : gputop_client,
                                       dependencies : [mesa_dep, protobuf_c_dep],
				       include_directories : gputop_client_inc)
