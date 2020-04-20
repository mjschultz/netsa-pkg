dnl Copyright (C) 2004-2020 by Carnegie Mellon University.
dnl
dnl @OPENSOURCE_LICENSE_START@
dnl See license information in ../LICENSE.txt
dnl @OPENSOURCE_LICENSE_END@

dnl RCSIDENT("$SiLK: ax_check_libpcap.m4 ef14e54179be 2020-04-14 21:57:45Z mthomas $")


# ---------------------------------------------------------------------------
# AX_CHECK_LIBPCAP
#
#    Determine how to use pcap.  Output variable: PCAP_LDFLAGS
#    Output definition: HAVE_PCAP_H, HAVE_PCAPPCAP_H
#
AC_DEFUN([AX_CHECK_LIBPCAP],[
    AC_SUBST(PCAP_LDFLAGS)

    AC_ARG_WITH([pcap],[AS_HELP_STRING([--with-pcap=PCAP_DIR],
            [specify location of the PCAP packet capture library; find "pcap.h" in PCAP_DIR/include/; find "libpcap.so" in PCAP_DIR/lib/ [auto]])[]dnl
        ],[
            if test "x$withval" != "xyes"
            then
                pcap_dir="$withval"
                pcap_includes="$pcap_dir/include"
                pcap_libraries="$pcap_dir/lib"
            fi
    ])
    AC_ARG_WITH([pcap-includes],[AS_HELP_STRING([--with-pcap-includes=DIR],
            [find "pcap.h" in DIR/ (overrides PCAP_DIR/include/)])[]dnl
        ],[
            if test "x$withval" = "xno"
            then
                pcap_dir=no
            elif test "x$withval" != "xyes"
            then
                pcap_includes="$withval"
            fi
    ])
    AC_ARG_WITH([pcap-libraries],[AS_HELP_STRING([--with-pcap-libraries=DIR],
            [find "libpcap.so" in DIR/ (overrides PCAP_DIR/lib/)])[]dnl
        ],[
            if test "x$withval" = "xno"
            then
                pcap_dir=no
            elif test "x$withval" != "xyes"
            then
                pcap_libraries="$withval"
            fi
    ])

    ENABLE_PCAP=0
    if test "x$pcap_dir" != "xno"
    then
        # Cache current values
        sk_save_LDFLAGS="$LDFLAGS"
        sk_save_LIBS="$LIBS"
        sk_save_CFLAGS="$CFLAGS"
        sk_save_CPPFLAGS="$CPPFLAGS"

        if test "x$pcap_libraries" != "x"
        then
            PCAP_LDFLAGS="-L$pcap_libraries"
            LDFLAGS="$PCAP_LDFLAGS $sk_save_LDFLAGS"
        fi

        if test "x$pcap_includes" != "x"
        then
            PCAP_CFLAGS="-I$pcap_includes"
            CPPFLAGS="$PCAP_CFLAGS $sk_save_CPPFLAGS"
        fi

        # look for -lpcap or -lwpcap
        AC_SEARCH_LIBS([pcap_open_dead], [pcap wpcap], [ENABLE_PCAP=1])
        if test "x$ENABLE_PCAP" = "x1"
        then
            case "(X$ac_cv_search_pcap_open_dead" in *X-l*)
                PCAP_LDFLAGS="$PCAP_LDFLAGS $ac_cv_search_pcap_open_dead" ;;
            esac
        fi

        if test "x$ENABLE_PCAP" != "x1"
        then
            AC_MSG_NOTICE([(${PACKAGE}) Not building rwp* tools due to missing pcap library])
        else
            # look for header as pcap.h and pcap/pcap.h
            ENABLE_PCAP=0
            AC_CHECK_HEADERS([pcap.h pcap/pcap.h], [ENABLE_PCAP=1 ; break])

            if test "x$ENABLE_PCAP" != "x1"
            then
                AC_MSG_NOTICE([(${PACKAGE}) Not building rwp* tools: Found libpcap but not pcap.h. Maybe you need to install pcap-devel?])
            fi
        fi

        if test "x$ENABLE_PCAP" = "x1"
        then
            AC_MSG_CHECKING([usability of PCAP library and headers])
            LDFLAGS="$sk_save_LDFLAGS"
            LIBS="$PCAP_LDFLAGS $sk_save_LIBS"
            AC_LINK_IFELSE(
                [AC_LANG_PROGRAM([
#ifdef HAVE_PCAP_PCAP_H
#include <pcap/pcap.h>
#else
#include <pcap.h>
#endif
                    ],[
pcap_t *pc;
u_char *pkt;
struct pcap_pkthdr hdr;
pcap_dumper_t *dump;

pc = pcap_open_dead(0, 0);
dump = pcap_dump_open(pc, "/dev/null");
pkt = (u_char*)(pcap_next(pc, &hdr));
pcap_dump((u_char*)dump, &hdr, pkt);
pcap_dump_flush(dump);
                     ])],[
                AC_MSG_RESULT([yes])],[
                AC_MSG_RESULT([no])
                ENABLE_PCAP=0])
        fi

        if test "x$ENABLE_PCAP" != "x1"
        then
            AC_MSG_NOTICE([(${PACKAGE}) Not building rwp* tools due to error using pcap library and header. Details in config.log])
        fi

        # Restore cached values
        LDFLAGS="$sk_save_LDFLAGS"
        LIBS="$sk_save_LIBS"
        CFLAGS="$sk_save_CFLAGS"
        CPPFLAGS="$sk_save_CPPFLAGS"
    fi

    if test "x$ENABLE_PCAP" != "x1"
    then
        PCAP_LDFLAGS=
        PCAP_CFLAGS=
    else
        AC_DEFINE([HAVE_PCAP_H], 1,
            [Define to 1 if you have the <pcap.h> header file.])
    fi

    AM_CONDITIONAL(HAVE_PCAP, [test "x$ENABLE_PCAP" = x1])

])# AX_CHECK_LIBPCAP

dnl Local Variables:
dnl mode:autoconf
dnl indent-tabs-mode:nil
dnl End:
