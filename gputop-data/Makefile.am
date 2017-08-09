include $(top_srcdir)/Makefile.generated

BUILT_SOURCES = \
	$(gputop_xml_files) \
	package.json

remotedir = $(datadir)/remote
nobase_dist_remote_DATA = \
	gputop.proto \
	$(gputop_xml_files)

gputop-%.xml: $(srcdir)/oa-%.xml $(script_dir)/gputop-oa-codegen.py
	$(top_srcdir)/scripts/gputop-oa-codegen.py --chipset=$(*) --xml-out=gputop-$(*).xml oa-$(*).xml

package.json: $(top_srcdir)/scripts/gen-template.py Makefile.am $(top_srcdir)/Makefile.generated package.json.in
	$(PYTHON2) $(PYTHON_FLAGS) $(top_srcdir)/scripts/gen-template.py --template=$(@).in --out=$(@) $(gputop_xml_files)


# Preferable to just use GNU make for running the xml import / i915
# perf codegen scripts...
PROXY_MAKEFLAGS=-f $(srcdir)/Makefile.xml $(AM_MAKEFLAGS) top_srcdir=$(top_srcdir) builddir=.
all-local:
	$(MAKE) $(PROXY_MAKEFLAGS) all
clean-local:
	$(MAKE) $(PROXY_MAKEFLAGS) clean
distclean-local:
	$(MAKE) $(PROXY_MAKEFLAGS) distclean
