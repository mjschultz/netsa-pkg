dnl Copyright (C) 2011-2020 by Carnegie Mellon University.
dnl
dnl @OPENSOURCE_LICENSE_START@
dnl See license information in ../LICENSE.txt
dnl @OPENSOURCE_LICENSE_END@

dnl RCSIDENT("$SiLK: ax_check_printf_z.m4 ef14e54179be 2020-04-14 21:57:45Z mthomas $")

# ---------------------------------------------------------------------------
# AX_CHECK_PRINTF_Z
#
#    Determine what format string to use for size_t and ssize_t
#
AC_DEFUN([AX_CHECK_PRINTF_Z],[
    AC_MSG_CHECKING([whether printf understands the "z" modifier])
    sk_save_CFLAGS="${CFLAGS}"
    CFLAGS="${WARN_CFLAGS} ${sk_werror} ${CFLAGS}"
    AC_COMPILE_IFELSE([
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
char a[128];
char b[128];
size_t s = (size_t)0xfedcba98;
sprintf(a, "%zu", s);
sprintf(b, "%lu", (unsigned long)s);
return strcmp(a, b);
            ]])
    ],[
         AC_MSG_RESULT([yes])
         AC_DEFINE([HAVE_PRINTF_Z_FORMAT], [1], [Define to 1 if printf supports the "z" modifier])
    ],[
         AC_MSG_RESULT([no])
    ],[
         AC_MSG_RESULT([assuming no])
    ])
    CFLAGS="${sk_save_CFLAGS}"
])# AX_CHECK_PRINTF_Z

dnl Local Variables:
dnl mode:autoconf
dnl indent-tabs-mode:nil
dnl End:
