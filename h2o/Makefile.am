klib_source = \
    klib/kmath.h \
    klib/kthread.c \
    klib/kbtree.h \
    klib/kgraph.h \
    klib/ksw.c \
    klib/knhx.h \
    klib/knhx.c \
    klib/kmath.c \
    klib/kopen.c \
    klib/kstring.c \
    klib/kson.h \
    klib/ksw.h \
    klib/khmm.c \
    klib/kvec.h \
    klib/knetfile.h \
    klib/kson.c \
    klib/ksa.c \
    klib/klist.h \
    klib/kseq.h \
    klib/knetfile.c \
    klib/kstring.h \
    klib/khmm.h \
    klib/khash.h \
    klib/bgzf.c \
    klib/bgzf.h \
    klib/kbit.h \
    klib/ksort.h

#    klib/kurl.h
#    klib/kurl.c

picohttpparser_source = \
    picohttpparser/picohttpparser.c \
    picohttpparser/picohttpparser.h

picotest_source = \
    picotest/picotest.c \
    picotest/picotest.h

yoml_source = \
    yoml/yoml-parser.h \
    yoml/yoml.h

#    yoml/test-yoml.c

wslay_source = \
    wslay/wslay_net.c \
    wslay/wslay_event.h \
    wslay/wslay_queue.c \
    wslay/wslay_queue.h \
    wslay/wslay.h \
    wslay/wslay_stack.c \
    wslay/wslay_event.c \
    wslay/wslay_net.h \
    wslay/wslay_frame.h \
    wslay/wslayver.h \
    wslay/wslay_frame.c \
    wslay/wslay_stack.h

noinst_LTLIBRARIES=libh2o.la

libh2o_la_CFLAGS= \
   -I$(top_builddir)/wslay/includes \
   -I$(srcdir)/include \
   -I$(srcdir)/klib \
   -I$(srcdir)/picohttpparser \
   -I$(srcdir)/picotest \
   -I$(srcdir)/yoml

libh2o_la_SOURCES= \
    $(klib_source) \
    $(picohttpparser_source) \
    $(picotest_source) \
    $(yoml_source) \
    $(wslay_source) \
    handler/file/templates.c.h \
    handler/file/_templates.c.h \
    handler/mimemap.c \
    handler/mimemap/mimemap.c.h \
    handler/chunked.c \
    handler/headers.c \
    handler/redirect.c \
    handler/reproxy.c \
    handler/file.c \
    handler/proxy.c \
    handler/access_log.c \
    handler/configurator/redirect.c \
    handler/configurator/file.c \
    handler/configurator/headers.c \
    handler/configurator/proxy.c \
    handler/configurator/reproxy.c \
    handler/configurator/access_log.c \
    handler/configurator/expires.c \
    handler/expires.c \
    common/hostinfo.c \
    common/time.c \
    common/serverutil.c \
    common/http1client.c \
    common/socket/uv-binding.c.h \
    common/socket/evloop.c.h \
    common/socket/evloop/select.c.h \
    common/socket/evloop/kqueue.c.h \
    common/socket/evloop/epoll.c.h \
    common/socket.c \
    common/string.c \
    common/socketpool.c \
    common/url.c \
    common/timeout.c \
    common/memory.c \
    common/multithread.c \
    core/token_table.h \
    core/configurator.c \
    core/util.c \
    core/config.c \
    core/context.c \
    core/headers.c \
    core/proxy.c \
    core/token.c \
    core/request.c \
    websocket.c \
    http1.c \
    http2/frame.c \
    http2/hpack.c \
    http2/stream.c \
    http2/hpack_static_table.h \
    http2/scheduler.c \
    http2/hpack_huffman_table.h \
    http2/connection.c

