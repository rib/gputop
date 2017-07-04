noinst_LTLIBRARIES = src/intel/common/libintel_common.la

noinst_DATA = \
	include/drm-uapi/README

src_intel_common_libintel_common_la_CPPFLAGS = \
	-I$(srcdir)/include \
	-I$(srcdir)/src \
	-I$(srcdir)/util
src_intel_common_libintel_common_la_SOURCES = \
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
	src/util/macros.h

BUILT_SOURCES = \
	package.json

package.json: $(top_srcdir)/scripts/gen-template.py Makefile.am package.json.in
	$(PYTHON2) $(PYTHON_FLAGS) $(top_srcdir)/scripts/gen-template.py --template=$(@).in --out=$(@) $(src_intel_common_libintel_common_la_SOURCES)
