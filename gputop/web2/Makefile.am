third_party_files = \
	protobuf.min.js \
	bytebuffer.min.js \
	long.min.js \
	jquery.min.js \
	jquery-ui.js \
	jquery.flot.js \
	bootstrap-wysiwyg.min.js \
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
	bootstrap/fonts/glyphicons-halflings-regular.svg

webdir = $(datadir)/web2
nobase_dist_web_DATA = \	
	gputop-web-v2.js \
	gputop-web-v2.js.map \
	index.html \
	Gputop.js \
	Gputop_ui.js \
	css/gputop.css \
	css/custom.css \
	css/slider.css \
	ajax/metrics.html \
	ajax/overview.html \
	assets/gputop_logo.png \
	assets/gputop_logo_44.png \
	$(third_party_files)

BUILT_SOURCE = gputop-web-v2.js node_modules

node_modules: $(srcdir)/package.json
	npm install

bootstrap% %.js: | node_modules ;

# Automake seems awkward to use for running the emscripten toolchain so
# we have a plain GNU Makefile that we proxy too instead...
EMCC_PROXY_MAKEFLAGS=-f $(srcdir)/Makefile.emscripten $(AM_MAKEFLAGS) top_srcdir=$(top_srcdir)

gputop-web-v2.js.map:
	$(MAKE) $(EMCC_PROXY_MAKEFLAGS) gputop-web-v2.js.map
gputop-web-v2.js:
	$(MAKE) $(EMCC_PROXY_MAKEFLAGS) gputop-web-v2.js
	
all-local: | node_modules
	$(MAKE) $(EMCC_PROXY_MAKEFLAGS) all
#check-local:
#	$(MAKE) $(EMCC_PROXY_MAKEFLAGS) check
clean-local:
	$(MAKE) $(EMCC_PROXY_MAKEFLAGS) clean
distclean-local:
	$(MAKE) $(EMCC_PROXY_MAKEFLAGS) distclean
	rm -fr node_modules
install-data-local: | node_modules ;
#	$(MAKE) $(EMCC_PROXY_MAKEFLAGS) install-data
#install-exec-local:
#	$(MAKE) $(EMCC_PROXY_MAKEFLAGS) install-exec
#uninstall-local:
#	$(MAKE) $(EMCC_PROXY_MAKEFLAGS) uninstall
