noinst_LTLIBRARIES = libmesa.la

noinst_DATA = \
	include/drm-uapi/README

libmesa_la_CPPFLAGS = \
	-DHAVE_PTHREAD \
	-I$(srcdir)/include \
	-I$(srcdir)/src \
	-I$(srcdir)/src/mesa \
	-I$(srcdir)/util
libmesa_la_SOURCES = \
	include/c99_compat.h \
	include/no_extern_c.h \
	include/drm-uapi/drm.h \
	include/drm-uapi/i915_drm.h \
	include/drm-uapi/drm_fourcc.h \
	include/drm-uapi/drm_mode.h \
	include/pci_ids/i965_pci_ids.h \
	\
	src/compiler/shader_enums.h \
	\
	src/intel/common/gen_device_info.c \
	src/intel/common/gen_device_info.h \
	\
	src/util/hash_table.c \
	src/util/hash_table.h \
	src/util/list.h \
	src/util/macros.h \
	src/util/ralloc.c \
	src/util/ralloc.h


BUILT_SOURCES = \
	package.json

package.json: $(top_srcdir)/scripts/gen-template.py Makefile.am package.json.in
	$(PYTHON2) $(PYTHON_FLAGS) $(top_srcdir)/scripts/gen-template.py --template=$(@).in --out=$(@) $(libmesa_la_SOURCES)
