SUBDIRS = libuv

if SUPPORT_WEBUI
SUBDIRS += protobuf-c protoc-c h2o
endif

SUBDIRS += gputop

if SUPPORT_WEBUI
SUBDIRS += gputop-csv
endif

ACLOCAL_AMFLAGS = -I build/autotools ${ACLOCAL_FLAGS}
