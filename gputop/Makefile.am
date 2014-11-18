
bin_PROGRAMS = gputop

gputop_SOURCES = gputop.c

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
    $(GPUTOP_EXTRA_CPPFLAGS)

