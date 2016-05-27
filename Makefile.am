SUBDIRS = libuv

SUBDIRS += protobuf-c protoc-c h2o

SUBDIRS += gputop

if ENABLE_REMOTE_CLIENTS
SUBDIRS += gputop-csv
endif

ACLOCAL_AMFLAGS = -I build/autotools ${ACLOCAL_FLAGS}
