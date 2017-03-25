dnl Copyright (C) 2004-2017 by Carnegie Mellon University.
dnl
dnl @OPENSOURCE_LICENSE_START@
dnl See license information in ../LICENSE.txt
dnl @OPENSOURCE_LICENSE_END@

dnl RCSIDENT("$SiLK: ax_pkg_check_gnutls.m4 275df62a2e41 2017-01-05 17:30:40Z mthomas $")


# ---------------------------------------------------------------------------
# AX_PKG_CHECK_GNUTLS
#
#    Determine how to use GNUTLS.
#
#    Output variables:  GNUTLS_CFLAGS, GNUTLS_LDFLAGS, ENABLE_GNUTLS
#
#    Output definitions: HAVE_DECL_GNUTLS_CERT_EXPIRED
#
AC_DEFUN([AX_PKG_CHECK_GNUTLS],[
    AC_SUBST(GNUTLS_CFLAGS)
    AC_SUBST(GNUTLS_LDFLAGS)

    # Either one or two arguments; first is minimum version required;
    # if a second is present, it represents a version that is "too
    # new"
    gnutls_required_version="$1"
    if test "x$2" = "x"
    then
        version_check="gnutls >= $1"
        report_version="gnutls.pc >= $1"
    else
        version_check="gnutls >= $1 gnutls < $2"
        report_version="gnutls.pc >= $1, gnutls.pc < $2"
    fi

    ENABLE_GNUTLS=0

    # The configure switch
    sk_pkg_config=""
    AC_ARG_WITH([gnutls],[AS_HELP_STRING([--with-gnutls=DIR],
            [specify location of the GnuTLS transport security package; find "gnutls.pc" in the directory DIR/ (i.e., prepend DIR to PKG_CONFIG_PATH).  The last component of DIR is likely "pkgconfig" [auto]])[]dnl
        ],[
            if test "x${withval}" != "xyes"
            then
                sk_pkg_config="${withval}"
            fi
    ])

    if test "x${sk_pkg_config}" = "xno"
    then
        AC_MSG_NOTICE([(${PACKAGE}) Building without GnuTLS support at user request])
    else
        # prepend any argument to PKG_CONFIG_PATH
        if test "x${sk_pkg_config}" != "x"
        then
            sk_save_PKG_CONFIG_PATH="${PKG_CONFIG_PATH}"
            PKG_CONFIG_PATH="${sk_pkg_config}:${PKG_CONFIG_PATH}"
            export PKG_CONFIG_PATH
        fi

        # use pkg-config to check for gnutls existence
        PKG_CHECK_MODULES([GNUTLS],
            [${version_check}],
            [ENABLE_GNUTLS=1], [ENABLE_GNUTLS=0])

        if test "x${ENABLE_GNUTLS}" = "x0"
        then
            AC_MSG_NOTICE([(${PACKAGE}) Building without GnuTLS support since pkg-config failed to find ${report_version}])
        else
            # verify that gnutls has the packages it depends on
            gnutls_reported_version=`${PKG_CONFIG} --modversion gnutls 2>/dev/null`
            if test "x${gnutls_reported_version}" = "x"
            then
                # PKG_CHECK_MODULES() says package is available, but
                # pkg-config does not find it; assume the user set the
                # GNUTLS_LIBS/GNUTLS_CFLAGS variables
                gnutls_reported_version=unknown
            else
                AC_MSG_CHECKING([presence of gnutls dependencies])
                echo "${as_me}:${LINENO}: \$PKG_CONFIG --libs gnutls >/dev/null 2>&AS_MESSAGE_LOG_FD" >&AS_MESSAGE_LOG_FD
                (${PKG_CONFIG} --libs gnutls) >/dev/null 2>&AS_MESSAGE_LOG_FD
                sk_pkg_status=$?
                echo "${as_me}:${LINENO}: \$? = ${sk_pkg_status}" >&AS_MESSAGE_LOG_FD
    
                if test 0 -eq ${sk_pkg_status}
                then
                    AC_MSG_RESULT([yes])
                else
                    AC_MSG_RESULT([no])
                    AC_MSG_NOTICE([(${PACKAGE}) Building without GnuTLS support due to missing dependencies for gnutls. Details in config.log])
                    ENABLE_GNUTLS=0
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

    # compile program that uses gnutls
    if test "x${ENABLE_GNUTLS}" = "x1"
    then
        # Cache current values
        sk_save_LDFLAGS="${LDFLAGS}"
        sk_save_LIBS="${LIBS}"
        sk_save_CFLAGS="${CFLAGS}"
        sk_save_CPPFLAGS="${CPPFLAGS}"

        GNUTLS_LDFLAGS="${GNUTLS_LIBS}"
        LIBS="${GNUTLS_LDFLAGS} ${LIBS}"

        CPPFLAGS="${GNUTLS_CFLAGS} ${CPPFLAGS}"

        AC_MSG_CHECKING([usability of gnutls library and headers])
        AC_LINK_IFELSE(
                [AC_LANG_PROGRAM([
#include <stdlib.h>
#include <gnutls/gnutls.h>
#ifdef HAVE_PTHREAD_H
#include <pthread.h>
#endif
#ifdef HAVE_ERRNO_H
#include <errno.h>
#endif

gnutls_certificate_credentials_t cred;
                    ],[
gnutls_global_init();
                     ])],[ENABLE_GNUTLS=1],[ENABLE_GNUTLS=0])

        if test "x${ENABLE_GNUTLS}" = "x0"
        then
            AC_MSG_RESULT([no])
            AC_MSG_NOTICE([(${PACKAGE}) Building without GnuTLS support since unable to compile program using gnutls. Details in config.log])
        else
            # This function went away in GnuTLS 3.0.0
            AC_CHECK_FUNCS([gnutls_transport_set_lowat])

            # This was added in GnuTLS 2.6.6
            AC_CHECK_DECLS([GNUTLS_CERT_EXPIRED], [], [], [[#include <gnutls/gnutls.h>]])
        fi

        # Restore cached values
        LDFLAGS="${sk_save_LDFLAGS}"
        LIBS="${sk_save_LIBS}"
        CFLAGS="${sk_save_CFLAGS}"
        CPPFLAGS="${sk_save_CPPFLAGS}"
    fi

    if test "x${ENABLE_GNUTLS}" = "x0"
    then
        GNUTLS_LDFLAGS=
        GNUTLS_CFLAGS=
    fi

    AC_DEFINE_UNQUOTED([ENABLE_GNUTLS], [${ENABLE_GNUTLS}],
        [Define to 1 build with support for GnuTLS.  Define to 0 otherwise.
         Requires the GnuTLS library and the <gnutls/gnutls.h> header file.])
])# AX_PKG_CHECK_GNUTLS


# ---------------------------------------------------------------------------
# AX_PKG_CHECK_GNUTLS_PKGCONFIG
#
#    Run the part of --with-gnutls that requires pkgconfig
#
AC_DEFUN([AX_PKG_CHECK_GNUTLS_PKGCONFIG],[
])# AX_PKG_CHECK_GNUTLS_PKGCONFIG


# ---------------------------------------------------------------------------
# AX_CHECK_LIBGCRYPT
#
#    Determine how to use libgcrypt.  Used when configuring gnutls
#    since pkgconfig/gnutls.pc may not include -lgcrypt (which makes
#    me wonder what the point on pkgconfig is).
#
AC_DEFUN([AX_CHECK_LIBGCRYPT],[
    AC_ARG_WITH([libgcrypt],[AS_HELP_STRING([--with-libgcrypt-config=CONFIG_PROG],
            [specify location of the libgcrypt cryptographic library configuration program; find "libgcrypt-config" at CONFIG_PROG [auto]])[]dnl
        ],[
            if test "x${withval}" != "xyes"
            then
                libgcrypt_config="${withval}"
            fi
    ])

    ENABLE_LIBGCRYPT=0
    if test "x${libgcrypt_config}" = "x"
    then
        AC_PATH_PROG([IGNORE_LIBGCRYPT_CONFIG], [libgcrypt-config], [no])
        libgcrypt_config=${IGNORE_LIBGCRYPT_CONFIG}
    fi

    if test "x${libgcrypt_config}" != "xno"
    then
        # Just extract the values but don't check them here; we'll let
        # gnutls do the checking
        LIBGCRYPT_LDFLAGS=`${libgcrypt_config} --libs 2>/dev/null`
        LIBGCRYPT_CFLAGS=`${libgcrypt_config} --cflags 2>/dev/null`
    fi
])# AX_CHECK_LIBGCRYPT

dnl Local Variables:
dnl mode:autoconf
dnl indent-tabs-mode:nil
dnl End:
