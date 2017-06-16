
noinst_PROGRAMS = protoc-c

AM_CPPFLAGS = \
    -include $(top_builddir)/config.h \
    -I$(top_srcdir) \
    -I$(top_srcdir)/protobuf-c \
    -I$(top_srcdir)/protobuf \
    $(PROTOBUF_DEP_CFLAGS)

protoc_c_SOURCES = \
    c_bytes_field.cc \
    c_bytes_field.h \
    c_enum.cc \
    c_enum.h \
    c_enum_field.cc \
    c_enum_field.h \
    c_extension.cc \
    c_extension.h \
    c_field.cc \
    c_field.h \
    c_file.cc \
    c_file.h \
    c_generator.cc \
    c_generator.h \
    c_helpers.cc \
    c_helpers.h \
    c_message.cc \
    c_message.h \
    c_message_field.cc \
    c_message_field.h \
    c_primitive_field.cc \
    c_primitive_field.h \
    c_service.cc \
    c_service.h \
    c_string_field.cc \
    c_string_field.h \
    main.cc

protoc_c_LDADD = \
    -lstdc++ \
    -lpthread \
    $(top_builddir)/protobuf/libprotobuf.la \
    $(top_builddir)/protobuf-c/libprotobuf-c.la

EXTRA_DIST = LICENSE
