/*
** Copyright (C) 2003-2017 by Carnegie Mellon University.
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
RCSIDENT("$SiLK: sku-wrapgetopt.c 275df62a2e41 2017-01-05 17:30:40Z mthomas $");

#include "getopt.c"
#endif


/*
** Local Variables:
** mode:c
** indent-tabs-mode:nil
** c-basic-offset:4
** End:
*/
