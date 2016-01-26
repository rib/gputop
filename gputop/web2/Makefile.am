webdir = $(datadir)/web
nobase_dist_web_DATA = \	
	gputop-web-v2.js \
	gputop-web-v2.js.map 

BUILT_SOURCE = gputop-web-v2.js

# Automake seems awkward to use for running the emscripten toolchain so
# we have a plain GNU Makefile that we proxy too instead...
EMCC_PROXY_MAKEFLAGS=-f $(srcdir)/Makefile.emscripten $(AM_MAKEFLAGS) top_srcdir=$(top_srcdir)

gputop-web-v2.js.map:
	$(MAKE) $(EMCC_PROXY_MAKEFLAGS) gputop-web-v2.js.map
gputop-web-v2.js:
	$(MAKE) $(EMCC_PROXY_MAKEFLAGS) gputop-web-v2.js
	
all-local:
	$(MAKE) $(EMCC_PROXY_MAKEFLAGS) all
#check-local:
#	$(MAKE) $(EMCC_PROXY_MAKEFLAGS) check
clean-local:
	$(MAKE) $(EMCC_PROXY_MAKEFLAGS) clean
distclean-local:
	$(MAKE) $(EMCC_PROXY_MAKEFLAGS) distclean
#install-data-local:
#	$(MAKE) $(EMCC_PROXY_MAKEFLAGS) install-data
#install-exec-local:
#	$(MAKE) $(EMCC_PROXY_MAKEFLAGS) install-exec
#uninstall-local:
#	$(MAKE) $(EMCC_PROXY_MAKEFLAGS) uninstall
