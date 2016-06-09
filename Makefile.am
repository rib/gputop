SUBDIRS = libuv

SUBDIRS += protobuf-c protoc-c h2o

SUBDIRS += gputop-data gputop-server

if ENABLE_REMOTE_CLIENTS
SUBDIRS += gputop-client-c gputop-client gputop-csv gputop-tracepoints
endif

ACLOCAL_AMFLAGS = -I build/autotools ${ACLOCAL_FLAGS}
