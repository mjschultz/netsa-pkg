dnl Copyright (C) 2004-2020 by Carnegie Mellon University.
dnl
dnl @OPENSOURCE_LICENSE_START@
dnl See license information in ../LICENSE.txt
dnl @OPENSOURCE_LICENSE_END@

dnl RCSIDENT("$SiLK: ax_check_libsnappy.m4 ef14e54179be 2020-04-14 21:57:45Z mthomas $")

# ---------------------------------------------------------------------------
# AX_CHECK_LIBSNAPPY
#
#    Determine how to use the snappy compression library
#
#    Substitutions: SK_ENABLE_SNAPPY
#    Output defines: ENABLE_SNAPPY
#
AC_DEFUN([AX_CHECK_LIBSNAPPY],[
    ENABLE_SNAPPY=0

    AC_ARG_WITH([snappy],[AS_HELP_STRING([--with-snappy=SNAPPY_DIR],
            [specify location of the snappy file compression library; find "snappy-c.h" in SNAPPY_DIR/include/; find "libsnappy.so" in SNAPPY_DIR/lib/ [auto]])[]dnl
        ],[
            if test "x$withval" != "xyes"
            then
                snappy_dir="$withval"
                snappy_includes="$snappy_dir/include"
                snappy_libraries="$snappy_dir/lib"
            fi
    ])
    AC_ARG_WITH([snappy-includes],[AS_HELP_STRING([--with-snappy-includes=DIR],
            [find "snappy-c.h" in DIR/ (overrides SNAPPY_DIR/include/)])[]dnl
        ],[
            if test "x$withval" = "xno"
            then
                snappy_dir=no
            elif test "x$withval" != "xyes"
            then
                snappy_includes="$withval"
            fi
    ])
    AC_ARG_WITH([snappy-libraries],[AS_HELP_STRING([--with-snappy-libraries=DIR],
            [find "libsnappy.so" in DIR/ (overrides SNAPPY_DIR/lib/)])[]dnl
        ],[
            if test "x$withval" = "xno"
            then
                snappy_dir=no
            elif test "x$withval" != "xyes"
            then
                snappy_libraries="$withval"
            fi
    ])

    if test "x$snappy_dir" != "xno"
    then
        # Cache current values
        sk_save_LDFLAGS="$LDFLAGS"
        sk_save_LIBS="$LIBS"
        sk_save_CFLAGS="$CFLAGS"
        sk_save_CPPFLAGS="$CPPFLAGS"

        if test "x$snappy_libraries" != "x"
        then
            SNAPPY_LDFLAGS="-L$snappy_libraries"
            LDFLAGS="$SNAPPY_LDFLAGS $sk_save_LDFLAGS"
        fi

        if test "x$snappy_includes" != "x"
        then
            SNAPPY_CFLAGS="-I$snappy_includes"
            CPPFLAGS="$SNAPPY_CFLAGS $sk_save_CPPFLAGS"
        fi

        AC_CHECK_LIB([snappy], [snappy_compress],
            [ENABLE_SNAPPY=1 ; SNAPPY_LDFLAGS="$SNAPPY_LDFLAGS -lsnappy"])

        if test "x$ENABLE_SNAPPY" = "x1"
        then
            AC_CHECK_HEADER([snappy-c.h], , [
                AC_MSG_WARN([Found snappy but not snappy-c.h.  Maybe you should install snappy-devel?])
                ENABLE_SNAPPY=0])
        fi

        # Restore cached values
        LDFLAGS="$sk_save_LDFLAGS"
        LIBS="$sk_save_LIBS"
        CFLAGS="$sk_save_CFLAGS"
        CPPFLAGS="$sk_save_CPPFLAGS"
    fi

    if test "x$ENABLE_SNAPPY" != "x1"
    then
        SNAPPY_CFLAGS=
        SNAPPY_LDFLAGS=
    fi

    AC_DEFINE_UNQUOTED([ENABLE_SNAPPY], [$ENABLE_SNAPPY],
        [Define to 1 build with support for snappy compression.  Define
         to 0 otherwise.  Requires the libsnappy library and the
         snappy-c.h header file.])
    AC_SUBST([SK_ENABLE_SNAPPY], [$ENABLE_SNAPPY])
])# AX_CHECK_SNAPPY

dnl Local Variables:
dnl mode:autoconf
dnl indent-tabs-mode:nil
dnl End:
