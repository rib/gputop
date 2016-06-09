include Makefile.mathjax

SUBDIRS =

third_party_files = \
	protobuf.min.js \
	bytebuffer.min.js \
	long.min.js \
	jquery.min.js \
	jquery-ui.js \
	jquery.flot.js \
	jquery.flot.navigate.js \
	$(mathjax_files) \
	bootstrap-slider.min.js \
	bootstrap/css/bootstrap.min.css \
	bootstrap/css/bootstrap.min.css.map \
	bootstrap/css/bootstrap-theme.min.css \
	bootstrap/css/bootstrap.css \
	bootstrap/css/bootstrap-theme.min.css.map \
	bootstrap/js/bootstrap.min.js \
	bootstrap/fonts/glyphicons-halflings-regular.woff \
	bootstrap/fonts/glyphicons-halflings-regular.ttf \
	bootstrap/fonts/glyphicons-halflings-regular.eot \
	bootstrap/fonts/glyphicons-halflings-regular.woff2 \
	bootstrap/fonts/glyphicons-halflings-regular.svg \
	bootbox.min.js \
	plotly/plotly.min.js

built_xml = \
	hsw.xml \
	bdw.xml \
	chv.xml \
	skl.xml

remotedir = $(datadir)/remote
nobase_dist_remote_DATA = \
	index.html \
	gputop.js \
	gputop-ui.js \
	css/gputop.css \
	css/custom.css \
	css/bootstrap-slider.min.css \
	ajax/metrics.html \
	ajax/overview.html \
	ajax/welcome.html \
	assets/gputop_logo.png \
	assets/gputop_logo_44.png \
	$(built_xml) \
	$(third_party_files)

BUILT_SOURCES = \
	gputop-web.js node_modules \
	$(built_xml)

data_dir = $(top_srcdir)/gputop-data
script_dir = $(top_srcdir)/scripts

hsw.xml: $(data_dir)/oa-hsw.xml $(script_dir)/gputop-oa-codegen.py
	$(PYTHON2) $(PYTHON_FLAGS) $(script_dir)/gputop-oa-codegen.py \
	    --header=$(builddir)/oa-hsw.h \
	    --code=$(builddir)/oa-hsw.c \
	    --chipset="hsw" $(data_dir)/oa-hsw.xml \
	    --xml_eq="hsw.xml"

bdw.xml: $(data_dir)/oa-bdw.xml $(script_dir)/gputop-oa-codegen.py
	$(PYTHON2) $(PYTHON_FLAGS) $(script_dir)/gputop-oa-codegen.py \
	    --header=$(builddir)/oa-bdw.h \
	    --code=$(builddir)/oa-bdw.c \
	    --chipset="bdw" $(data_dir)/oa-bdw.xml \
	    --xml_eq="bdw.xml"

chv.xml: $(data_dir)/oa-chv.xml $(script_dir)/gputop-oa-codegen.py
	$(PYTHON2) $(PYTHON_FLAGS) $(script_dir)/gputop-oa-codegen.py \
	    --header=$(builddir)/oa-chv.h \
	    --code=$(builddir)/oa-chv.c \
	    --chipset="chv" $(data_dir)/oa-chv.xml \
	    --xml_eq="chv.xml"

skl.xml: $(data_dir)/oa-skl.xml $(script_dir)/gputop-oa-codegen.py
	$(PYTHON2) $(PYTHON_FLAGS) $(script_dir)/gputop-oa-codegen.py \
	    --header=$(builddir)/oa-skl.h \
	    --code=$(builddir)/oa-skl.c \
	    --chipset="skl" $(data_dir)/oa-skl.xml \
	    --xml_eq="skl.xml"

node_modules: $(srcdir)/package.json
	npm install

bootstrap% %.js: | node_modules ;

all-local: | node_modules

distclean-local:
	rm -fr node_modules
install-data-local: | node_modules ;
	npm install -g --prefix=$(prefix) --production --cache-min 999999999

CLEANFILES = $(BUILT_SOURCES)
