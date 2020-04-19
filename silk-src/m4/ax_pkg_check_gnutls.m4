dnl Copyright (C) 2004-2020 by Carnegie Mellon University.
dnl
dnl @OPENSOURCE_LICENSE_START@
dnl See license information in ../LICENSE.txt
dnl @OPENSOURCE_LICENSE_END@

dnl RCSIDENT("$SiLK: ax_pkg_check_gnutls.m4 ef14e54179be 2020-04-14 21:57:45Z mthomas $")


# ---------------------------------------------------------------------------
# AX_PKG_CHECK_GNUTLS
#
#    Determine how to use the GNUTLS library.  Function accepts two
#    arguments (requires 1): minimum allowed version and too-new
#    version.
#
#    Output variables:  GNUTLS_CFLAGS, GNUTLS_LDFLAGS
#    Output definitions: ENABLE_GNUTLS
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
    else
        version_check="gnutls >= $1 gnutls < $2"
    fi

    ENABLE_GNUTLS=0

    # whether to exit with an error if building without libgnutls.
    # this is set to true when --with-gnutls is given and its
    # argument is not "no"
    sk_withval_used=false

    # The configure switch
    sk_pkg_config=""
    AC_ARG_WITH([gnutls],[AS_HELP_STRING([--with-gnutls=DIR],
            [specify location of the GnuTLS transport security package; find "gnutls.pc" in the directory DIR/ (i.e., prepend DIR to PKG_CONFIG_PATH).  The last component of DIR is likely "pkgconfig" [auto]])[]dnl
        ],[
            case "x${withval}" in
            xno)  sk_pkg_config="${withval}"  ;;
            xyes) sk_withval_used=true        ;;
            *)    sk_pkg_config="${withval}"  sk_withval_used=true ;;
            esac
    ])

    if test "x${sk_pkg_config}" = "xno"
    then
        AC_MSG_NOTICE([Building without GnuTLS support at user request])
    else
        if test "x${sk_pkg_config}" != "x"
        then
            # prepend the argument to PKG_CONFIG_PATH, and warn when
            # that argument does not end with "/pkgconfig"
            sk_save_PKG_CONFIG_PATH="${PKG_CONFIG_PATH}"
            PKG_CONFIG_PATH="${sk_pkg_config}:${PKG_CONFIG_PATH}"
            export PKG_CONFIG_PATH

            if expr "x${sk_pkg_config}" : '.*/pkgconfig$' > /dev/null
            then
                :
            else
                AC_MSG_WARN([Argument to --with-gnutls should probably end with '/pkgconfig'])
            fi
        fi

        # use pkg-config to check for gnutls existence
        echo "${as_me}:${LINENO}: Using PKG_CONFIG_PATH='${PKG_CONFIG_PATH}'" >&AS_MESSAGE_LOG_FD
        PKG_CHECK_MODULES([GNUTLS],
            [${version_check}],
            [ENABLE_GNUTLS=1], [ENABLE_GNUTLS=0])
        if test "x${ENABLE_GNUTLS}" = "x0"
        then
            AC_MSG_NOTICE([Building without GnuTLS support: pkg-config failed to find gnutls])
            if ${sk_withval_used}
            then
                AC_MSG_ERROR([unable to use GnuTLS; fix or remove --with-gnutls switch])
            fi
        else
            # verify that gnutls has the packages it depends on
            sk_pkg_modversion=`${PKG_CONFIG} --modversion gnutls 2>/dev/null`
            if test "x${sk_pkg_modversion}" = "x"
            then
                # PKG_CHECK_MODULES() says package is available, but
                # pkg-config does not find it; assume the user set the
                # GNUTLS_LIBS/GNUTLS_CFLAGS variables
                sk_pkg_modversion=unknown
            else
                AC_MSG_CHECKING([presence of gnutls-${sk_pkg_modversion} dependencies])
                echo "${as_me}:${LINENO}: \$PKG_CONFIG --libs gnutls >/dev/null 2>&AS_MESSAGE_LOG_FD" >&AS_MESSAGE_LOG_FD
                (${PKG_CONFIG} --libs gnutls) >/dev/null 2>&AS_MESSAGE_LOG_FD
                sk_pkg_status=$?
                echo "${as_me}:${LINENO}: \$? = ${sk_pkg_status}" >&AS_MESSAGE_LOG_FD
    
                if test 0 -eq ${sk_pkg_status}
                then
                    AC_MSG_RESULT([yes])
                else
                    AC_MSG_RESULT([no])
                    AC_MSG_NOTICE([Building without GnuTLS support: pkg-config failed to find dependencies for gnutls. Details in config.log])
                    if ${sk_withval_used}
                    then
                        AC_MSG_ERROR([unable to use GnuTLS; fix or remove --with-gnutls switch])
                    fi
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

        AC_MSG_CHECKING([usability of gnutls-${sk_pkg_modversion} library and headers])
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

        if test "x${ENABLE_GNUTLS}" = "x1"
        then
            AC_MSG_RESULT([yes])
        else
            AC_MSG_RESULT([no])
            AC_MSG_NOTICE([Building without GnuTLS support: pkg-config found gnutls-${sk_pkg_modversion} but failed to compile a program that uses it. Details in config.log])
            if ${sk_withval_used}
            then
                AC_MSG_ERROR([unable to use GnuTLS; fix or remove --with-gnutls switch])
            fi
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

dnl Local Variables:
dnl mode:autoconf
dnl indent-tabs-mode:nil
dnl End:
