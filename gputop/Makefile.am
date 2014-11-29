SUBDIRS = registry

bin_PROGRAMS = gputop

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
libgputop_la_SOURCES = gputop.c
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

