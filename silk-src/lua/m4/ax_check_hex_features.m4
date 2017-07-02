dnl Copyright (C) 2011-2017 by Carnegie Mellon University.
dnl
dnl @OPENSOURCE_LICENSE_START@
dnl See license information in ../../LICENSE.txt
dnl @OPENSOURCE_LICENSE_END@

dnl RCSIDENT("$SiLK: ax_check_hex_features.m4 efd886457770 2017-06-21 18:43:23Z mthomas $")

# ---------------------------------------------------------------------------
# AX_CHECK_C99
#
#    Check for C99 features:
#
#      * Confirm that stdint.h exists.
#
#      * Determine whether strtod() is C99 compliant and supports
#        hexadecimal numbers
#
#      * Determine whether printf() is C99 compliant and supports "aA"
#        output format for floating point hexadecimal.
#
AC_DEFUN([AX_CHECK_C99],[

    lua_have_stdint=0
    AC_CHECK_HEADER([stdint.h], [lua_have_stdint=1])

    lua_use_strtodhex=0
    AC_MSG_CHECKING([whether strtod() can read hexadecimal])
    AC_RUN_IFELSE([
        AC_LANG_PROGRAM([[
#include <stdio.h>
#if HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif
#ifdef    STDC_HEADERS
#  include <stdlib.h>
#  include <stddef.h>
#else
#  ifdef  HAVE_STDLIB_H
#    include <stdlib.h>
#  endif
#  ifdef  HAVE_MALLOC_H
#    include <malloc.h>
#  endif
#endif
#ifdef    HAVE_STRING_H
#  if     !defined STDC_HEADERS && defined HAVE_MEMORY_H
#    include <memory.h>
#  endif
#  include <string.h>
#endif
#ifdef    HAVE_STRINGS_H
#  include <strings.h>
#endif
            ]], [[
char *ep;
(void)strtod("0xff", &ep);
return 'x' == *ep;
            ]])
    ],[
         AC_MSG_RESULT([yes])
         lua_use_strtodhex=1
    ],[
         AC_MSG_RESULT([no])
    ],[
         AC_MSG_RESULT([assuming no])
    ])

    lua_use_aformat=0
    AC_MSG_CHECKING([whether printf() supports "%a" format])
    AC_RUN_IFELSE([
        AC_LANG_PROGRAM([[
#include <stdio.h>
#if HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif
#ifdef    STDC_HEADERS
#  include <stdlib.h>
#  include <stddef.h>
#else
#  ifdef  HAVE_STDLIB_H
#    include <stdlib.h>
#  endif
#  ifdef  HAVE_MALLOC_H
#    include <malloc.h>
#  endif
#endif
#ifdef    HAVE_STRING_H
#  if     !defined STDC_HEADERS && defined HAVE_MEMORY_H
#    include <memory.h>
#  endif
#  include <string.h>
#endif
#ifdef    HAVE_STRINGS_H
#  include <strings.h>
#endif
            ]], [[
const char expected[] = "0x1.0000p+4,0X1.0000P+4";
double d = 16.0;
char buf[256];

(void)sprintf(buf, "%.4a,%.4A", d, d);
fprintf(stderr, "sample output is   '%s'\n", buf);
fprintf(stderr, "expected output is '%s'\n", expected);
return strcmp(buf, expected);
            ]])
    ],[
         AC_MSG_RESULT([yes])
         lua_use_aformat=1
    ],[
         AC_MSG_RESULT([no])
    ],[
         AC_MSG_RESULT([assuming no])
    ])

    case "${lua_have_stdint}${lua_use_strtodhex}${lua_use_aformat}" in
      111)
        AC_DEFINE([LUA_USE_C99], [1], [Define to 1 if the required C99 features are available])
        ;;
      101|001)
        AC_DEFINE([LUA_USE_AFORMAT], [1], [Define to 1 if printf() supports "a" format])
        ;;
    esac

])# AX_CHECK_C99

dnl Local Variables:
dnl mode:autoconf
dnl indent-tabs-mode:nil
dnl End:
