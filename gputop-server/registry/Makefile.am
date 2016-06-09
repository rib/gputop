BUILT_SOURCES = glxapi.c glapi.c eglapi.c
CLEANFILES = $(BUILT_SOURCES)

EXTRA_DIST = gl.xml glx.xml egl.xml reg.py genapis.py

%api.c: %.xml
	$(srcdir)/genapis.py $(srcdir)

