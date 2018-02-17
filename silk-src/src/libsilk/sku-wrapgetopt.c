/*
** Copyright (C) 2003-2018 by Carnegie Mellon University.
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
RCSIDENT("$SiLK: sku-wrapgetopt.c 2e9b8964a7da 2017-12-22 18:13:18Z mthomas $");

#include "getopt.c"
#endif


/*
** Local Variables:
** mode:c
** indent-tabs-mode:nil
** c-basic-offset:4
** End:
*/
