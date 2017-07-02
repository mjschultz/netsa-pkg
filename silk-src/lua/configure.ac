dnl Process this file with autoconf to produce a configure script
dnl ------------------------------------------------------------------------
dnl configure.ac
dnl autotools build system for Lua
dnl ------------------------------------------------------------------------

AC_PREREQ(2.64)
AC_INIT([Lua],[5.3.4],[],[lua],[http://www.lua.org])
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_AUX_DIR([autoconf])
AC_CONFIG_SRCDIR([src/lua.h])
AM_INIT_AUTOMAKE([1.11 foreign no-exeext no-installinfo])

# Tell autoconf to write a header, but note that nothing uses it
AC_CONFIG_HEADERS([autoconf.h])

AM_MAINTAINER_MODE

AC_USE_SYSTEM_EXTENSIONS
AC_LANG(C)
LT_INIT

AC_PROG_MAKE_SET
AC_HEADER_STDC

# Files to create
AC_CONFIG_FILES([
    Makefile
    src/Makefile
])

# Add math library if needed
AC_SEARCH_LIBS([sqrt], [m])
# Add dynamic library library if needed
AC_SEARCH_LIBS([dlopen], [dl])

# Check for ULLONG_MAX, LLONG_MAX, LLONG_MIN
AC_CHECK_DECL(ULLONG_MAX, ,
    [AC_DEFINE([ULLONG_MAX], [0xFFFFFFFFFFFFFFFFULL],
        [Set to maximum unsigned long long when not provided by limits.h])],
    [#include <limits.h>])
AC_CHECK_DECL(LLONG_MAX, ,
    [AC_DEFINE([LLONG_MAX],  [0x7FFFFFFFFFFFFFFFLL],
        [Set to maximum signed long long when not provided by limits.h])],
    [#include <limits.h>])
AC_CHECK_DECL(LLONG_MIN, ,
    [AC_DEFINE([LLONG_MIN],  [-0x7FFFFFFFFFFFFFFFLL-1],
        [Set to minimum signed long long when not provided by limits.h])],
    [#include <limits.h>])

# Check for features that the luaconfig.h puts under the LUA_USE_C99
# umbrella.  These tests check whether strtod() understands
# hexadecimal, and whether printf() understands "%a"
AX_CHECK_C99

# Add support for readline if available
AC_CHECK_HEADER([readline/readline.h],[
    AC_CHECK_HEADER([readline/history.h],[
        AC_CHECK_LIB([readline], [add_history], [
            AC_DEFINE([LUA_USE_READLINE], [1],
                [Define to 1 to have Lua use the readline library])
            LIBS="-lreadline ${LIBS}"
])])])

# Since the Lua sources #include standard headers before including
# luaconf.h, there is no way to ensure that autoconf.h is included
# before all other files without editing all of the Lua sources.
#
# Instead, add the various flags to DEFS as needed by grepping their
# values out of the confdefs.h file and adding them to DEFS.
AC_CONFIG_COMMANDS_PRE([
    # Remove -DHAVE_CONFIG_H from DEFS
    DEFS=`echo ${DEFS} | sed s/-DHAVE_CONFIG_H//`

    # As of Lua 5.3.0-alpha, we add these to luaconf.h directly
    # # Add Lua defaults to DEFS
    # DEFS="${DEFS} -DLUA_COMPAT_APIUNSIGNED -DLUA_USE_POSIX -DLUA_USE_DLOPEN"

    for compflag in LUA_USE_AFORMAT LUA_USE_C99 LUA_USE_READLINE _ALL_SOURCE _GNU_SOURCE _POSIX_PTHREAD_SEMANTICS _TANDEM_SOURCE __EXTENSIONS__ _POSIX_1_SOURCE _POSIX_SOURCE ULLONG_MAX LLONG_MAX LLONG_MIN ULLONG_MIN
    do
        sk_def_test=`echo ${compflag} | sed -e 's/,.*$//'`
        sk_def_flag=`echo ${compflag} | sed -e 's/^.*,//'`

        val=`awk '/^# *define[ \t]+'$sk_def_test'[ \t]/ { print $NF; exit; }' confdefs.h`
        if test "X${val}" != X
        then
            DEFS="${DEFS} -D${sk_def_flag}=${val}"
        fi
    done
])

# Output the silk-summary.txt file again so it is the final thing that
# configure produces.
AC_CONFIG_COMMANDS([silk_summary],[
    if test -n "${SILK_SUMMARY_FILE}" && test -f ${SILK_SUMMARY_FILE}
    then
        cat ${SILK_SUMMARY_FILE}
    fi])

AC_OUTPUT
