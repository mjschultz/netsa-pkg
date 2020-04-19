dnl Copyright (C) 2004-2020 by Carnegie Mellon University.
dnl
dnl @OPENSOURCE_LICENSE_START@
dnl See license information in ../LICENSE.txt
dnl @OPENSOURCE_LICENSE_END@

dnl RCSIDENT("$SiLK: ax_check_libcares.m4 ef14e54179be 2020-04-14 21:57:45Z mthomas $")

# ---------------------------------------------------------------------------
# AX_CHECK_LIBCARES
#
#    A bit of confusion here: The package we need is c-ares.  The
#    header is named <ares.h>, but the library is libcares.
#
#    Try to find the CARES (Asynchronous DNS) library.
#
#    Output variables: CARES_CFLAGS CARES_LDFLAGS
#    Output definition: HAVE_CARES_H
#
AC_DEFUN([AX_CHECK_LIBCARES],[
    AC_SUBST(CARES_CFLAGS)
    AC_SUBST(CARES_LDFLAGS)

    AC_ARG_WITH([c-ares],[AS_HELP_STRING([--with-c-ares=CARES_DIR],
            [specify location of the c-ares asynchronous DNS library; find "ares.h" in CARES_DIR/include/; find "libcares.so" in CARES_DIR/lib/ [auto]])[]dnl
        ],[
            if test "x$withval" != "xyes"
            then
                cares_dir="$withval"
                cares_includes="$cares_dir/include"
                cares_libraries="$cares_dir/lib"
            fi
    ])
    AC_ARG_WITH([c-ares-includes],[AS_HELP_STRING([--with-c-ares-includes=DIR],
            [find "ares.h" in DIR/ (overrides CARES_DIR/include/)])[]dnl
        ],[
            if test "x$withval" = "xno"
            then
                cares_dir=no
            elif test "x$withval" != "xyes"
            then
                cares_includes="$withval"
            fi
    ])
    AC_ARG_WITH([c-ares-libraries],[AS_HELP_STRING([--with-c-ares-libraries=DIR],
            [find "libcares.so" in DIR/ (overrides CARES_DIR/lib/)])[]dnl
        ],[
            if test "x$withval" = "xno"
            then
                cares_dir=no
            elif test "x$withval" != "xyes"
            then
                cares_libraries="$withval"
            fi
    ])

    ENABLE_CARES=0
    if test "x$cares_dir" != "xno"
    then
        # Cache current values
        sk_save_LDFLAGS="$LDFLAGS"
        sk_save_LIBS="$LIBS"
        sk_save_CFLAGS="$CFLAGS"
        sk_save_CPPFLAGS="$CPPFLAGS"

        if test "x$cares_libraries" != "x"
        then
            CARES_LDFLAGS="-L$cares_libraries"
            LDFLAGS="$CARES_LDFLAGS $sk_save_LDFLAGS"
        fi

        if test "x$cares_includes" != "x"
        then
            CARES_CFLAGS="-I$cares_includes"
            CPPFLAGS="$CARES_CFLAGS $sk_save_CPPFLAGS"
        fi

        AC_CHECK_LIB([cares], [ares_init],
            [ENABLE_CARES=1 ; CARES_LDFLAGS="$CARES_LDFLAGS -lcares"])

        if test "x$ENABLE_CARES" = "x1"
        then
            AC_CHECK_HEADER([ares.h], , [
                AC_MSG_WARN([Found libcares but not ares.h.  Maybe you should install c-ares-devel?])
                ENABLE_CARES=0])
        fi

        if test "x$ENABLE_CARES" = "x1"
        then
            AC_MSG_CHECKING([usability of C-ARES library and headers])
            LDFLAGS="$sk_save_LDFLAGS"
            LIBS="$CARES_LDFLAGS $sk_save_LIBS"
            AC_LINK_IFELSE(
                [AC_LANG_PROGRAM([
#include <stdio.h>
#if HAVE_STDLIB_H
#include <stdlib.h>
#endif
#if HAVE_STRING_H
#include <string.h>
#endif
#include <ares.h>
                    ],[
ares_channel ares;
int rv;

rv = ares_init(&ares);
rv = ares_fds(ares, NULL, NULL);
ares_process(ares, NULL, NULL);
ares_cancel(ares);
                     ])],[
                AC_MSG_RESULT([yes])],[
                AC_MSG_RESULT([no])
                ENABLE_CARES=0])
        fi

        # Restore cached values
        LDFLAGS="$sk_save_LDFLAGS"
        LIBS="$sk_save_LIBS"
        CFLAGS="$sk_save_CFLAGS"
        CPPFLAGS="$sk_save_CPPFLAGS"
    fi

    if test "x$ENABLE_CARES" != "x1"
    then
        CARES_LDFLAGS=
        CARES_CFLAGS=
    else
        AC_DEFINE([HAVE_CARES_H], 1,
            [Define to 1 include support for C-ARES (asynchronous DNS).
             Requires the CARES library and the <ares.h> header file.])
    fi
])# AX_CHECK_LIBCARES

dnl Local Variables:
dnl mode:autoconf
dnl indent-tabs-mode:nil
dnl End:
