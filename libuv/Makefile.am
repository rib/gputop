# Copyright (c) 2013, Ben Noordhuis <info@bnoordhuis.nl>
#
# Permission to use, copy, modify, and/or distribute this software for any
# purpose with or without fee is hereby granted, provided that the above
# copyright notice and this permission notice appear in all copies.
#
# THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
# WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
# MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
# ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
# WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
# ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
# OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

AM_CPPFLAGS = \
	-I$(top_srcdir)/libuv/include \
	-I$(top_srcdir)/libuv/src \
	$(GPUTOP_EXTRA_CFLAGS) \
	-include $(top_builddir)/config.h

noinst_LTLIBRARIES = libuv.la
libuv_la_SOURCES = \
	src/fs-poll.c \
	src/heap-inl.h \
	src/inet.c \
	src/queue.h \
	src/threadpool.c \
	src/uv-common.c \
	src/uv-common.h \
	src/version.c

AM_CPPFLAGS += -I$(top_srcdir)/libuv/src/unix
libuv_la_SOURCES += \
	src/unix/async.c \
	src/unix/atomic-ops.h \
	src/unix/core.c \
	src/unix/dl.c \
	src/unix/fs.c \
	src/unix/getaddrinfo.c \
	src/unix/getnameinfo.c \
	src/unix/internal.h \
	src/unix/loop-watcher.c \
	src/unix/loop.c \
	src/unix/pipe.c \
	src/unix/poll.c \
	src/unix/process.c \
	src/unix/signal.c \
	src/unix/spinlock.h \
	src/unix/stream.c \
	src/unix/tcp.c \
	src/unix/thread.c \
	src/unix/timer.c \
	src/unix/tty.c \
	src/unix/udp.c

libuv_la_SOURCES += \
	src/unix/linux-core.c \
	src/unix/linux-inotify.c \
	src/unix/linux-syscalls.c \
	src/unix/linux-syscalls.h \
	src/unix/proctitle.c

libuv_la_LIBADD = $(CRYPTO_DEP_LIBS) $(SSL_DEP_LIBS)

EXTRA_DIST = \
	LICENSE
