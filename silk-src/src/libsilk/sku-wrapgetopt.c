/*
** Copyright (C) 2003-2019 by Carnegie Mellon University.
**
** @OPENSOURCE_LICENSE_START@
** See license information in ../../LICENSE.txt
** @OPENSOURCE_LICENSE_END@
*/

/*
**  sku-wrapgetopt.c
**
**  Compile getopt.c when SK_HAVE_GETOPT_LONG_ONLY is not defined
*/

#include <silk/silk.h>

#ifndef SK_HAVE_GETOPT_LONG_ONLY
RCSIDENT("$SiLK: sku-wrapgetopt.c 945cf5167607 2019-01-07 18:54:17Z mthomas $");

#include "getopt.c"
#endif


/*
** Local Variables:
** mode:c
** indent-tabs-mode:nil
** c-basic-offset:4
** End:
*/
