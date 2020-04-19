dnl Copyright (C) 2004-2020 by Carnegie Mellon University.
dnl
dnl @OPENSOURCE_LICENSE_START@
dnl See license information in ../LICENSE.txt
dnl @OPENSOURCE_LICENSE_END@

dnl RCSIDENT("$SiLK: ax_check_libadns.m4 ef14e54179be 2020-04-14 21:57:45Z mthomas $")

# ---------------------------------------------------------------------------
# AX_CHECK_LIBADNS
#
#    Try to find the ADNS (Asynchronous DNS) library.
#
#    Output variables: ADNS_CFLAGS ADNS_LDFLAGS
#    Output definition: HAVE_ADNS_H
#
AC_DEFUN([AX_CHECK_LIBADNS],[
    AC_SUBST(ADNS_CFLAGS)
    AC_SUBST(ADNS_LDFLAGS)

    AC_ARG_WITH([adns],[AS_HELP_STRING([--with-adns=ADNS_DIR],
            [specify location of the ADNS asynchronous DNS library; find "adns.h" in ADNS_DIR/include/; find "libadns.so" in ADNS_DIR/lib/ [auto]])[]dnl
        ],[
            if test "x$withval" != "xyes"
            then
                adns_dir="$withval"
                adns_includes="$adns_dir/include"
                adns_libraries="$adns_dir/lib"
            fi
    ])
    AC_ARG_WITH([adns-includes],[AS_HELP_STRING([--with-adns-includes=DIR],
            [find "adns.h" in DIR/ (overrides ADNS_DIR/include/)])[]dnl
        ],[
            if test "x$withval" = "xno"
            then
                adns_dir=no
            elif test "x$withval" != "xyes"
            then
                adns_includes="$withval"
            fi
    ])
    AC_ARG_WITH([adns-libraries],[AS_HELP_STRING([--with-adns-libraries=DIR],
            [find "libadns.so" in DIR/ (overrides ADNS_DIR/lib/)])[]dnl
        ],[
            if test "x$withval" = "xno"
            then
                adns_dir=no
            elif test "x$withval" != "xyes"
            then
                adns_libraries="$withval"
            fi
    ])

    ENABLE_ADNS=0
    if test "x$adns_dir" != "xno"
    then
        # Cache current values
        sk_save_LDFLAGS="$LDFLAGS"
        sk_save_LIBS="$LIBS"
        sk_save_CFLAGS="$CFLAGS"
        sk_save_CPPFLAGS="$CPPFLAGS"

        if test "x$adns_libraries" != "x"
        then
            ADNS_LDFLAGS="-L$adns_libraries"
            LDFLAGS="$ADNS_LDFLAGS $sk_save_LDFLAGS"
        fi

        if test "x$adns_includes" != "x"
        then
            ADNS_CFLAGS="-I$adns_includes"
            CPPFLAGS="$ADNS_CFLAGS $sk_save_CPPFLAGS"
        fi

        AC_CHECK_LIB([adns], [adns_init],
            [ENABLE_ADNS=1 ; ADNS_LDFLAGS="$ADNS_LDFLAGS -ladns"])

        if test "x$ENABLE_ADNS" = "x1"
        then
            AC_CHECK_HEADER([adns.h], , [
                AC_MSG_WARN([Found libadns but not adns.h.  Maybe you should install adns-devel?])
                ENABLE_ADNS=0])
        fi

        if test "x$ENABLE_ADNS" = "x1"
        then
            AC_MSG_CHECKING([usability of ADNS library and headers])
            LDFLAGS="$sk_save_LDFLAGS"
            LIBS="$ADNS_LDFLAGS $sk_save_LIBS"
            AC_LINK_IFELSE(
                [AC_LANG_PROGRAM([
#include <adns.h>
                    ],[
adns_state adns;
adns_query q;
int rv;

rv = adns_init(&adns, (adns_initflags)0, 0);
rv = adns_submit(adns, "255.255.255.255.in-addr.arpa", adns_r_ptr,
                 (adns_queryflags)(adns_qf_quoteok_cname|adns_qf_cname_loose),
                 NULL, &q);
                     ])],[
                AC_MSG_RESULT([yes])],[
                AC_MSG_RESULT([no])
                ENABLE_ADNS=0])
        fi

        # Restore cached values
        LDFLAGS="$sk_save_LDFLAGS"
        LIBS="$sk_save_LIBS"
        CFLAGS="$sk_save_CFLAGS"
        CPPFLAGS="$sk_save_CPPFLAGS"
    fi

    if test "x$ENABLE_ADNS" != "x1"
    then
        ADNS_LDFLAGS=
        ADNS_CFLAGS=
    else
        AC_DEFINE([HAVE_ADNS_H], 1,
            [Define to 1 include support for ADNS (asynchronous DNS).
             Requires the ADNS library and the <adns.h> header file.])
    fi
])# AX_CHECK_LIBADNS

dnl Local Variables:
dnl mode:autoconf
dnl indent-tabs-mode:nil
dnl End:
