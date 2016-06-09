BUILT_SOURCES = \
    oa-hsw.h \
    oa-hsw.c \
    oa-bdw.h \
    oa-bdw.c \
    oa-chv.h \
    oa-chv.c \
    oa-skl.h \
    oa-skl.c

data_dir = $(top_srcdir)/gputop-data
script_dir = $(top_srcdir)/scripts

oa-hsw.h oa-hsw.c: $(data_dir)/oa-hsw.xml $(script_dir)/gputop-oa-codegen.py
	$(PYTHON2) $(PYTHON_FLAGS) $(script_dir)/gputop-oa-codegen.py \
	    --header=$(builddir)/oa-hsw.h \
	    --code=$(builddir)/oa-hsw.c \
	    --chipset="hsw" $(data_dir)/oa-hsw.xml

oa-bdw.h oa-bdw.c: $(data_dir)/oa-bdw.xml $(script_dir)/gputop-oa-codegen.py
	$(PYTHON2) $(PYTHON_FLAGS) $(script_dir)/gputop-oa-codegen.py \
	    --header=$(builddir)/oa-bdw.h \
	    --code=$(builddir)/oa-bdw.c \
	    --chipset="bdw" $(data_dir)/oa-bdw.xml

oa-chv.h oa-chv.c: $(data_dir)/oa-chv.xml $(script_dir)/gputop-oa-codegen.py
	$(PYTHON2) $(PYTHON_FLAGS) $(script_dir)/gputop-oa-codegen.py \
	    --header=$(builddir)/oa-chv.h \
	    --code=$(builddir)/oa-chv.c \
	    --chipset="chv" $(data_dir)/oa-chv.xml

oa-skl.h oa-skl.c: $(data_dir)/oa-skl.xml $(script_dir)/gputop-oa-codegen.py
	$(PYTHON2) $(PYTHON_FLAGS) $(script_dir)/gputop-oa-codegen.py \
	    --header=$(builddir)/oa-skl.h \
	    --code=$(builddir)/oa-skl.c \
	    --chipset="skl" $(data_dir)/oa-skl.xml

if ENABLE_REMOTE_CLIENTS
remotedir = $(datadir)/remote
nobase_dist_remote_DATA = \
    gputop-web.js \
    gputop-web.js.map

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
