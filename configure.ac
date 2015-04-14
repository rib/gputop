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

AM_INIT_AUTOMAKE([1.14 foreign -Wno-portability no-define no-dist-gzip
                  dist-bzip2])
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
AC_PROG_CPP
AM_PROG_CC_C_O
AC_ISC_POSIX
AC_C_CONST


dnl ================================================================
dnl Libtool stuff.
dnl ================================================================
AC_PROG_LIBTOOL
dnl LIBTOOL="$LIBTOOL --preserve-dup-deps"


dnl ================================================================
dnl I18n stuff.
dnl ================================================================
AM_GNU_GETTEXT_VERSION([0.17])
AM_GNU_GETTEXT([external])

GETTEXT_PACKAGE="gputop"
AC_SUBST(GETTEXT_PACKAGE)
AC_DEFINE_UNQUOTED(GETTEXT_PACKAGE,
                   "$GETTEXT_PACKAGE",
                   [The prefix for our gettext translation domains.])
AS_ALL_LINGUAS


dnl ================================================================
dnl Check for dependency packages.
dnl ================================================================

AX_PYTHON_MODULE(lxml, [error])

AC_CHECK_LIB(ncursesw, [subwin],
	     [],
	     [AC_MSG_ERROR([gputop requires ncurses])])

PKG_CHECK_MODULES(GPUTOP_DEP, [libuv libdrm libdrm_intel])

dnl gputop-system is a dummy "test application" that needs to be
dnl linked against GL...
PKG_CHECK_MODULES(GL_DEP, [gl x11])

dnl ================================================================
dnl Misc program dependencies.
dnl ================================================================
AC_PROG_INSTALL


dnl ================================================================
dnl What needs to be substituted in other files
dnl ================================================================
AC_SUBST(GPUTOP_EXTRA_CPPFLAGS)
AC_SUBST(GPUTOP_EXTRA_CFLAGS)
AC_SUBST(GPUTOP_EXTRA_LDFLAGS)
AC_SUBST(GPUTOP_DEP_CFLAGS)
AC_SUBST(GPUTOP_DEP_LIBS)

dnl ================================================================
dnl Dah Da!
dnl ================================================================
AC_OUTPUT(
Makefile
gputop/Makefile
gputop/registry/Makefile
po/Makefile.in
)
