SUBDIRS = registry

bin_PROGRAMS = gputop gputop-system

gputop_SOURCES = gputop-main.c

#gputop_LDADD =
gputop_LDFLAGS = \
    $(GPUTOP_DEP_LIBS) \
    $(GPUTOP_EXTRA_LDFLAGS) \
    -export-dynamic
gputop_CFLAGS = \
    $(GPUTOP_DEP_CFLAGS) \
    $(GPUTOP_EXTRA_CFLAGS)
gputop_CPPFLAGS = \
    -I$(top_srcdir) \
    -I$(srcdir) \
    $(GPUTOP_EXTRA_CPPFLAGS) \
    -DGPUTOP_WRAPPER_DIR=\"$(libdir)/wrappers\"

lib_LTLIBRARIES = libgputop.la
libgputop_la_SOURCES = \
    intel_chipset.h \
    gputop-gl.h \
    gputop-gl.c \
    oa-hsw.h \
    oa-hsw.c \
    gputop-perf.h \
    gputop-perf.c \
    gputop-util.h \
    gputop-list.h \
    gputop-list.c \
    gputop-ui.h \
    gputop-ui.c
libgputop_la_CFLAGS = \
    $(GPUTOP_DEP_CFLAGS) \
    $(GPUTOP_EXTRA_CFLAGS)
libgputop_la_LDFLAGS = $(GPUTOP_DEP_LIBS) $(GPUTOP_EXTRA_LDFLAGS) -lncursesw

wrapperdir = $(libdir)/wrappers
wrapper_LTLIBRARIES = libGL.la #libEGL.la

libGL_la_SOURCES = \
    registry/glxapi.c \
    registry/glapi.c
libGL_la_CFLAGS = \
    $(GPUTOP_DEP_CFLAGS) \
    $(GPUTOP_EXTRA_CFLAGS)
libGL_la_LIBADD = libgputop.la
libGL_la_LDFLAGS = -shared -version-info 1

#libEGL_la_SOURCES = \
#    registry/eglapi.c
#libEGL_la_CFLAGS = \
#    $(GPUTOP_DEP_CFLAGS) \
#    $(GPUTOP_EXTRA_CFLAGS)
#libEGL_la_LIBADD = libgputop.la
#libEGL_la_LDFLAGS = -shared -version-info 1

gputop_system_LDFLAGS = \
    $(GPUTOP_DEP_LIBS) \
    $(GPUTOP_EXTRA_LDFLAGS) \
    $(GL_DEP_LIBS) \
    libgputop.la
gputop_system_CFLAGS = \
    $(GPUTOP_DEP_CFLAGS) \
    $(GPUTOP_EXTRA_CFLAGS) \
    $(GL_DEP_CFLAGS)
gputop_system_CPPFLAGS = \
    -I$(top_srcdir) \
    -I$(srcdir) \
    $(GPUTOP_EXTRA_CPPFLAGS) \
    -DGPUTOP_WRAPPER_DIR=\"$(libdir)/wrappers\"

BUILT_SOURCES = \
    oa-hsw.h \
    oa-hsw.c

oa-hsw.c: oa-hsw.xml oa-gen.py
	$(PYTHON2) $(PYTHON_FLAGS) $(srcdir)/oa-gen.py --code=$(builddir)/oa-hsw.c --include="oa-hsw.h" $(srcdir)/oa-hsw.xml

oa-hsw.h: oa-hsw.xml oa-gen.py
	$(PYTHON2) $(PYTHON_FLAGS) $(srcdir)/oa-gen.py --header=$(builddir)/oa-hsw.h $(srcdir)/oa-hsw.xml
