dnl Copyright (C) 2004-2020 by Carnegie Mellon University.
dnl
dnl @OPENSOURCE_LICENSE_START@
dnl See license information in ../LICENSE.txt
dnl @OPENSOURCE_LICENSE_END@

dnl RCSIDENT("$SiLK: ax_check_libz.m4 ef14e54179be 2020-04-14 21:57:45Z mthomas $")

# ---------------------------------------------------------------------------
# AX_CHECK_LIBZ
#
#    Determine how to use the zlib (gzip) compression library
#
#    Substitutions: SK_ENABLE_ZLIB
#    Output defines: ENABLE_ZLIB

AC_DEFUN([AX_CHECK_LIBZ],[
    ENABLE_ZLIB=0

    AC_ARG_WITH([zlib],[AS_HELP_STRING([--with-zlib=ZLIB_DIR],
            [specify location of the zlib file compression library; find "zlib.h" in ZLIB_DIR/include/; find "libz.so" in ZLIB_DIR/lib/ [auto]])[]dnl
        ],[
            if test "x$withval" != "xyes"
            then
                zlib_dir="$withval"
                zlib_includes="$zlib_dir/include"
                zlib_libraries="$zlib_dir/lib"
            fi
    ])
    AC_ARG_WITH([zlib-includes],[AS_HELP_STRING([--with-zlib-includes=DIR],
            [find "zlib.h" in DIR/ (overrides ZLIB_DIR/include/)])[]dnl
        ],[
            if test "x$withval" = "xno"
            then
                zlib_dir=no
            elif test "x$withval" != "xyes"
            then
                zlib_includes="$withval"
            fi
    ])
    AC_ARG_WITH([zlib-libraries],[AS_HELP_STRING([--with-zlib-libraries=DIR],
            [find "libz.so" in DIR/ (overrides ZLIB_DIR/lib/)])[]dnl
        ],[
            if test "x$withval" = "xno"
            then
                zlib_dir=no
            elif test "x$withval" != "xyes"
            then
                zlib_libraries="$withval"
            fi
    ])

    if test "x$zlib_dir" != "xno"
    then
        # Cache current values
        sk_save_LDFLAGS="$LDFLAGS"
        sk_save_LIBS="$LIBS"
        sk_save_CFLAGS="$CFLAGS"
        sk_save_CPPFLAGS="$CPPFLAGS"

        if test "x$zlib_libraries" != "x"
        then
            ZLIB_LDFLAGS="-L$zlib_libraries"
            LDFLAGS="$ZLIB_LDFLAGS $sk_save_LDFLAGS"
        fi

        if test "x$zlib_includes" != "x"
        then
            ZLIB_CFLAGS="-I$zlib_includes"
            CPPFLAGS="$ZLIB_CFLAGS $sk_save_CPPFLAGS"
        fi

        AC_CHECK_LIB([z], [gzopen],
            [ENABLE_ZLIB=1 ; ZLIB_LDFLAGS="$ZLIB_LDFLAGS -lz"])

        if test "x$ENABLE_ZLIB" = "x1"
        then
            AC_CHECK_HEADER([zlib.h], , [
                AC_MSG_WARN([Found libz but not zlib.h.  Maybe you should install zlib-devel?])
                ENABLE_ZLIB=0])
        fi

        if test "x$ENABLE_ZLIB" = "x1"
        then
            # found zlib, now look for the compressBound() function
            AC_CHECK_LIB([z], [compressBound],
                AC_DEFINE([HAVE_COMPRESSBOUND], 1,
                    [Define to 1 if your zlib library provides compressBound().]))
        fi

        # Restore cached values
        LDFLAGS="$sk_save_LDFLAGS"
        LIBS="$sk_save_LIBS"
        CFLAGS="$sk_save_CFLAGS"
        CPPFLAGS="$sk_save_CPPFLAGS"
    fi

    if test "x$ENABLE_ZLIB" != "x1"
    then
        ZLIB_CFLAGS=
        ZLIB_LDFLAGS=
    fi

    AC_DEFINE_UNQUOTED([ENABLE_ZLIB], [$ENABLE_ZLIB],
        [Define to 1 build with support for zlib compression.  Define
         to 0 otherwise.  Requires the libz library and the <zlib.h>
         header file.])
    AC_SUBST([SK_ENABLE_ZLIB], [$ENABLE_ZLIB])
])# AX_CHECK_LIBZ

dnl Local Variables:
dnl mode:autoconf
dnl indent-tabs-mode:nil
dnl End:
