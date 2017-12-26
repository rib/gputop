bin_PROGRAMS = gputop-wrapper

gputop_wrapper_SOURCES = \
	gputop-uv-network.c \
	gputop-wrapper-main.c
gputop_wrapper_CFLAGS = \
	\
	-I$(top_srcdir)/mesa/include \
	-I$(top_srcdir)/mesa/include/drm-uapi \
	-I$(top_srcdir)/mesa/src \
	-I$(top_srcdir)/mesa/src/intel \
	-I$(top_srcdir)/mesa/src/util \
	\
	-I$(top_srcdir)/libuv/include \
	\
	-I$(top_srcdir)/h2o \
	\
	-I$(top_srcdir)/gputop-client-c \
	$(LIBSOUP_CFLAGS)
gputop_wrapper_LDADD = \
	$(top_builddir)/mesa/libmesa.la \
	$(top_builddir)/gputop-client-c/libgputop-client.la \
	$(LIBSOUP_LIBS)
