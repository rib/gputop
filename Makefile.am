SUBDIRS = libuv

SUBDIRS += protobuf-c protoc-c h2o

SUBDIRS += gputop-data gputop-server

if ENABLE_CLIENTS
SUBDIRS += gputop-client-c gputop-client
endif

if ENABLE_NODE_CLIENTS
SUBDIRS += gputop-csv gputop-tracepoints
endif

ACLOCAL_AMFLAGS = -I build/autotools ${ACLOCAL_FLAGS}
