/**
 * @internal
 *
 * null plugin
 *
 * these plugins do nothing, they don't recognize any protocols, they
 * are useful for testing the shared library loading and argument
 * passing to the plugins
 *
 ** ------------------------------------------------------------------------
 ** Copyright (C) 2007-2021 Carnegie Mellon University. All Rights Reserved.
 ** ------------------------------------------------------------------------
 ** Authors: Chris Inacio <inacio@cert.org>
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
 *
 */
#define _YAF_SOURCE_
#include <yaf/autoinc.h>
#include <yaf/yafcore.h>
#include <yaf/decode.h>
#include <payloadScanner.h>

YC_SCANNER_PROTOTYPE(nullplugin_LTX_ycNullScanScan);
YC_SCANNER_PROTOTYPE(nullplugin_LTX_ycNullScanScan2);
YC_SCANNER_PROTOTYPE(nullplugin_LTX_ycNullScanScan3);
YC_SCANNER_PROTOTYPE(nullplugin_LTX_ycNullScanScan4);

/* #define DEBUG_VERBOSITY */


/**
 * nullplugin_LTX_ycNullScanScan
 *
 * @param argc number of string arguments in argv
 * @param argv string arguments for this plugin (first two are library
 *             name and function name)
 * @param payload the packet payload
 * @param payloadSize size of the packet payload
 * @param flow a pointer to the flow state structure
 * @param val a pointer to biflow state (used for forward vs reverse)
 *
 * @return always 0
 */
uint16_t
nullplugin_LTX_ycNullScanScan(
    int             argc,
    char           *argv[],
    const uint8_t  *payload,
    unsigned int    payloadSize,
    yfFlow_t       *flow,
    yfFlowVal_t    *val)
{
#ifdef DEBUG_VERBOSITY
    int loop;
#endif

    /* supress compiler warnings about unused arguments */
    (void)payload;
    (void)payloadSize;
    (void)argc;
    (void)argv;
    (void)flow;
    (void)val;

#ifdef DEBUG_VERBOSITY
    for (loop = 0; loop < argc; loop++) {
        printf("arg %d is \"%s\"\n", loop, argv[loop]);
    }
#endif

    return 0;
}


/**
 * nullplugin_LTX_ycNullScanScan2
 *
 * @param argc number of string arguments in argv
 * @param argv string arguments for this plugin (first two are library
 *             name and function name)
 * @param payload the packet payload
 * @param payloadSize size of the packet payload
 * @param flow a pointer to the flow state structure
 * @param val a pointer to biflow state (used for forward vs reverse)
 *
 * @return always 0
 */
uint16_t
nullplugin_LTX_ycNullScanScan2(
    int             argc,
    char           *argv[],
    const uint8_t  *payload,
    unsigned int    payloadSize,
    yfFlow_t       *flow,
    yfFlowVal_t    *val)
{
#ifdef DEBUG_VERBOSITY
    int loop;
#endif

    /* supress compiler warnings about unused arguments */
    (void)payload;
    (void)payloadSize;
    (void)argc;
    (void)argv;
    (void)flow;
    (void)val;

#ifdef DEBUG_VERBOSITY
    for (loop = 0; loop < argc; loop++) {
        printf("arg %d is \"%s\"\n", loop, argv[loop]);
    }
#endif

    return 0;
}


/**
 * nullplugin_LTX_ycNullScanScan3
 *
 * @param argc number of string arguments in argv
 * @param argv string arguments for this plugin (first two are library
 *             name and function name)
 * @param payload the packet payload
 * @param payloadSize size of the packet payload
 * @param flow a pointer to the flow state structure
 * @param val a pointer to biflow state (used for forward vs reverse)
 *
 * @return always 0
 */
uint16_t
nullplugin_LTX_ycNullScanScan3(
    int             argc,
    char           *argv[],
    const uint8_t  *payload,
    unsigned int    payloadSize,
    yfFlow_t       *flow,
    yfFlowVal_t    *val)
{
#ifdef DEBUG_VERBOSITY
    int loop;
#endif

    /* supress compiler warnings about unused arguments */
    (void)payload;
    (void)payloadSize;
    (void)argc;
    (void)argv;
    (void)flow;
    (void)val;

#ifdef DEBUG_VERBOSITY
    for (loop = 0; loop < argc; loop++) {
        printf("arg %d is \"%s\"\n", loop, argv[loop]);
    }
#endif

    return 0;
}


/**
 * nullplugin_LTX_ycNullScanScan4
 *
 * @param argc number of string arguments in argv
 * @param argv string arguments for this plugin (first two are library
 *             name and function name)
 * @param payload the packet payload
 * @param payloadSize size of the packet payload
 * @param flow a pointer to the flow state structure
 * @param val a pointer to biflow state (used for forward vs reverse)
 *
 * @return always 0
 */
uint16_t
nullplugin_LTX_ycNullScanScan4(
    int             argc,
    char           *argv[],
    const uint8_t  *payload,
    unsigned int    payloadSize,
    yfFlow_t       *flow,
    yfFlowVal_t    *val)
{
#ifdef DEBUG_VERBOSITY
    int loop;
#endif

    /* supress compiler warnings about unused arguments */
    (void)payload;
    (void)payloadSize;
    (void)argc;
    (void)argv;
    (void)flow;
    (void)val;

#ifdef DEBUG_VERBOSITY
    for (loop = 0; loop < argc; loop++) {
        printf("arg %d is \"%s\"\n", loop, argv[loop]);
    }
#endif

    return 0;
}
