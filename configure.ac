dnl Process this file with autoconf to produce a configure script.

AC_PREREQ(2.59)

dnl ================================================================
dnl Source code release status
dnl ================================================================
m4_define([gputop_release_status], [git])


dnl ================================================================
dnl Required versions for dependencies
dnl ================================================================

AC_INIT(gputop, [0.1.0])
AC_CONFIG_SRCDIR(gputop/gputop-main.c)
AC_CONFIG_AUX_DIR([build])
AC_CONFIG_MACRO_DIR([build/autotools])
AC_CONFIG_HEADERS(config.h)

# Save this value here, since automake will set cflags later and we
# want to know if the user specified custom cflags or not.
cflags_set=${CFLAGS+set}

AM_INIT_AUTOMAKE([1.11 foreign -Wno-portability no-define no-dist-gzip
                  dist-bzip2 subdir-objects])
AM_SILENT_RULES([yes])

AC_USE_SYSTEM_EXTENSIONS()

dnl ================================================================
dnl Handle extra configure options
dnl ================================================================

m4_define([default], [m4_if(gputop_release_status, [git], [yes], [no])])
AC_ARG_ENABLE(
  [debug],
  [AC_HELP_STRING([--enable-debug=@<:@no/yes@:>@], [Control debugging level @<:@default=]default[@:>@])],
  [],
  enable_debug=default
)
AS_CASE(
  [$enable_debug],
  [yes],
  [
    test "$cflags_set" = set || CFLAGS="$CFLAGS -g -O0"
    GPUTOP_EXTRA_CFLAGS="$GPUTOP_EXTRA_CFLAGS -DGPUTOP_ENABLE_DEBUG"
  ],
  [no],
  [
  ],
  [AC_MSG_ERROR([Unknown argument for --enable-debug])]
)

# use strict compiler flags only when building from git; the rules for
# distcheck will take care of turning this on when making a release
m4_define([default], [m4_if(gputop_release_status, [git], [yes], [no])])
AC_ARG_ENABLE(
  [maintainer-flags],
  [AC_HELP_STRING([--enable-maintainer-flags=@<:@no/yes/error@:>@], [Use strict compiler flags @<:@default=]default[@:>@])],
  [],
  enable_maintainer_flags=default
)

MAINTAINER_COMPILER_FLAGS="-Wall -Wcast-align -Wuninitialized
                           -Wno-strict-aliasing -Wempty-body -Wformat
                           -Wformat-security -Winit-self
                           -Wdeclaration-after-statement -Wvla
                           -Wpointer-arith"

AS_CASE(
  [$enable_maintainer_flags],
  [yes],
  [
    AS_COMPILER_FLAGS([MAINTAINER_CFLAGS], [$MAINTAINER_COMPILER_FLAGS])
  ],
  [no],
  [
  ],
  [error],
  [
    MAINTAINER_COMPILER_FLAGS="$MAINTAINER_COMPILER_FLAGS -Werror"
    AS_COMPILER_FLAGS([MAINTAINER_CFLAGS], [$MAINTAINER_COMPILER_FLAGS])
  ],
  [*],
  [AC_MSG_ERROR([Invalid option for --enable-maintainer-flags])]
)

# strip leading spaces
GPUTOP_EXTRA_CFLAGS="$GPUTOP_EXTRA_CFLAGS ${MAINTAINER_CFLAGS/#  }"


dnl ================================================================
dnl Compiler stuff.
dnl ================================================================
AC_PROG_CC
AC_PROG_CXX

AX_PTHREAD

dnl ================================================================
dnl Libtool stuff.
dnl ================================================================
AC_PROG_LIBTOOL
dnl LIBTOOL="$LIBTOOL --preserve-dup-deps"


dnl ================================================================
dnl Check for dependency packages.
dnl ================================================================

AX_PYTHON_MODULE(lxml, [error])

PKG_CHECK_MODULES(NCURSES_DEP, [ncursesw])
PKG_CHECK_MODULES(PROTOBUF_DEP, [protobuf])

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

dnl ================================================================
dnl Misc program dependencies.
dnl ================================================================
AC_PROG_INSTALL
AC_CHECK_PROGS([PYTHON2], [python2 python])
AC_CHECK_PROGS([EMCC], [emcc])

have_emcc=no
AS_IF([test -n "$EMCC"],
      [ have_emcc="yes" ])
AC_ARG_ENABLE(
  [webui],
  [AC_HELP_STRING([--enable-webui=@<:@no/yes@:>@],
                  [Enable Web UI @<:@default=auto@:>@])],
  [enable_webui="$enableval"],
  [enable_webui="$have_emcc"]
)
AS_IF([test "$enable_webui" = "yes"],
      [
        AC_DEFINE([SUPPORT_WEBUI], [1], [Support web based user interface])
      ])
AM_CONDITIONAL(SUPPORT_WEBUI, [test "$enable_webui" = "yes"])

dnl ================================================================
dnl What needs to be substituted in other files
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
protobuf-c/Makefile
protoc-c/Makefile
gputop/Makefile
gputop/registry/Makefile
gputop/remote/Makefile
)

echo ""
echo "GPU Top configured:"
echo ""
echo " • Optional Features: ${FEATURES}"
echo "        GL Intercept: ${enable_gl}"
echo "              Web UI: ${enable_webui}"
echo ""

echo " • Compiler options:"
echo "        Debug: ${enable_debug}"
echo "        Compiler flags: ${CFLAGS} ${GPUTOP_EXTRA_CFLAGS}"
echo "        Linker flags: ${LDFLAGS} ${GPUTOP_EXTRA_LDFLAGS}"
echo ""
