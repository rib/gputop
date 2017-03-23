BUILT_SOURCES = \
    oa-hsw.h \
    oa-hsw.c \
    oa-bdw.h \
    oa-bdw.c \
    oa-chv.h \
    oa-chv.c \
    oa-sklgt2.h \
    oa-sklgt2.c \
    oa-sklgt3.h \
    oa-sklgt3.c \
    oa-sklgt4.h \
    oa-sklgt4.c \
    oa-bxt.h \
    oa-bxt.c

data_dir = $(top_srcdir)/gputop-data
script_dir = $(top_srcdir)/scripts

# Note: we avoid using a multi target rule here and outputting both the
# .c and .h files in one go so we don't hit problems with parallel
# make and multiple invocations of the same script trying to write
# to the same files.
oa-%.h: $(data_dir)/oa-%.xml $(script_dir)/gputop-oa-codegen.py Makefile.am
	$(PYTHON2) $(PYTHON_FLAGS) $(script_dir)/gputop-oa-codegen.py \
	    --header=$(builddir)/oa-$(*).h \
	    --chipset="$(*)" $(data_dir)/oa-$(*).xml
oa-%.c: $(data_dir)/oa-%.xml $(script_dir)/gputop-oa-codegen.py Makefile.am
	$(PYTHON2) $(PYTHON_FLAGS) $(script_dir)/gputop-oa-codegen.py \
	    --code=$(builddir)/oa-$(*).c \
	    --chipset="$(*)" $(data_dir)/oa-$(*).xml

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
