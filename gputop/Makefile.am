SUBDIRS =

if SUPPORT_GL
SUBDIRS += registry
endif

# Note: dot here ensures we build the current directory before
# entering into web/ so that we generate the shared protocol buffers
# code
SUBDIRS += .

if SUPPORT_WEBUI
SUBDIRS += remote
endif

bin_PROGRAMS = gputop gputop-system

gputop_SOURCES = gputop-main.c

#gputop_LDADD =
gputop_LDFLAGS = \
    $(GPUTOP_EXTRA_LDFLAGS) \
    -ldl
gputop_CFLAGS = \
    $(GPUTOP_EXTRA_CFLAGS) \
    -std=gnu99
gputop_CPPFLAGS = \
    -I$(top_srcdir) \
    -I$(srcdir) \
    -DGPUTOP_LIB_DIR=\"$(libdir)\"

lib_LTLIBRARIES = libgputop.la
libgputop_la_SOURCES = \
    gputop-hash-table.h \
    gputop-hash-table.c \
    intel_chipset.h \
    oa-hsw.h \
    oa-hsw.c \
    oa-bdw.h \
    oa-bdw.c \
    oa-chv.h \
    oa-chv.c \
    oa-skl.h \
    oa-skl.c \
    gputop-perf.h \
    gputop-perf.c \
    gputop-util.h \
    gputop-util.c \
    gputop-list.h \
    gputop-list.c \
    gputop-log.h \
    gputop-log.c \
    gputop-ui.h \
    gputop-ui.c \
    gputop-oa-counters.h \
    gputop-oa-counters.c \
    gputop-cpu.h \
    gputop-cpu.c \
    gputop-ioctl.c
libgputop_la_CFLAGS = \
    $(GPUTOP_EXTRA_CFLAGS) \
    $(NCURSES_DEP_CFLAGS) \
    $(PTHREAD_CFLAGS) \
    -I$(top_srcdir)/libuv/include \
    -I$(top_srcdir)/h2o \
    -I$(top_srcdir)/h2o/include \
    -DGPUTOP_WEB_ROOT=\"$(datadir)/remote\" \
    -DGPUTOP_NODEJS_ROOT=\"$(libdir)/nodejs/gputop\" \
    -std=gnu99
libgputop_la_LIBADD = \
    $(GPUTOP_EXTRA_LDFLAGS) \
    $(NCURSES_DEP_LIBS) \
    $(PTHREAD_LIBS) \
    $(top_builddir)/libuv/libuv.la \
    -ldl \
    -lm \
    -lrt

oa_xml_files = \
    oa-hsw.xml \
    oa-bdw.xml \
    oa-chv.xml \
    oa-skl.xml

built_oa_xml_files = \
    hsw.xml \
    bdw.xml \
    chv.xml \
    skl.xml

if SUPPORT_WEBUI
libgputop_la_SOURCES += \
    gputop.pb-c.h \
    gputop.pb-c.c \
    gputop-server.h \
    gputop-server.c
libgputop_la_CFLAGS += \
    $(PROTOBUF_DEP_CFLAGS)
libgputop_la_LIBADD += \
    $(top_builddir)/h2o/libh2o.la \
    $(top_builddir)/protobuf-c/libprotobuf-c.la \
    $(PROTOBUF_DEP_LIBS) \
    -lssl -lcrypto -lyaml

remotedir = $(datadir)/remote
nobase_dist_remote_DATA = \
    gputop.proto \
    $(oa_xml_files) \
    $(built_oa_xml_files)
endif

if SUPPORT_GL
libgputop_la_SOURCES += \
    gputop-gl.h \
    gputop-gl.c

# XXX: it's awkward to link libgputop.so with the systen libGL without
# libtool getting confused (it will think we want to link with our
# wrapper libGL.so but doing that introduces a cyclical dependency)
# gputop-gl.c is instead careful to lookup GLX symbols via
# dlopen/dlsym so we don't need to be linked to the system GL library.
#libgputop_la_LIBADD += $(GLX_DEP_LIBS)
libgputop_la_LIBADD += -lX11
libgputop_la_CFLAGS += $(GLX_DEP_CFLAGS)

wrapperdir = $(libdir)/wrappers
wrapper_LTLIBRARIES = libfakeGL.la #libEGL.la

libfakeGL_la_SOURCES = \
    registry/glxapi.c \
    registry/glapi.c
libfakeGL_la_CFLAGS = \
    $(GPUTOP_EXTRA_CFLAGS)
libfakeGL_la_LIBADD = libgputop.la
libfakeGL_la_LDFLAGS = -shared -version-info 1
endif

#libEGL_la_SOURCES = \
#    registry/eglapi.c
#libEGL_la_CFLAGS = \
#    $(GPUTOP_EXTRA_CFLAGS)
#libEGL_la_LIBADD = libgputop.la
#libEGL_la_LDFLAGS = -shared -version-info 1

gputop_system_LDADD = \
    $(GPUTOP_EXTRA_LDFLAGS) \
    libgputop.la
gputop_system_CFLAGS = \
    $(GPUTOP_EXTRA_CFLAGS)
gputop_system_CPPFLAGS = \
    -I$(top_srcdir) \
    -I$(srcdir)

BUILT_SOURCES = \
    oa-hsw.h \
    oa-hsw.c \
    oa-bdw.h \
    oa-bdw.c \
    oa-chv.h \
    oa-chv.c \
    oa-skl.h \
    oa-skl.c \
    $(built_oa_xml_files)

if SUPPORT_WEBUI
BUILT_SOURCES += \
    gputop.pb-c.h \
    gputop.pb-c.c
endif

oa-hsw.h oa-hsw.c hsw.xml: oa-hsw.xml oa-gen.py
	$(PYTHON2) $(PYTHON_FLAGS) $(srcdir)/oa-gen.py \
	    --header=$(builddir)/oa-hsw.h \
	    --code=$(builddir)/oa-hsw.c \
	    --chipset="hsw" $(srcdir)/oa-hsw.xml \
	    --xml_eq="hsw.xml"

oa-bdw.h oa-bdw.c bdw.xml: oa-bdw.xml oa-gen.py
	$(PYTHON2) $(PYTHON_FLAGS) $(srcdir)/oa-gen.py \
	    --header=$(builddir)/oa-bdw.h \
	    --code=$(builddir)/oa-bdw.c \
	    --chipset="bdw" $(srcdir)/oa-bdw.xml \
	    --xml_eq="bdw.xml"

oa-chv.h oa-chv.c chv.xml: oa-chv.xml oa-gen.py
	$(PYTHON2) $(PYTHON_FLAGS) $(srcdir)/oa-gen.py \
	    --header=$(builddir)/oa-chv.h \
	    --code=$(builddir)/oa-chv.c \
	    --chipset="chv" $(srcdir)/oa-chv.xml \
	    --xml_eq="chv.xml"

oa-skl.h oa-skl.c skl.xml: oa-skl.xml oa-gen.py
	$(PYTHON2) $(PYTHON_FLAGS) $(srcdir)/oa-gen.py \
	    --header=$(builddir)/oa-skl.h \
	    --code=$(builddir)/oa-skl.c \
	    --chipset="skl" $(srcdir)/oa-skl.xml \
	    --xml_eq="skl.xml"

%.pb-c.c %.pb-c.h: %.proto
	$(top_builddir)/protoc-c/protoc-c --c_out=$(top_builddir)/gputop -I$(srcdir) $(srcdir)/$(*F).proto

EXTRA_DIST = gputop.proto
CLEANFILES = $(BUILT_SOURCES)
