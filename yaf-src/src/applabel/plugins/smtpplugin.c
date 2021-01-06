/**
 *@internal
 *
 *@file slpplugin.c
 *
 *@brief this is a protocol classifier for the service location protocol (SLP)
 *
 * SLP is a protocol to find well known protocol/services on a local area
 * network.  It can scale from small scale networks to large lan networks.
 * For small scale networks, it uses multicasting in order to ask all
 * machines for a service.  In larger networks it uses Directory Agents
 * in order to centralize management of service information and increase
 * scaling by decreasing network load.
 *
 * @sa rfc 2608  href="http://www.ietf.org/rfc/rfc2608.txt"
 *
 ** ------------------------------------------------------------------------
 ** Copyright (C) 2007-2020 Carnegie Mellon University. All Rights Reserved.
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

#if YAF_ENABLE_HOOKS
#include <yaf/yafhooks.h>
#endif

#define SMTP_PORT_NUMBER 25
#define NUM_CAPT_VECTS 60
#define SMTP_MAX_EMAILS 10

YC_SCANNER_PROTOTYPE(smtpplugin_LTX_ycSMTPScanScan);

static pcre        *smtpRegex = NULL;

static pcre        *smtpStartMessageRegex = NULL;
static pcre        *smtpEndMessageRegex = NULL;
static pcre        *smtpBlankLineRegex = NULL;

static pcre        *smtpHelloRegex = NULL;
static pcre        *smtpEnhancedRegex = NULL;
static pcre        *smtpSizeRegex = NULL;
static pcre        *smtpStartTLSRegex = NULL;
static pcre        *smtpFailedRegex = NULL;
static pcre        *smtpSubjectRegex = NULL;
static pcre        *smtpToRegex = NULL;
static pcre        *smtpFromRegex = NULL;
static pcre        *smtpFileRegex = NULL;
static pcre        *smtpURLRegex = NULL;
static pcre        *smtpHeaderRegex = NULL;
static unsigned int pcreInitialized = 0;

static uint16_t
ycSMTPScanInit(
    void);

/**
 * smtpplugin_LTX_ycSMTPScanScan
 *
 * returns SMTP_PORT_NUMBER if the passed in payload matches a service location
 * protocol packet
 *
 * @param argc number of string arguments in argv
 * @param argv string arguments for this plugin (first two are library
 *             name and function name)
 * @param payload the packet payload
 * @param payloadSize size of the packet payload
 * @param flow a pointer to the flow state structure
 * @param val a pointer to biflow state (used for forward vs reverse)
 *
 *
 * @return SMTP_PORT_NUMBER otherwise 0
 */
uint16_t
smtpplugin_LTX_ycSMTPScanScan(
    int             argc,
    char           *argv[],
    const uint8_t  *payload,
    unsigned int    payloadSize,
    yfFlow_t       *flow,
    yfFlowVal_t    *val)
{
    int rc;
    int vects[NUM_CAPT_VECTS];

    if (0 == pcreInitialized) {
        if (0 == ycSMTPScanInit()) {
            return 0;
        }
    }

    rc = pcre_exec(smtpRegex, NULL, (char *)payload, payloadSize, 0, 0, vects,
                   NUM_CAPT_VECTS);

#if YAF_ENABLE_HOOKS
    if (rc > 0) {
        int startRc;
        int blankRc;
        int offset = 0;

        startRc = pcre_exec(smtpStartMessageRegex, NULL, (char *)payload,
                            payloadSize, 0, 0, vects, NUM_CAPT_VECTS);

        while (startRc > 0) {
            yfHookScanPayload(flow, payload, 2, NULL, vects[1], 38,
                              SMTP_PORT_NUMBER);
            offset = vects[1];

            blankRc = pcre_exec(smtpBlankLineRegex, NULL, (char *)payload,
                                payloadSize, offset, 0, vects,
                                NUM_CAPT_VECTS);
            if (blankRc > 0) {
                yfHookScanPayload(flow, payload, 2, NULL, vects[0], 40,
                                  SMTP_PORT_NUMBER);
            }

            startRc = pcre_exec(smtpStartMessageRegex, NULL, (char *)payload,
                                payloadSize, offset, 0, vects,
                                NUM_CAPT_VECTS);
        }

        yfHookScanPayload(flow, payload, payloadSize, smtpEndMessageRegex, 0,
                          39, SMTP_PORT_NUMBER);

        yfHookScanPayload(flow, payload, payloadSize, smtpHelloRegex, 0, 26,
                          SMTP_PORT_NUMBER);
        yfHookScanPayload(flow, payload, payloadSize, smtpSubjectRegex, 0, 31,
                          SMTP_PORT_NUMBER);
        yfHookScanPayload(flow, payload, payloadSize, smtpToRegex, 0, 32,
                          SMTP_PORT_NUMBER);
        yfHookScanPayload(flow, payload, payloadSize, smtpFromRegex, 0, 33,
                          SMTP_PORT_NUMBER);
        yfHookScanPayload(flow, payload, payloadSize, smtpStartTLSRegex, 0, 29,
                          SMTP_PORT_NUMBER);
        yfHookScanPayload(flow, payload, payloadSize, smtpFileRegex, 0, 34,
                          SMTP_PORT_NUMBER);
        yfHookScanPayload(flow, payload, payloadSize, smtpURLRegex, 0, 35,
                          SMTP_PORT_NUMBER);
        yfHookScanPayload(flow, payload, payloadSize, smtpHeaderRegex, 0, 36,
                          SMTP_PORT_NUMBER);
    } else if (flow->appLabel == SMTP_PORT_NUMBER) {
        yfHookScanPayload(flow, payload, payloadSize, smtpEnhancedRegex, 0, 27,
                          SMTP_PORT_NUMBER);
        yfHookScanPayload(flow, payload, payloadSize, smtpSizeRegex, 0, 28,
                          SMTP_PORT_NUMBER);
        yfHookScanPayload(flow, payload, payloadSize, smtpFailedRegex, 0, 30,
                          SMTP_PORT_NUMBER);
    }
#endif /* if YAF_ENABLE_HOOKS */

    if (rc > 0 || flow->appLabel == SMTP_PORT_NUMBER) {
        return SMTP_PORT_NUMBER;
    }

    return 0;
}


/**
 * ycSMTPScanInit
 *
 * this initializes the PCRE expressions needed to search the payload for SMTP
 *
 * @sideeffect sets the initialized flag on success
 *
 * @return 1 if initialization is complete correctly, 0 otherwise
 */
static
uint16_t
ycSMTPScanInit(
    void)
{
    const char *errorString;
    int         errorPos;

    const char  smtpRegexString[] = "(?i)^(HE|EH)LO\\b";

    /* FIXME: Should we ensure this is the only content on a line? */
    const char  smtpStartMessageRegexString[] = "DATA";
    const char  smtpEndMessageRegexString[] = "\r?\n\\.\r?\n";
    const char  smtpBlankLineRegexString[] = "\r?\n\r?\n";

    const char  smtpHelloRegexString[] =
        "((?i)(HE|EH)LO ?\\[?[a-zA-Z0-9 \\.]+\\]?)\\b";
    const char  smtpEnhancedRegexString[] = "(ESMTP [_a-zA-Z0-9., @#]+)\\b";
    const char  smtpSizeRegexString[] = "(?i)size[ =]([0-9]+)";
    const char  smtpStartTLSRegexString[] = "(?i)starttls";
    const char  smtpFailedRegexString[] =
        "\\r?\\n([45]\\d{2} ?[-a-zA-Z0-9@.,:?=/ ]+\\[?[0-9.]*\\]?)\\b";
    /* the field-body regex is the same as for smtpHeaderRegexString; perhaps
     * we should not allow the body to span multiple lines */
    const char  smtpSubjectRegexString[] =
        "(?i)\\nSubject: *((?:.|\\r?\\n)+?)\\r?\\n(?! )";
    const char  smtpToRegexString[] =
        "(?i)RCPT TO: ?<?([-a-zA-Z0-9._ ]+\\@?[-a-zA-Z0-9._]+)>?";
    const char  smtpFromRegexString[] =
        "(?i)MAIL FROM: ?<?([-a-zA-Z0-9_. ]+\\@?\\[?[-a-zA-Z0-9._]+\\]?)>?";
    const char  smtpFileRegexString[] = "(?i)filename=([-a-zA-Z0-9\"._ ]+)";
    const char  smtpURLRegexString[] =
        "https?:\\/\\/(www\\.)?[-a-zA-Z0-9@:%._\\+~#=]{1,256}\\.[a-zA-Z0-9()]{1,6}\b([-a-zA-Z0-9()@:%_\\+.~#?&//=]*)";
    /* RFC2822 2.2: field-name is any ASCII from decimal 33(!) to 126(~)
     * inclusive except 58(:); note ASCII chars 57(9) and 59(;) */
    const char  smtpHeaderRegexString[] =
        "([!-9;-~]+?: (?:.|\\r?\\n)+?)\\r?\\n(?! )";

    smtpRegex = pcre_compile(smtpRegexString, PCRE_ANCHORED, &errorString,
                             &errorPos, NULL);

    /* FIXME: Many of these should use MUTLILINE mode, where we ensure the
     * text occurs at the start of a line. */
    smtpStartMessageRegex = pcre_compile(smtpStartMessageRegexString, 0,
                                         &errorString, &errorPos, NULL);
    smtpEndMessageRegex = pcre_compile(smtpEndMessageRegexString, 0,
                                       &errorString, &errorPos, NULL);
    smtpBlankLineRegex = pcre_compile(smtpBlankLineRegexString, 0,
                                      &errorString, &errorPos, NULL);

    smtpHelloRegex = pcre_compile(smtpHelloRegexString, 0, &errorString,
                                  &errorPos, NULL);
    smtpEnhancedRegex = pcre_compile(smtpEnhancedRegexString, 0, &errorString,
                                     &errorPos, NULL);
    smtpSizeRegex = pcre_compile(smtpSizeRegexString, 0, &errorString,
                                 &errorPos, NULL);
    smtpStartTLSRegex = pcre_compile(smtpStartTLSRegexString, 0, &errorString,
                                     &errorPos, NULL);
    smtpFailedRegex = pcre_compile(smtpFailedRegexString, 0, &errorString,
                                   &errorPos, NULL);
    smtpSubjectRegex = pcre_compile(smtpSubjectRegexString, 0, &errorString,
                                    &errorPos, NULL);
    smtpToRegex = pcre_compile(smtpToRegexString, 0, &errorString, &errorPos,
                               NULL);
    smtpFromRegex = pcre_compile(smtpFromRegexString, 0, &errorString,
                                 &errorPos, NULL);
    smtpFileRegex = pcre_compile(smtpFileRegexString, 0, &errorString,
                                 &errorPos, NULL);
    smtpURLRegex = pcre_compile(smtpURLRegexString, 0, &errorString, &errorPos,
                                NULL);
    smtpHeaderRegex = pcre_compile(smtpHeaderRegexString, 0, &errorString,
                                   &errorPos, NULL);

    if (NULL != smtpRegex) {
        pcreInitialized = 1;
    } else {
        g_debug("errpos is %d", errorPos);
    }

    return pcreInitialized;
}
