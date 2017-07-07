include $(top_srcdir)/Makefile.generated

gputop_xml = \
	gputop-hsw.xml \
	gputop-bdw.xml \
	gputop-chv.xml \
	gputop-sklgt2.xml \
	gputop-sklgt3.xml \
	gputop-sklgt4.xml \
	gputop-bxt.xml \
	gputop-kblgt2.xml \
	gputop-kblgt3.xml \
	gputop-glk.xml

BUILT_SOURCES = $(gputop_xml)

remotedir = $(datadir)/remote
nobase_dist_remote_DATA = \
	gputop.proto \
	$(gputop_xml)

gputop-%.xml: $(srcdir)/oa-%.xml $(script_dir)/gputop-oa-codegen.py
	$(top_srcdir)/scripts/gputop-oa-codegen.py --chipset=$(*) --xml-out=gputop-$(*).xml oa-$(*).xml


# Preferable to just use GNU make for running the xml import / i915
# perf codegen scripts...
PROXY_MAKEFLAGS=-f $(srcdir)/Makefile.xml $(AM_MAKEFLAGS) top_srcdir=$(top_srcdir) builddir=.
all-local:
	$(MAKE) $(PROXY_MAKEFLAGS) all
clean-local:
	$(MAKE) $(PROXY_MAKEFLAGS) clean
distclean-local:
	$(MAKE) $(PROXY_MAKEFLAGS) distclean
