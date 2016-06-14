built_xml = \
	hsw.xml \
	bdw.xml \
	chv.xml \
	skl.xml

BUILT_SOURCES = $(built_xml)

remotedir = $(datadir)/remote
nobase_dist_remote_DATA = \
	gputop.proto \
	$(built_xml)

script_dir = $(top_srcdir)/scripts

%.xml: $(srcdir)/oa-%.xml $(script_dir)/gputop-oa-codegen.py $(srcdir)/gen-client-xml.py
	$(PYTHON2) $(PYTHON_FLAGS) $(srcdir)/gen-client-xml.py
