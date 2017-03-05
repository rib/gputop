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

# XXX: note that we daisy chain these oa-*.[ch] dependencies so
# only the header directly depends on the script to be generated and
# the .c file depends on the header instead of using a multi target
# rule.  This ensures that a parallel make won't resort to running the
# script twice (with the risk that the two runs trample on each other
# generating the same files).
oa-%.h: $(data_dir)/oa-%.xml $(script_dir)/gputop-oa-codegen.py Makefile.am
	$(PYTHON2) $(PYTHON_FLAGS) $(script_dir)/gputop-oa-codegen.py \
	    --header=$(builddir)/oa-$(*).h \
	    --code=$(builddir)/oa-$(*).c \
	    --chipset="$(*)" $(data_dir)/oa-$(*).xml
oa-%.c: oa-%.h

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

gputop-web.js.map:
	$(MAKE) $(EMCC_PROXY_MAKEFLAGS) gputop-web.js.map
gputop-web.js:
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
