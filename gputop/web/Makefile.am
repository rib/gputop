
jqueryui_images = \
    images/ui-bg_diagonals-thick_20_666666_40x40.png \
    images/ui-bg_glass_100_fdf5ce_1x400.png \
    images/ui-bg_highlight-soft_75_ffe45c_1x100.png \
    images/ui-icons_ffd27a_256x240.png \
    images/ui-icons_ef8c08_256x240.png \
    images/ui-bg_glass_65_ffffff_1x400.png \
    images/ui-bg_diagonals-thick_18_b81900_40x40.png \
    images/ui-bg_highlight-soft_100_eeeeee_1x100.png \
    images/ui-icons_222222_256x240.png \
    images/ui-icons_ffffff_256x240.png \
    images/ui-bg_glass_100_f6f6f6_1x400.png \
    images/ui-bg_gloss-wave_35_f6a828_500x100.png \
    images/ui-icons_228ef1_256x240.png \
    images/ui-bg_flat_10_000000_40x100.png

webdir = $(datadir)/web
nobase_dist_web_DATA = \
	jquery.js \
	jquery-ui.js \
	jquery-ui.css \
	$(jqueryui_images) \
	jquery.flot.js \
	jquery.flot.categories.js \
	snap.svg.js \
	index.html \
	gputop-ui.js \
	gputop-web-worker.js \
	haswell.svg

BUILT_SOURCE = gputop-web-worker.js

# Automake seems awkward to use for running the emscripten toolchain so
# we have a plain GNU Makefile that we proxy too instead...
EMCC_PROXY_MAKEFLAGS=-f $(srcdir)/Makefile.emscripten $(AM_MAKEFLAGS) top_srcdir=$(top_srcdir)

gputop-web-worker.js:
	$(MAKE) $(EMCC_PROXY_MAKEFLAGS) all

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
