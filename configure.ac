dnl Process this file with autoconf to produce a configure script.

# Save this value here, since automake will set cflags later and we
# want to know if the user specified custom cflags or not.

cflags_set=${CFLAGS+set}

AC_PREREQ(2.59)

AC_INIT(gputop, [0.1.0])
AC_CONFIG_SRCDIR(gputop-server/gputop-main.c)
AC_CONFIG_AUX_DIR([build])
AC_CONFIG_MACRO_DIR([build/autotools])
AC_CONFIG_HEADERS(config.h)

AM_INIT_AUTOMAKE([1.11 foreign -Wno-portability no-define no-dist-gzip
                  dist-bzip2 subdir-objects])
AM_SILENT_RULES([yes])

AC_USE_SYSTEM_EXTENSIONS()


dnl ================================================================
dnl Options
dnl ================================================================

AC_ARG_ENABLE(
  [debug],
  [AC_HELP_STRING([--enable-debug=@<:@no/yes@:>@], [Control debugging level @<:@default=no@:>@])],
  [enable_debug="$enableval"],
  [enable_debug="no"]
)
AS_CASE(
  [$enable_debug],
  [yes],
  [
    test "$cflags_set" = set || CFLAGS="-g -O0 $CFLAGS"
    GPUTOP_EXTRA_CFLAGS="$GPUTOP_EXTRA_CFLAGS -DGPUTOP_ENABLE_DEBUG"
  ],
  [no],
  [],
  [AC_MSG_ERROR([Unknown argument for --enable-debug])]
)

AC_ARG_ENABLE(
  [maintainer-flags],
  [AC_HELP_STRING([--enable-maintainer-flags=@<:@no/yes/error@:>@], [Use strict compiler flags @<:@default=yes@:>@])],
  [enable_maintainer_flags="$enableval"],
  [enable_maintainer_flags="yes"]
)

MAINTAINER_COMPILER_FLAGS="-Wall -Wcast-align -Wuninitialized
                           -Wno-strict-aliasing -Wempty-body -Wformat
                           -Wformat-security -Winit-self
                           -Wvla -Wpointer-arith"

AS_CASE(
  [$enable_maintainer_flags],
  [yes],
  [
    AS_COMPILER_FLAGS([MAINTAINER_CFLAGS], [$MAINTAINER_COMPILER_FLAGS])
  ],
  [no],
  [],
  [error],
  [
    MAINTAINER_COMPILER_FLAGS="$MAINTAINER_COMPILER_FLAGS -Werror"
    AS_COMPILER_FLAGS([MAINTAINER_CFLAGS], [$MAINTAINER_COMPILER_FLAGS])
  ],
  [*],
  [AC_MSG_ERROR([Invalid option for --enable-maintainer-flags={yes,no,error}])]
)

# strip leading spaces
GPUTOP_EXTRA_CFLAGS="$GPUTOP_EXTRA_CFLAGS ${MAINTAINER_CFLAGS/#  }"


dnl ================================================================
dnl Toolchain stuff.
dnl ================================================================

AC_PROG_CC
AC_PROG_CXX

AX_PTHREAD

AC_PROG_LIBTOOL

AC_CHECK_PROGS([PYTHON2], [python2 python])
AX_PYTHON_MODULE(lxml, [error])
AX_PYTHON_MODULE(mako, [error])

AC_PROG_INSTALL
AC_CHECK_PROGS([EMCC], [emcc])
AC_CHECK_PROGS([NODE], [node])
AC_CHECK_PROGS([NODE_GYP], [node-gyp])
AC_CHECK_PROGS([NPM], [npm])

AS_IF([test -n "$NODE"], [
   AC_MSG_CHECKING([node version])
   NODE_MAJOR=$($NODE --version|cut -d'.' -f1|cut -dv -f2)
   NODE_MINOR=$($NODE --version|cut -d'.' -f2)
   NODE_MICRO=$($NODE --version|cut -d'.' -f3)
   AC_MSG_RESULT([$NODE_MAJOR.$NODE_MINOR.$NODE_MICRO])
])

AS_IF([test -n "$NODE_GYP"], [
   AC_MSG_CHECKING([node-gyp version])
   NODE_GYP_MAJOR=$($NODE_GYP --version|cut -d'.' -f1|cut -dv -f2)
   NODE_GYP_MINOR=$($NODE_GYP --version|cut -d'.' -f2)
   NODE_GYP_MICRO=$($NODE_GYP --version|cut -d'.' -f3)
   AC_MSG_RESULT([$NODE_GYP_MAJOR.$NODE_GYP_MINOR.$NODE_GYP_MICRO])
])


dnl ================================================================
dnl Required third-party dependencies
dnl ================================================================
PKG_CHECK_MODULES(CRYTPO_DEP, [libcrypto])
PKG_CHECK_MODULES(SSL_DEP, [libssl >= 1.0 libssl < 1.1])

dnl ================================================================
dnl Optional features
dnl ================================================================

PKG_CHECK_EXISTS([gl x11], [have_glx=yes], [have_glx=no])
AC_ARG_ENABLE(
  [gl],
  [AC_HELP_STRING([--enable-gl=@<:@no/yes@:>@],
                  [Enable monitoring GL applications @<:@default=auto@:>@])],
  [enable_gl="$enableval"],
  [enable_gl="$have_glx"]
)
AS_IF([test "$enable_gl" = "yes"],
      [
	PKG_CHECK_MODULES(GLX_DEP, [gl x11])
	AC_DEFINE([SUPPORT_GL], [1], [Support intercepting GL applications])
      ])
AM_CONDITIONAL(SUPPORT_GL, [test "$enable_gl" = "yes" ])

AC_ARG_ENABLE(
  [web-ui],
  [AC_HELP_STRING([--enable-web-ui=@<:@no/yes@:>@],
                  [Build web ui @<:@default=auto@:>@])],
  [enable_web_ui="$enableval"],
  [enable_web_ui="no"]
)
AS_IF([test "$enable_web_ui" = "yes"],
      [
        AS_IF([test -z "$EMCC"], [ AC_MSG_ERROR([Emscripten emcc command missing, required to build web UI]) ])
        AC_DEFINE([ENABLE_WEBUI], [1], [Build web UI])
      ])
AM_CONDITIONAL(ENABLE_WEBUI, [test "$enable_web_ui" = "yes"])

MIN_NODE_MAJOR=6
MIN_NODE_GYP_MAJOR=3
AS_IF([test -n "$NODE" -a -n "$NODE_MAJOR" -a $NODE_MAJOR -ge $MIN_NODE_MAJOR -a \
       -n "NODE_GYP" -a -n "$NODE_GYP_MAJOR" -a $NODE_GYP_MAJOR -ge $MIN_NODE_GYP_MAJOR -a \
       -n "$NPM"],
      [ node_looks_good="yes" ],
      [ node_looks_good="no" ])

AC_ARG_ENABLE(
  [node-clients],
  [AC_HELP_STRING([--enable-node-clients=@<:@no/yes@:>@],
                  [Build node.js clients @<:@default=auto@:>@])],
  [enable_node_clients="$enableval"],
  [enable_node_clients="$node_looks_good"]
)
AM_CONDITIONAL(ENABLE_NODE_CLIENTS, [test "$enable_node_clients" = "yes"])
AS_IF([test "$enable_node_clients" = "yes" -a "$node_looks_good" = "no"],
      [
        NODE_WARNING="(WARNING: node toolchain checks didn't pass: node ($NODE_MAJOR.$NODE_MINOR.$NODE_MICRO) >=$MIN_NODE_MAJOR, node-gyp ($NODE_GYP_MAJOR.$NODE_GYP_MINOR.$NODE_GYP_MICRO) >=$MIN_NODE_GYP_MAJOR and npm exists)"
      ])

AM_CONDITIONAL(ENABLE_CLIENTS, [test "$enable_web_ui" = "yes" -o "$enable_node_clients" = "yes"])


dnl ================================================================
dnl Substitutions
dnl ================================================================
AC_SUBST(GPUTOP_EXTRA_CFLAGS)
AC_SUBST(GPUTOP_EXTRA_LDFLAGS)

dnl ================================================================
dnl Dah Da!
dnl ================================================================
AC_OUTPUT(
Makefile
libuv/Makefile
h2o/Makefile
protobuf/Makefile
protobuf-c/Makefile
protoc-c/Makefile
mesa/Makefile
gputop-data/Makefile
gputop-server/Makefile
gputop-server/registry/Makefile
gputop-client/Makefile
gputop-client-c/Makefile
gputop-webui/Makefile
gputop-csv/Makefile
gputop-tracepoints/Makefile
)

echo ""
echo "GPU Top configured:"
echo ""
echo "   Optional Features: ${FEATURES}"
echo "        GL Intercept: ${enable_gl}"
echo "              Web UI: ${enable_web_ui}"
echo "               Tools: ${enable_node_clients} $NODE_WARNING"
echo ""
echo "   Compiler options:"
echo "        Debug: ${enable_debug}"
echo "        Compiler flags: ${CFLAGS} ${GPUTOP_EXTRA_CFLAGS}"
echo "        Linker flags: ${LDFLAGS} ${GPUTOP_EXTRA_LDFLAGS}"
echo ""
echo "TO BUILD:"
echo "$ make install"
echo ""
echo "TO RUN SERVER:"
echo "$ gputop"
echo ""
echo "Note: Also see 'gputop --help' and try 'gputop --dry-run'"
echo ""
AS_IF([test "$enable_web_ui" = "yes"], [
echo "WEB UI:"
echo "By default you can browse to http://localhost:7890 with gputop running"
echo ""
echo "Note: The UI hosted at gputop.github.io can also be used by browsing to"
echo "http://gputop.github.io?target=localhost with gputop running"
echo ""
echo ""
],[
echo "WEB UI:"
echo "Since the web UI isn't being built currently you can use the UI hosted on"
echo "github by browsing to http://gputop.github.io?target=localhost with gputop"
echo "running"
echo ""

])
