include Makefile.mathjax

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
	$(third_party_files)

BUILT_SOURCES = \
	gputop-web.js node_modules

node_modules: $(srcdir)/package.json
	npm install

bootstrap% %.js: | node_modules ;

all-local: | node_modules

if ENABLE_NODE_CLIENTS
distclean-local:
	rm -fr node_modules
install-data-local: | node_modules ;
	env NODE_GYP_FLAGS=-Dtop_srcdir=$(shell realpath $(top_srcdir)) npm install -g --prefix=$(prefix) --production --cache-min 999999999
endif

CLEANFILES = $(BUILT_SOURCES)
