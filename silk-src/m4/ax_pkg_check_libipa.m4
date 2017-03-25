dnl Copyright (C) 2004-2017 by Carnegie Mellon University.
dnl
dnl @OPENSOURCE_LICENSE_START@
dnl See license information in ../LICENSE.txt
dnl @OPENSOURCE_LICENSE_END@

dnl RCSIDENT("$SiLK: ax_pkg_check_libipa.m4 275df62a2e41 2017-01-05 17:30:40Z mthomas $")


# ---------------------------------------------------------------------------
# AX_PKG_CHECK_LIBIPA
#
#    Determine how to use IPA
#
#    Output variables: LIBIPA_CFLAGS, LIBIPA_LDFLAGS
#    Output definitions: ENABLE_IPA
#
AC_DEFUN([AX_PKG_CHECK_LIBIPA],[
    AC_SUBST([LIBIPA_CFLAGS])
    AC_SUBST([LIBIPA_LDFLAGS])

    # Either one or two arguments; first is minimum version required;
    # if a second is present, it represents a version that is "too
    # new"
    libipa_required_version="$1"
    if test "x$2" = "x"
    then
        version_check="libipa >= $1"
        report_version="libipa.pc >= $1"
    else
        version_check="libipa >= $1 libipa < $2"
        report_version="libipa.pc >= $1, libipa.pc < $2"
    fi

    ENABLE_IPA=0

    # The configure switch
    sk_pkg_config=""
    AC_ARG_WITH([libipa],[AS_HELP_STRING([--with-libipa=DIR],
            [specify location of the IPA IP-address annotation package; find "libipa.pc" in the directory DIR/ (i.e., prepend DIR to PKG_CONFIG_PATH).  The last component of DIR is likely "pkgconfig" [auto]])[]dnl
        ],[
            if test "x${withval}" != "xyes"
            then
                sk_pkg_config="${withval}"
            fi
    ])

    if test "x${sk_pkg_config}" = "xno"
    then
        AC_MSG_NOTICE([(${PACKAGE}) Building without IPA support at user request])
    else
        # prepend any argument to PKG_CONFIG_PATH
        if test "x${sk_pkg_config}" != "x"
        then
            sk_save_PKG_CONFIG_PATH="${PKG_CONFIG_PATH}"
            PKG_CONFIG_PATH="${sk_pkg_config}:${PKG_CONFIG_PATH}"
            export PKG_CONFIG_PATH
        fi

        # use pkg-config to check for libipa existence
        PKG_CHECK_MODULES([LIBIPA],
            [${version_check}],
            [ENABLE_IPA=1], [ENABLE_IPA=0])
        if test "x${ENABLE_IPA}" = "x0"
        then
            AC_MSG_NOTICE([(${PACKAGE}) Building without IPA support since pkg-config failed to find ${report_version}])
        else
            # verify that libipa has any packages it depends on
            libipa_reported_version=`${PKG_CONFIG} --modversion libipa 2>/dev/null`
            if test "x${libipa_reported_version}" = "x"
            then
                # PKG_CHECK_MODULES() says package is available, but
                # pkg-config does not find it; assume the user set the
                # LIBIPA_LIBS/LIBIPA_CFLAGS variables
                libipa_reported_version=unknown
            else
                AC_MSG_CHECKING([presence of libipa dependencies])
                echo "${as_me}:${LINENO}: \$PKG_CONFIG --libs libipa >/dev/null 2>&AS_MESSAGE_LOG_FD" >&AS_MESSAGE_LOG_FD
                (${PKG_CONFIG} --libs libipa) >/dev/null 2>&AS_MESSAGE_LOG_FD
                sk_pkg_status=$?
                echo "${as_me}:${LINENO}: \$? = ${sk_pkg_status}" >&AS_MESSAGE_LOG_FD
    
                if test 0 -eq ${sk_pkg_status}
                then
                    AC_MSG_RESULT([yes])
                else
                    AC_MSG_RESULT([no])
                    AC_MSG_NOTICE([(${PACKAGE}) Building without IPA support due to missing dependencies for libipa. Details in config.log])
                    ENABLE_IPA=0
                fi
            fi
        fi

        # Restore the PKG_CONFIG_PATH to the saved value
        if test "x${sk_pkg_config}" != "x"
        then
            PKG_CONFIG_PATH="${sk_save_PKG_CONFIG_PATH}"
            export PKG_CONFIG_PATH
        fi
    fi

    # compile program that uses libipa
    if test "x${ENABLE_IPA}" = "x1"
    then
        # Cache current values
        sk_save_LDFLAGS="${LDFLAGS}"
        sk_save_LIBS="${LIBS}"
        sk_save_CFLAGS="${CFLAGS}"
        sk_save_CPPFLAGS="${CPPFLAGS}"

        LIBIPA_LDFLAGS="${LIBIPA_LIBS}"
        LIBS="${LIBIPA_LDFLAGS} ${LIBS}"

        CPPFLAGS="${LIBIPA_CFLAGS} ${CPPFLAGS}"

        AC_MSG_CHECKING([usability of libipa library and headers])
        AC_LINK_IFELSE(
            [AC_LANG_PROGRAM([
#include <ipa/ipa.h>
                ],[
IPAContext *ctx;
                 ])],[ENABLE_IPA=1],[ENABLE_IPA=0])

        if test "x${ENABLE_IPA}" = "x0"
        then
            AC_MSG_RESULT([no])
            AC_MSG_NOTICE([(${PACKAGE}) Building without IPA support since unable to compile program using libipa. Details in config.log])
        else
            AC_MSG_RESULT([yes])
        fi

        # Restore cached values
        LDFLAGS="${sk_save_LDFLAGS}"
        LIBS="${sk_save_LIBS}"
        CFLAGS="${sk_save_CFLAGS}"
        CPPFLAGS="${sk_save_CPPFLAGS}"
    fi

    if test "x${ENABLE_IPA}" = "x0"
    then
        LIBIPA_LDFLAGS=
        LIBIPA_CFLAGS=
    fi

    AM_CONDITIONAL(HAVE_IPA, [test "x${ENABLE_IPA}" = "x1"])

    AC_DEFINE_UNQUOTED([ENABLE_IPA], [${ENABLE_IPA}],
        [Define to 1 to build with support for IPA.  Define to 0 otherwise.
         Requires the libipa library and the <ipa/ipa.h> header file.])

])# AX_PKG_CHECK_LIBIPA


# ---------------------------------------------------------------------------
# AX_PKG_CHECK_LIBIPA_PKGCONFIG
#
#    Run the part of --with-ipa that requires pkgconfig
#
AC_DEFUN([AX_PKG_CHECK_LIBIPA_PKGCONFIG],[
])# AX_PKG_CHECK_LIBIPA_PKGCONFIG

dnl Local Variables:
dnl mode:autoconf
dnl indent-tabs-mode:nil
dnl End:
