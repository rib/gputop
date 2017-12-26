include $(top_srcdir)/Makefile.generated

templated_files = \
	binding.gyp \
	package.json

BUILT_SOURCES = \
	$(oa_generated_sources) \
	$(templated_files)

noinst_DATA = \
	$(templated_files)

noinst_PROGRAMS = tracepoint_parser

EXTRA_DIST = \
	$(templated_files:%=%.in)

package.json: $(top_srcdir)/scripts/gen-template.py Makefile.am $(top_srcdir)/Makefile.generated package.json.in
	$(PYTHON2) $(PYTHON_FLAGS) $(top_srcdir)/scripts/gen-template.py --template=$(@).in --out=$(@) $(oa_generated_sources)

binding.gyp: $(top_srcdir)/scripts/gen-template.py Makefile.am $(top_srcdir)/Makefile.generated binding.gyp.in
	$(PYTHON2) $(PYTHON_FLAGS) $(top_srcdir)/scripts/gen-template.py --template=$(@).in --out=$(@) $(oa_generated_sources)

noinst_LTLIBRARIES = libgputop-client.la

libgputop_client_la_CPPFLAGS = \
	-DGPUTOP_CLIENT \
	\
	-I$(top_srcdir)/mesa/include \
	-I$(top_srcdir)/mesa/include/drm-uapi \
	-I$(top_srcdir)/mesa/src \
	-I$(top_srcdir)/mesa/src/intel \
	-I$(top_srcdir)/mesa/src/util
libgputop_client_la_SOURCES = \
	gputop-client-context.c \
	gputop-client-context.h \
	gputop-oa-counters.c \
	gputop-oa-counters.h \
	gputop-oa-metrics.h \
	gputop-util.h \
	gputop.pb-c.h \
	gputop.pb-c.c \
	$(oa_generated_sources)
libgputop_client_la_LIBADD = \
	$(top_builddir)/protobuf-c/libprotobuf-c.la \
	$(top_builddir)/mesa/libmesa.la

%.pb-c.c %.pb-c.h: $(data_dir)/%.proto Makefile.am
	$(top_builddir)/protoc-c/protoc-c --c_out=$(builddir) -I$(data_dir) $(data_dir)/$(*F).proto

tracepoint_format.leg.h: $(srcdir)/tracepoint_format.leg
	$(LEG) -o $@ $<

BUILT_SOURCES += \
	gputop.pb-c.h \
	gputop.pb-c.c \
	tracepoint_format.leg.h

if ENABLE_WEBUI
remotedir = $(datadir)/remote
nobase_dist_remote_DATA = \
    gputop-web.js \
    gputop-web.js.map

BUILT_SOURCES += \
	gputop-web.js

# Automake seems awkward to use for running the emscripten toolchain so
# we have a plain GNU Makefile that we proxy too instead...
EMCC_PROXY_MAKEFLAGS=-f $(srcdir)/Makefile.emscripten $(AM_MAKEFLAGS) top_srcdir=$(top_srcdir)

gputop-web.js: $(OA_GEN_SOURCES)
	$(MAKE) $(EMCC_PROXY_MAKEFLAGS) gputop-web.js

all-local:
	$(MAKE) $(EMCC_PROXY_MAKEFLAGS) all
#check-local:
#	$(MAKE) $(EMCC_PROXY_MAKEFLAGS) check
clean-local:
	$(MAKE) $(EMCC_PROXY_MAKEFLAGS) clean
distclean-local:
	$(MAKE) $(EMCC_PROXY_MAKEFLAGS) distclean

endif

tracepoint_parser_SOURCES = \
	tracepoint_parser.c
