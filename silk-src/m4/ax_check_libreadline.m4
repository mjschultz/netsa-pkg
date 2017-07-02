dnl Copyright (C) 2015-2017 by Carnegie Mellon University.
dnl
dnl @OPENSOURCE_LICENSE_START@
dnl See license information in ../LICENSE.txt
dnl @OPENSOURCE_LICENSE_END@

dnl RCSIDENT("$SiLK: ax_check_libreadline.m4 efd886457770 2017-06-21 18:43:23Z mthomas $")

# ---------------------------------------------------------------------------
# AX_CHECK_LIBREADLINE
#
#    Try to find the readline library.
#
#    Output variables: READLINE_CFLAGS READLINE_LDFLAGS
#
AC_DEFUN([AX_CHECK_LIBREADLINE],[
    AC_SUBST(READLINE_CFLAGS)
    AC_SUBST(READLINE_LDFLAGS)

    AC_ARG_WITH([readline],[AS_HELP_STRING([--with-readline=READLINE_DIR],
            [specify location of the readline library; find "readline/readline.h" in READLINE_DIR/include/; find "libreadline.so" in READLINE_DIR/lib/ [auto]])[]dnl
        ],[
            if test "x${withval}" != "xyes"
            then
                readline_dir="${withval}"
                readline_includes="${readline_dir}/include"
                readline_libraries="${readline_dir}/lib"
            fi
    ])
    AC_ARG_WITH([readline-includes],[AS_HELP_STRING([--with-readline-includes=DIR],
            [find "readline/readline.h" in DIR/ (overrides READLINE_DIR/include/)])[]dnl
        ],[
            if test "x${withval}" = "xno"
            then
                readline_dir=no
            elif test "x${withval}" != "xyes"
            then
                readline_includes="${withval}"
            fi
    ])
    AC_ARG_WITH([readline-libraries],[AS_HELP_STRING([--with-readline-libraries=DIR],
            [find "libreadline.so" in DIR/ (overrides READLINE_DIR/lib/)])[]dnl
        ],[
            if test "x${withval}" = "xno"
            then
                readline_dir=no
            elif test "x${withval}" != "xyes"
            then
                readline_libraries="${withval}"
            fi
    ])

    ENABLE_READLINE=0
    if test "x${readline_dir}" != "xno"
    then
        # Cache current values
        sk_save_LDFLAGS="${LDFLAGS}"
        sk_save_LIBS="${LIBS}"
        sk_save_CFLAGS="${CFLAGS}"
        sk_save_CPPFLAGS="${CPPFLAGS}"

        if test "x${readline_libraries}" != "x"
        then
            READLINE_LDFLAGS="-L${readline_libraries}"
            LDFLAGS="${READLINE_LDFLAGS} ${sk_save_LDFLAGS}"
        fi

        if test "x${readline_includes}" != "x"
        then
            READLINE_CFLAGS="-I${readline_includes}"
            CPPFLAGS="${READLINE_CFLAGS} ${sk_save_CPPFLAGS}"
        fi

        AC_CHECK_LIB([readline], [add_history],
            [ENABLE_READLINE=1 ; READLINE_LDFLAGS="${READLINE_LDFLAGS} -lreadline"])

        if test "x${ENABLE_READLINE}" = "x1"
        then
            AC_CHECK_HEADER([readline/readline.h], , [
                AC_MSG_WARN([Found libreadline but not readline/readline.h.  Maybe you should install readline-devel?])
                ENABLE_READLINE=0])
        fi

        if test "x${ENABLE_READLINE}" = "x1"
        then
            AC_MSG_CHECKING([usability of READLINE library and headers])
            LDFLAGS="${sk_save_LDFLAGS}"
            LIBS="${READLINE_LDFLAGS} ${sk_save_LIBS}"
            AC_LINK_IFELSE(
                [AC_LANG_PROGRAM([
#include <stdio.h>
#include <readline/readline.h>
                    ],[
char *line = readline("Enter a line: ");
printf("You entered \"%s\"\n", line);
                     ])],[
                AC_MSG_RESULT([yes])],[
                AC_MSG_RESULT([no])
                ENABLE_READLINE=0])
        fi

        # Restore cached values
        LDFLAGS="${sk_save_LDFLAGS}"
        LIBS="${sk_save_LIBS}"
        CFLAGS="${sk_save_CFLAGS}"
        CPPFLAGS="${sk_save_CPPFLAGS}"
    fi

    if test "x${ENABLE_READLINE}" != "x1"
    then
        READLINE_LDFLAGS=
        READLINE_CFLAGS=
    else
        READLINE_CFLAGS="-DLUA_USE_READLINE ${READLINE_CFLAGS}"
    fi
])# AX_CHECK_LIBREADLINE

dnl Local Variables:
dnl mode:autoconf
dnl indent-tabs-mode:nil
dnl End:
