third_party_files = \
	protobuf.min.js \
	bytebuffer.min.js \
	long.min.js \
	jquery.min.js \
	jquery-ui.js \
	jquery.flot.js \
	MathJax/MathJax.js \
	MathJax/extensions/TeX/action.js \
	MathJax/jax/output/HTML-CSS/jax.js \
	MathJax/jax/output/HTML-CSS/fonts/TeX/fontdata.js \
	MathJax/jax/output/HTML-CSS/autoload/maction.js \
	MathJax/fonts/HTML-CSS/TeX/woff/MathJax_Main-Regular.woff \
	MathJax/fonts/HTML-CSS/TeX/woff/MathJax_Math-Italic.woff \
	MathJax/fonts/HTML-CSS/TeX/woff/MathJax_Size1-Regular.woff \
	MathJax/fonts/HTML-CSS/TeX/otf/MathJax_Main-Regular.otf \
	MathJax/fonts/HTML-CSS/TeX/otf/MathJax_Math-Italic.otf \
	MathJax/fonts/HTML-CSS/TeX/otf/MathJax_Size1-Regular.otf \
	MathJax/jax/output/HTML-CSS/imageFonts.js \
	MathJax/fonts/HTML-CSS/TeX/woff/MathJax_Main-Italic.woff \
	MathJax/fonts/HTML-CSS/TeX/otf/MathJax_Main-Italic.otf \
	MathJax/config/TeX-AMS-MML_HTMLorMML.js \
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

remotedir = $(datadir)/remote
nobase_dist_remote_DATA = \
	gputop-web.js \
	gputop-web.js.map \
	index.html \
	gputop.js \
	gputop-ui.js \
	css/gputop.css \
	css/custom.css \
	css/bootstrap-slider.min.css \
	ajax/metrics.html \
	ajax/overview.html \
	assets/gputop_logo.png \
	assets/gputop_logo_44.png \
	$(third_party_files)

nodejsdir = $(libdir)/nodejs/gputop
nobase_dist_nodejs_DATA = \
gputop-nodejs-ui.js \
gputop-nodejs-csv.js \
gputop-nodejs-toolkit.js

nodemodulesdir = $(libdir)/nodejs/gputop
nobase_dist_nodemodules_DATA := \
   $(shell find ./node_modules/{protobufjs,bytebuffer,long,uuid,ws,http,string_decoder,pkill,ultron,options,jsdom,jquery} -type f -name '*.js' -o -name '*.json' -o -name '*.node')

BUILT_SOURCE = gputop-web.js node_modules

node_modules: $(srcdir)/package.json
	npm install

bootstrap% %.js: | node_modules ;

# Automake seems awkward to use for running the emscripten toolchain so
# we have a plain GNU Makefile that we proxy too instead...
EMCC_PROXY_MAKEFLAGS=-f $(srcdir)/Makefile.emscripten $(AM_MAKEFLAGS) top_srcdir=$(top_srcdir)

gputop-web.js.map:
	$(MAKE) $(EMCC_PROXY_MAKEFLAGS) gputop-web.js.map
gputop-web.js:
	$(MAKE) $(EMCC_PROXY_MAKEFLAGS) gputop-web.js

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
