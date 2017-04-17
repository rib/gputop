brotli_source = \
    brotli/dec/bit_reader.c \
    brotli/dec/bit_reader.h \
    brotli/dec/context.h \
    brotli/dec/decode.c \
    brotli/dec/decode.h \
    brotli/dec/dictionary.h \
    brotli/dec/huffman.c \
    brotli/dec/huffman.h \
    brotli/dec/port.h \
    brotli/dec/prefix.h \
    brotli/dec/state.c \
    brotli/dec/state.h \
    brotli/dec/transform.h \
    brotli/dec/types.h \
    \
    brotli/enc/backward_references.cc \
    brotli/enc/backward_references.h \
    brotli/enc/bit_cost.h \
    brotli/enc/block_splitter.cc \
    brotli/enc/block_splitter.h \
    brotli/enc/brotli_bit_stream.cc \
    brotli/enc/brotli_bit_stream.h \
    brotli/enc/cluster.h \
    brotli/enc/command.h \
    brotli/enc/compress_fragment.cc \
    brotli/enc/compress_fragment.h \
    brotli/enc/compress_fragment_two_pass.cc \
    brotli/enc/compress_fragment_two_pass.h \
    brotli/enc/context.h \
    brotli/enc/dictionary.cc \
    brotli/enc/dictionary.h \
    brotli/enc/dictionary_hash.h \
    brotli/enc/encode.cc \
    brotli/enc/encode.h \
    brotli/enc/encode_parallel.cc \
    brotli/enc/encode_parallel.h \
    brotli/enc/entropy_encode.cc \
    brotli/enc/entropy_encode.h \
    brotli/enc/entropy_encode_static.h \
    brotli/enc/fast_log.h \
    brotli/enc/find_match_length.h \
    brotli/enc/hash.h \
    brotli/enc/histogram.cc \
    brotli/enc/histogram.h \
    brotli/enc/literal_cost.cc \
    brotli/enc/literal_cost.h \
    brotli/enc/metablock.cc \
    brotli/enc/metablock.h \
    brotli/enc/port.h \
    brotli/enc/prefix.h \
    brotli/enc/ringbuffer.h \
    brotli/enc/static_dict.cc \
    brotli/enc/static_dict.h \
    brotli/enc/static_dict_lut.h \
    brotli/enc/streams.cc \
    brotli/enc/streams.h \
    brotli/enc/transform.h \
    brotli/enc/types.h \
    brotli/enc/utf8_util.cc \
    brotli/enc/utf8_util.h \
    brotli/enc/write_bits.h

cloexec_source = \
    cloexec/cloexec.c \
    cloexec/cloexec.h

hiredis_source = \
    hiredis/adapters/ae.h \
    hiredis/adapters/ivykis.h \
    hiredis/adapters/libevent.h \
    hiredis/adapters/libev.h \
    hiredis/adapters/libuv.h \
    hiredis/async.c \
    hiredis/async.h \
    hiredis/dict.c \
    hiredis/dict.h \
    hiredis/fmacros.h \
    hiredis/hiredis.c \
    hiredis/hiredis.h \
    hiredis/net.c \
    hiredis/net.h \
    hiredis/read.c \
    hiredis/read.h \
    hiredis/sdsalloc.h \
    hiredis/sds.c \
    hiredis/sds.h

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

libgkc_source = \
    libgkc/gkc.c \
    libgkc/gkc.h

libyrmcds_source = \
    libyrmcds/close.c \
    libyrmcds/connect.c \
    libyrmcds/counter.c \
    libyrmcds/recv.c \
    libyrmcds/send.c \
    libyrmcds/send_text.c \
    libyrmcds/set_compression.c \
    libyrmcds/socket.c \
    libyrmcds/strerror.c \
    libyrmcds/text_mode.c \
    libyrmcds/yrmcds.h \
    libyrmcds/yrmcds_portability.h \
    libyrmcds/yrmcds_text.h

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

h2o_includes = \
   -I$(top_srcdir)/libuv/include \
   -I$(top_builddir)/wslay/includes \
   -I$(srcdir)/brotli/dec \
   -I$(srcdir)/brotli/enc \
   -I$(srcdir)/cloexec \
   -I$(srcdir)/golombset \
   -I$(srcdir)/include \
   -I$(srcdir)/hiredis \
   -I$(srcdir)/klib \
   -I$(srcdir)/libgkc \
   -I$(srcdir)/libyrmcds \
   -I$(srcdir)/picohttpparser \
   -I$(srcdir)/picotest \
   -I$(srcdir)/yoml

libh2o_la_CFLAGS= $(h2o_includes)
libh2o_la_CXXFLAGS= $(h2o_includes)
libh2o_la_LDFLAGS= -lz -lstdc++

libh2o_la_SOURCES= \
    $(brotli_source) \
    $(cloexec_source) \
    $(hiredis_source) \
    $(klib_source) \
    $(libgkc_source) \
    $(libyrmcds_source) \
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
    handler/compress.c \
    handler/compress/brotli.cc \
    handler/compress/gzip.c \
    handler/configurator/compress.c \
    handler/configurator/errordoc.c \
    handler/configurator/fastcgi.c \
    handler/configurator/headers_util.c \
    handler/configurator/http2_debug_state.c \
    handler/configurator/status.c \
    handler/configurator/throttle_resp.c \
    handler/errordoc.c \
    handler/fastcgi.c \
    handler/headers_util.c \
    handler/http2_debug_state.c \
    handler/status.c \
    handler/status/durations.c \
    handler/status/events.c \
    handler/status/requests.c \
    handler/throttle_resp.c \
    common/cache.c \
    common/file.c \
    common/filecache.c \
    common/hostinfo.c \
    common/http1client.c \
    common/memcached.c \
    common/memory.c \
    common/multithread.c \
    common/redis.c \
    common/serverutil.c \
    common/socket/uv-binding.c.h \
    common/socket/evloop.c.h \
    common/socket/evloop/kqueue.c.h \
    common/socket/evloop/epoll.c.h \
    common/socket/evloop/poll.c.h \
    common/socket.c \
    common/string.c \
    common/socketpool.c \
    common/time.c \
    common/url.c \
    common/timeout.c \
    core/config.c \
    core/configurator.c \
    core/context.c \
    core/headers.c \
    core/token_table.h \
    core/logconf.c \
    core/proxy.c \
    core/request.c \
    core/token.c \
    core/util.c \
    websocket.c \
    http1.c \
    http2/cache_digests.c \
    http2/casper.c \
    http2/http2_debug_state.c \
    http2/frame.c \
    http2/hpack.c \
    http2/stream.c \
    http2/hpack_static_table.h \
    http2/scheduler.c \
    http2/hpack_huffman_table.h \
    http2/connection.c \
    tunnel.c
