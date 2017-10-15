data_dir = $(top_srcdir)/gputop-data

bin_PROGRAMS = gputop-ui

gputop_ui_SOURCES = \
	imgui/imconfig.h \
	imgui/imgui.cpp \
	imgui/imgui_demo.cpp \
	imgui/imgui_draw.cpp \
	imgui/imgui.h \
	imgui/imgui_internal.h \
	imgui/stb_rect_pack.h \
	imgui/stb_textedit.h \
	imgui/stb_truetype.h \
	imgui/imgui_impl_gtk3_cogl.cpp \
	imgui/imgui_impl_gtk3_cogl.h \
	\
	gputop-soup-network.c \
	gputop-ui-main.cpp \
	gputop-ui-multilines.cpp \
	gputop-ui-multilines.h \
	gputop-ui-timeline.cpp \
	gputop-ui-timeline.h \
	gputop-ui-topology.cpp \
	gputop-ui-topology.h \
	gputop-ui-utils.cpp \
	gputop-ui-utils.h


gputop_ui_CPPFLAGS = \
	-I$(top_srcdir) \
	-I$(top_srcdir)/gputop-client-c \
	\
	-I$(top_srcdir)/mesa/include \
	-I$(top_srcdir)/mesa/include/drm-uapi \
	-I$(top_srcdir)/mesa/src \
	-I$(top_srcdir)/mesa/src/intel \
	-I$(top_srcdir)/mesa/src/util \
	\
	-I$(srcdir)/imgui \
	$(LIBSOUP_CFLAGS) \
	$(GPUTOP_UI_CFLAGS)
gputop_ui_CFLAGS = \
	-std=c99
gputop_ui_LDFLAGS = \
	$(LIBSOUP_LIBS) \
	$(GPUTOP_UI_LIBS) \
	-ldl
gputop_ui_LDADD = \
	$(top_builddir)/mesa/libmesa.la \
	$(top_builddir)/gputop-client-c/libgputop-client.la


CLEANFILES = $(BUILT_SOURCES)
