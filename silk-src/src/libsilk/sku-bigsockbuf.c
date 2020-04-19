/*
** Copyright (C) 2004-2020 by Carnegie Mellon University.
**
** @OPENSOURCE_LICENSE_START@
** See license information in ../../LICENSE.txt
** @OPENSOURCE_LICENSE_END@
*/

/*
**
**  Exports a method to portably set socket send/receive buffer sizes.
**
*/


#include <silk/silk.h>

RCSIDENT("$SiLK: sku-bigsockbuf.c ef14e54179be 2020-04-14 21:57:45Z mthomas $");

#include <silk/utils.h>

/*
 * function: skGrowSocketBuffer
 *
 * There is no portable way to determine the max send and receive buffers
 * that can be set for a socket, so guess then decrement that guess by
 * 2K until the call succeeds.  If n > 1MB then the decrement by .5MB
 * instead.
 *
 * returns size or -1 for error
 */
int
skGrowSocketBuffer(
    int                 fd,
    int                 dir,
    int                 size)
{
    int n, tries;

    /* initial size */
    n = size;
    tries = 0;

    while (n > 4096) {
        if (setsockopt(fd, SOL_SOCKET, dir, (char*)&n, sizeof (n)) < 0) {
            /* anything other than no buffers available is fatal */
            if (errno != ENOBUFS) {
                return -1;
            }
            /* try a smaller value */
            if (n > 1024*1024) { /*most systems not > 256K bytes w/o tweaking*/
                n -= 1024*1024;
            } else {
                n -= 2048;
            }
            ++tries;
        } else {
            return n;
        }
    } /* while */

    /* no increase in buffer size */
    return 0;
}


/*
** Local Variables:
** mode:c
** indent-tabs-mode:nil
** c-basic-offset:4
** End:
*/
