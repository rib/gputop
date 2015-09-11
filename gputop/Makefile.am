SUBDIRS =

if SUPPORT_GL
SUBDIRS += registry
endif

# Note: dot here ensures we build the current directory before
# entering into web/ so that we generate the shared protocol buffers
# code
SUBDIRS += .

if SUPPORT_WEBUI
SUBDIRS += web
endif

bin_PROGRAMS = gputop gputop-system

gputop_SOURCES = gputop-main.c

#gputop_LDADD =
gputop_LDFLAGS = \
    $(GPUTOP_DEP_LIBS) \
    $(GPUTOP_EXTRA_LDFLAGS) \
    -export-dynamic \
    -ldl
gputop_CFLAGS = \
    $(GPUTOP_DEP_CFLAGS) \
    $(GPUTOP_EXTRA_CFLAGS)
gputop_CPPFLAGS = \
    -I$(top_srcdir) \
    -I$(srcdir) \
    -I$(top_srcdir)/h2o/include \
    $(GPUTOP_EXTRA_CPPFLAGS) \
    -DGPUTOP_WRAPPER_DIR=\"$(libdir)/wrappers\"

lib_LTLIBRARIES = libgputop.la
libgputop_la_SOURCES = \
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
    gputop.pb-c.h \
    gputop.pb-c.c \
    gputop-server.h \
    gputop-server.c \
    gputop-util.h \
    gputop-list.h \
    gputop-list.c \
    gputop-log.h \
    gputop-log.c \
    gputop-ui.h \
    gputop-ui.c
libgputop_la_CFLAGS = \
    $(GPUTOP_EXTRA_CFLAGS) \
    $(NCURSES_DEP_CFLAGS) \
    -I$(top_srcdir)/libuv/include \
    -I$(top_srcdir)/h2o \
    -I$(top_srcdir)/h2o/include \
    -DGPUTOP_WEB_ROOT=\"$(datadir)/web\"
libgputop_la_LDFLAGS = \
    $(GPUTOP_EXTRA_LDFLAGS) \
    $(NCURSES_DEP_LIBS) \
    $(top_builddir)/libuv/libuv.la \
    $(top_builddir)/h2o/libh2o.la \
    $(top_builddir)/protobuf-c/libprotobuf-c.la \
    -lm \
    -lssl -lcrypto -lyaml

if SUPPORT_GL
libgputop_la_SOURCES += \
    gputop-gl.h \
    gputop-gl.c

wrapperdir = $(libdir)/wrappers
wrapper_LTLIBRARIES = libGL.la #libEGL.la

libGL_la_SOURCES = \
    registry/glxapi.c \
    registry/glapi.c
libGL_la_CFLAGS = \
    $(GPUTOP_DEP_CFLAGS) \
    $(GPUTOP_EXTRA_CFLAGS)
libGL_la_LIBADD = libgputop.la $(top_builddir)/h2o/libh2o.la -lssl -lcrypto -lcurl -lyaml -lm
libGL_la_LDFLAGS = -shared -version-info 1
endif

#libEGL_la_SOURCES = \
#    registry/eglapi.c
#libEGL_la_CFLAGS = \
#    $(GPUTOP_DEP_CFLAGS) \
#    $(GPUTOP_EXTRA_CFLAGS)
#libEGL_la_LIBADD = libgputop.la
#libEGL_la_LDFLAGS = -shared -version-info 1

gputop_system_LDADD = \
    $(GPUTOP_DEP_LIBS) \
    $(GPUTOP_EXTRA_LDFLAGS) \
    libgputop.la
gputop_system_CFLAGS = \
    $(GPUTOP_DEP_CFLAGS) \
    $(GPUTOP_EXTRA_CFLAGS)
gputop_system_CPPFLAGS = \
    -I$(top_srcdir) \
    -I$(srcdir) \
    $(GPUTOP_EXTRA_CPPFLAGS) \
    -DGPUTOP_WRAPPER_DIR=\"$(libdir)/wrappers\"

if SUPPORT_GL
gputop_system_LDADD += $(GLX_DEP_LIBS)
gputop_system_CFLAGS += $(GLX_DEP_CFLAGS)
endif

BUILT_SOURCES = \
    oa-hsw.h \
    oa-hsw.c \
    oa-bdw.h \
    oa-bdw.c \
    oa-chv.h \
    oa-chv.c \
    oa-skl.h \
    oa-skl.c \
    gputop.pb-c.h \
    gputop.pb-c.c

oa-hsw.c: oa-hsw.xml oa-gen.py
	$(PYTHON2) $(PYTHON_FLAGS) $(srcdir)/oa-gen.py --code=$(builddir)/oa-hsw.c --include="oa-hsw.h" $(srcdir)/oa-hsw.xml

oa-hsw.h: oa-hsw.xml oa-gen.py
	$(PYTHON2) $(PYTHON_FLAGS) $(srcdir)/oa-gen.py --header=$(builddir)/oa-hsw.h $(srcdir)/oa-hsw.xml

oa-bdw.c: oa-bdw.xml oa-gen.py
	$(PYTHON2) $(PYTHON_FLAGS) $(srcdir)/oa-gen.py --code=$(builddir)/oa-bdw.c --include="oa-bdw.h" $(srcdir)/oa-bdw.xml

oa-bdw.h: oa-bdw.xml oa-gen.py
	$(PYTHON2) $(PYTHON_FLAGS) $(srcdir)/oa-gen.py --header=$(builddir)/oa-bdw.h $(srcdir)/oa-bdw.xml

oa-chv.c: oa-chv.xml oa-gen.py
	$(PYTHON2) $(PYTHON_FLAGS) $(srcdir)/oa-gen.py --code=$(builddir)/oa-chv.c --include="oa-chv.h" $(srcdir)/oa-chv.xml

oa-chv.h: oa-chv.xml oa-gen.py
	$(PYTHON2) $(PYTHON_FLAGS) $(srcdir)/oa-gen.py --header=$(builddir)/oa-chv.h $(srcdir)/oa-chv.xml

oa-skl.c: oa-skl.xml oa-gen.py
	$(PYTHON2) $(PYTHON_FLAGS) $(srcdir)/oa-gen.py --code=$(builddir)/oa-skl.c --include="oa-skl.h" $(srcdir)/oa-skl.xml

oa-skl.h: oa-skl.xml oa-gen.py
	$(PYTHON2) $(PYTHON_FLAGS) $(srcdir)/oa-gen.py --header=$(builddir)/oa-skl.h $(srcdir)/oa-skl.xml

%.pb-c.c %.pb-c.h: %.proto
	$(top_builddir)/protoc-c/protoc-c --c_out=$(top_builddir)/gputop -I$(srcdir) $(srcdir)/$(*F).proto

EXTRA_DIST = gputop.proto

