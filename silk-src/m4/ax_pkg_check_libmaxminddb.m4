dnl Copyright (C) 2018-2020 by Carnegie Mellon University.
dnl
dnl @OPENSOURCE_LICENSE_START@
dnl See license information in ../LICENSE.txt
dnl @OPENSOURCE_LICENSE_END@

dnl RCSIDENT("$SiLK: ax_pkg_check_libmaxminddb.m4 ef14e54179be 2020-04-14 21:57:45Z mthomas $")


# ---------------------------------------------------------------------------
# AX_PKG_CHECK_LIBMAXMINDDB
#
#    Determine how to use the LIBMAXMINDDB library.  Function accepts two
#    arguments (requires 1): minimum allowed version and too-new
#    version.
#
#    Output variables:   LIBMAXMINDDB_CFLAGS, LIBMAXMINDDB_LDFLAGS
#    Output definitions: ENABLE_LIBMAXMINDDB
#
AC_DEFUN([AX_PKG_CHECK_LIBMAXMINDDB],[
    AC_SUBST(LIBMAXMINDDB_CFLAGS)
    AC_SUBST(LIBMAXMINDDB_LDFLAGS)

    # Either one or two arguments; first is minimum version required;
    # if a second is present, it represents a version that is "too
    # new"
    libmaxminddb_required_version="$1"
    if test "x$2" = "x"
    then
        version_check="libmaxminddb >= $1"
    else
        version_check="libmaxminddb >= $1 libmaxminddb < $2"
    fi

    ENABLE_LIBMAXMINDDB=0

    # whether to exit with an error if building without libmaxminddb.
    # this is set to true when --with-libmaxminddb is given and its
    # argument is not "no"
    sk_withval_used=false

    # The configure switch
    sk_pkg_config=""
    AC_ARG_WITH([libmaxminddb],[AS_HELP_STRING([--with-libmaxminddb=DIR],
            [specify location of GeoIP2 library package; find "libmaxminddb.pc" in the directory DIR/.  The last component of DIR is likely "pkgconfig" [auto]])[]dnl
        ],[
            case "x${withval}" in
            xno)  sk_pkg_config="${withval}"  ;;
            xyes) sk_withval_used=true        ;;
            *)    sk_pkg_config="${withval}"  sk_withval_used=true ;;
            esac
    ])

    if test "x${sk_pkg_config}" = "xno"
    then
        AC_MSG_NOTICE([Building without libmaxminddb support at user request])
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
                AC_MSG_WARN([Argument to --with-libmaxminddb should probably end with '/pkgconfig'])
            fi
        fi

        # use pkg-config to check for libmaxminddb existence
        echo "${as_me}:${LINENO}: Using PKG_CONFIG_PATH='${PKG_CONFIG_PATH}'" >&AS_MESSAGE_LOG_FD
        PKG_CHECK_MODULES([LIBMAXMINDDB],
            [${version_check}],
            [ENABLE_LIBMAXMINDDB=1], [ENABLE_LIBMAXMINDDB=0])
        if test "x${ENABLE_LIBMAXMINDDB}" = "x0"
        then
            AC_MSG_NOTICE([Building without libmaxminddb support: pkg-config failed to find libmaxminddb])
            if ${sk_withval_used}
            then
                AC_MSG_ERROR([unable to use libmaxminddb; fix or remove --with-libmaxminddb switch])
            fi
        else
            # verify that libmaxminddb has the packages it depends on
            sk_pkg_modversion=`${PKG_CONFIG} --modversion libmaxminddb 2>/dev/null`
            if test "x${sk_pkg_modversion}" = "x"
            then
                # PKG_CHECK_MODULES() says package is available, but
                # pkg-config does not find it; assume the user set the
                # LIBMAXMINDDB_LIBS/LIBMAXMINDDB_CFLAGS variables
                sk_pkg_modversion=unknown
            else
                AC_MSG_CHECKING([presence of libmaxminddb-${sk_pkg_modversion} dependencies])
                echo "${as_me}:${LINENO}: \$PKG_CONFIG --libs libmaxminddb >/dev/null 2>&AS_MESSAGE_LOG_FD" >&AS_MESSAGE_LOG_FD
                (${PKG_CONFIG} --libs libmaxminddb) >/dev/null 2>&AS_MESSAGE_LOG_FD
                sk_pkg_status=$?
                echo "${as_me}:${LINENO}: \$? = ${sk_pkg_status}" >&AS_MESSAGE_LOG_FD

                if test 0 -eq ${sk_pkg_status}
                then
                    AC_MSG_RESULT([yes])
                else
                    AC_MSG_RESULT([no])
                    AC_MSG_NOTICE([Building without libmaxminddb support: pkg-config failed to find dependencies for libmaxminddb. Details in config.log])
                    if ${sk_withval_used}
                    then
                        AC_MSG_ERROR([unable to use libmaxminddb; fix or remove --with-libmaxminddb switch])
                    fi
                    ENABLE_LIBMAXMINDDB=0
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

    # compile program that uses libmaxminddb
    if test "x${ENABLE_LIBMAXMINDDB}" = "x1"
    then
        # Cache current values
        sk_save_LDFLAGS="${LDFLAGS}"
        sk_save_LIBS="${LIBS}"
        sk_save_CFLAGS="${CFLAGS}"
        sk_save_CPPFLAGS="${CPPFLAGS}"

        LIBMAXMINDDB_LDFLAGS="${LIBMAXMINDDB_LIBS}"
        LIBS="${LIBMAXMINDDB_LDFLAGS} ${LIBS}"

        CPPFLAGS="${LIBMAXMINDDB_CFLAGS} ${CPPFLAGS}"

        AC_MSG_CHECKING([usability of libmaxminddb-${sk_pkg_modversion} library and headers])
        AC_LINK_IFELSE(
                [AC_LANG_PROGRAM([
#include <maxminddb.h>
                    ],[
    MMDB_s mmdb;
    MMDB_open("/dev/null", MMDB_MODE_MMAP, &mmdb);
                     ])],[ENABLE_LIBMAXMINDDB=1],[ENABLE_LIBMAXMINDDB=0])

        if test "x${ENABLE_LIBMAXMINDDB}" = "x1"
        then
            AC_MSG_RESULT([yes])
        else
            AC_MSG_RESULT([no])
            AC_MSG_NOTICE([Building without libmaxminddb support: pkg-config found libmaxminddb-${sk_pkg_modversion} but failed to compile a program that uses it. Details in config.log])
            if ${sk_withval_used}
            then
                AC_MSG_ERROR([unable to use libmaxminddb; fix or remove --with-libmaxminddb switch])
            fi
        fi

        # Restore cached values
        LDFLAGS="${sk_save_LDFLAGS}"
        LIBS="${sk_save_LIBS}"
        CFLAGS="${sk_save_CFLAGS}"
        CPPFLAGS="${sk_save_CPPFLAGS}"
    fi

    if test "x${ENABLE_LIBMAXMINDDB}" = "x0"
    then
        LIBMAXMINDDB_LDFLAGS=
        LIBMAXMINDDB_CFLAGS=
    else
        AC_DEFINE([ENABLE_LIBMAXMINDDB], 1,
            [Define to 1 include support for processing MaxMind GeoIP2
             binary files.  Requires the libmaxminddb library and the
             <maxminddb.h> header file.])
    fi
])# AX_PKG_CHECK_LIBMAXMINDDB

dnl Local Variables:
dnl mode:autoconf
dnl indent-tabs-mode:nil
dnl End:
