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

hsw.xml: $(srcdir)/oa-hsw.xml $(script_dir)/gputop-oa-codegen.py
	$(PYTHON2) $(PYTHON_FLAGS) $(script_dir)/gputop-oa-codegen.py \
	    --chipset="hsw" $(srcdir)/oa-hsw.xml \
	    --xml-out="hsw.xml"

bdw.xml: $(srcdir)/oa-bdw.xml $(script_dir)/gputop-oa-codegen.py
	$(PYTHON2) $(PYTHON_FLAGS) $(script_dir)/gputop-oa-codegen.py \
	    --chipset="bdw" $(srcdir)/oa-bdw.xml \
	    --xml-out="bdw.xml"

chv.xml: $(srcdir)/oa-chv.xml $(script_dir)/gputop-oa-codegen.py
	$(PYTHON2) $(PYTHON_FLAGS) $(script_dir)/gputop-oa-codegen.py \
	    --chipset="chv" $(srcdir)/oa-chv.xml \
	    --xml-out="chv.xml"

skl.xml: $(srcdir)/oa-skl.xml $(script_dir)/gputop-oa-codegen.py
	$(PYTHON2) $(PYTHON_FLAGS) $(script_dir)/gputop-oa-codegen.py \
	    --chipset="skl" $(srcdir)/oa-skl.xml \
	    --xml-out="skl.xml"

