/*
** mio_source_tcp.c
** Multiple I/O passive TCP stream source
**
** ------------------------------------------------------------------------
** Copyright (C) 2006-2021 Carnegie Mellon University. All Rights Reserved.
** ------------------------------------------------------------------------
** Authors: Brian Trammell
** ------------------------------------------------------------------------
** @OPENSOURCE_HEADER_START@
** Use of the YAF system and related source code is subject to the terms
** of the following licenses:
**
** GNU General Public License (GPL) Rights pursuant to Version 2, June 1991
** Government Purpose License Rights (GPLR) pursuant to DFARS 252.227.7013
**
** NO WARRANTY
**
** ANY INFORMATION, MATERIALS, SERVICES, INTELLECTUAL PROPERTY OR OTHER
** PROPERTY OR RIGHTS GRANTED OR PROVIDED BY CARNEGIE MELLON UNIVERSITY
** PURSUANT TO THIS LICENSE (HEREINAFTER THE "DELIVERABLES") ARE ON AN
** "AS-IS" BASIS. CARNEGIE MELLON UNIVERSITY MAKES NO WARRANTIES OF ANY
** KIND, EITHER EXPRESS OR IMPLIED AS TO ANY MATTER INCLUDING, BUT NOT
** LIMITED TO, WARRANTY OF FITNESS FOR A PARTICULAR PURPOSE,
** MERCHANTABILITY, INFORMATIONAL CONTENT, NONINFRINGEMENT, OR ERROR-FREE
** OPERATION. CARNEGIE MELLON UNIVERSITY SHALL NOT BE LIABLE FOR INDIRECT,
** SPECIAL OR CONSEQUENTIAL DAMAGES, SUCH AS LOSS OF PROFITS OR INABILITY
** TO USE SAID INTELLECTUAL PROPERTY, UNDER THIS LICENSE, REGARDLESS OF
** WHETHER SUCH PARTY WAS AWARE OF THE POSSIBILITY OF SUCH DAMAGES.
** LICENSEE AGREES THAT IT WILL NOT MAKE ANY WARRANTY ON BEHALF OF
** CARNEGIE MELLON UNIVERSITY, EXPRESS OR IMPLIED, TO ANY PERSON
** CONCERNING THE APPLICATION OF OR THE RESULTS TO BE OBTAINED WITH THE
** DELIVERABLES UNDER THIS LICENSE.
**
** Licensee hereby agrees to defend, indemnify, and hold harmless Carnegie
** Mellon University, its trustees, officers, employees, and agents from
** all claims or demands made against them (and any related losses,
** expenses, or attorney's fees) arising out of, or relating to Licensee's
** and/or its sub licensees' negligent use or willful misuse of or
** negligent conduct or willful misconduct regarding the Software,
** facilities, or other rights or assistance granted by Carnegie Mellon
** University under this License, including, but not limited to, any
** claims of product liability, personal injury, death, damage to
** property, or violation of any laws or regulations.
**
** Carnegie Mellon University Software Engineering Institute authored
** documents are sponsored by the U.S. Department of Defense under
** Contract FA8721-05-C-0003. Carnegie Mellon University retains
** copyrights in all material produced under this contract. The U.S.
** Government retains a non-exclusive, royalty-free license to publish or
** reproduce these documents, or allow others to do so, for U.S.
** Government purposes only pursuant to the copyright license under the
** contract clause at 252.227.7013.
**
** @OPENSOURCE_HEADER_END@
** ------------------------------------------------------------------------
*/

/**
 * @file
 *
 * MIO passive TCP socket source initializer. Most applications should use the
 * interface in mio_config.h to access this initializer.
 */

/* idem hack */
#ifndef _AIRFRAME_MIO_SOURCE_TCP_H_
#define _AIRFRAME_MIO_SOURCE_TCP_H_
#include <airframe/mio.h>

/**
 * TCP source configuration context. Pass as the cfg argument to
 * mio_source_init_tcp().
 */

typedef struct _MIOSourceTCPConfig {
    /** String containing default service name or integer TCP port number. */
    char            *default_port;
    /**
     * select(2) timeout used by next_source; next_source will wait no longer
     * than this before failing and setting MIO_F_CTL_POLL to allow
     * applications to do work or to detect termination while awaiting a
     * connection.
     */
    struct timeval   timeout;
} MIOSourceTCPConfig;

/**
 * Initialize a source for reading from a passive TCP socket.
 * This source supports single-threaded, sequential access only; clients
 * connecting to an application using this source may be refused connection
 * while the application is servicing a previously connected client.
 *
 * @param source    pointer to MIOSource to initialize. This MIOSource will
 *                  be overwritten.
 * @param spec      input specifier to initialize MIOSource with.
 *                  Must be a service specifier of the form "[host,]service"
 *                  where host is the IPv4 or IPv6 name or address of an
 *                  interface to bind to, or * to bind to all interfaces, and
 *                  service is a service name or TCP port number to bind to.
 *                  If omitted, host is assumed to be *. If spec is NULL,
 *                  host is assumed to be * and service is taken from the
 *                  cfg paramater.
 * @param vsp_type  requested source pointer type, or MIO_T_ANY for default.
 * @param cfg       pointer to configuration context.
 *                  Must be a pointer to an MIOSourceTCPConfig structure.
 * @param err       An error description pointer.
 * @return TRUE if the MIOSource was successfully initialized.
 */
gboolean
mio_source_init_tcp(
    MIOSource   *source,
    const char  *spec,
    MIOType      vsp_type,
    void        *cfg,
    GError     **err);

#endif /* ifndef _AIRFRAME_MIO_SOURCE_TCP_H_ */
