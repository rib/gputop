gputop_client_src = [
  'gputop-client-context.c',
  'gputop-oa-counters.c',
  'gputop-oa-metrics.c',
]

gputop_client_generated_src = []
gputop_client_generated_src += custom_target(
  'proto-files',
  input : [join_paths(meson.current_source_dir(), '../data/gputop.proto')],
  output : ['gputop.pb-c.c', 'gputop.pb-c.h'],
  command : [protoc_c, '--c_out=@OUTDIR@',
             '-I' + join_paths(meson.current_source_dir(), '../data'),
             join_paths(meson.current_source_dir(), '../data/gputop.proto')])

hardware = [
  'hsw',
  'bdw', 'chv',
  'sklgt2', 'sklgt3', 'sklgt4',
  'kblgt2', 'kblgt3',
  'cflgt2', 'cflgt3',
  'bxt', 'glk',
  'cnl',
  'icl', 'lkf',
  'tgl',
]

gen_xml_files = []
foreach hw : hardware
  gen_xml_files += '../data/oa-@0@.xml'.format(hw)
endforeach

gputop_client_generated_src += custom_target(
  'gputop-gens-metrics',
  input : gen_xml_files,
  output : [ 'gputop-gens-metrics.c', 'gputop-gens-metrics.h' ],
  command : [
    find_program('../scripts/gputop-oa-codegen.py'),
    '--code', '@OUTPUT0@',
    '--header', '@OUTPUT1@',
    '@INPUT@',
  ])

gputop_client_generated_src += custom_target(
  'tracepoint-parser',
  output: 'tracepoint_format.leg.h',
  input: 'tracepoint_format.leg',
  command: [leg, '-P', '-o', '@OUTPUT@', '@INPUT@'])

gputop_client_inc = include_directories('.')

gputop_client = static_library('gputop_client',
                               gputop_client_src + gputop_client_generated_src,
                               dependencies : [mesa_dep, protobuf_c_dep],
	                       include_directories : gputop_client_inc)

gputop_client_dep = declare_dependency(link_with : gputop_client,
                                       dependencies : [mesa_dep, protobuf_c_dep],
                                       sources : gputop_client_generated_src,
				       include_directories : gputop_client_inc)
