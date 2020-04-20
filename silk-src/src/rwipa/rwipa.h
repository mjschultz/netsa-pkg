/*
** Copyright (C) 2007-2020 by Carnegie Mellon University.
**
** @OPENSOURCE_LICENSE_START@
** See license information in ../../LICENSE.txt
** @OPENSOURCE_LICENSE_END@
*/
#ifndef _RWIPA_H
#define _RWIPA_H
#ifdef __cplusplus
extern "C" {
#endif

#include <silk/silk.h>

RCSIDENTVAR(rcsID_RWIPA_H, "$SiLK: rwipa.h ef14e54179be 2020-04-14 21:57:45Z mthomas $");

#include <silk/iptree.h>
#include <silk/skbag.h>
#include <silk/skipaddr.h>
#include <silk/skipset.h>
#include <silk/skprefixmap.h>
#include <silk/sksite.h>
#include <silk/skstream.h>
#include <silk/utils.h>

#ifdef __cplusplus
extern "C" {
#endif

SK_DIAGNOSTIC_IGNORE_PUSH("-Wundef")
SK_DIAGNOSTIC_IGNORE_PUSH("-Wwrite-strings")

#include <ipa/ipa.h>

SK_DIAGNOSTIC_IGNORE_POP("-Wwrite-strings")
SK_DIAGNOSTIC_IGNORE_POP("-Wundef")

#ifdef __cplusplus
}
#endif

char *
get_ipa_config(
    void);

#ifdef __GNUC__
/* quiet warnings about unused variables symbols_main_p,
 * symbols_find_p, and symbols_none_p in ipa/ipa.h */

__attribute__((__unused__))
static int
quiet_unused_var_warnings(
    void)
{
    return ((int)symbols_main_p->symbol_token
            + (int)symbols_find_p->symbol_token
            + (int)symbols_none_p->symbol_token);
}
#endif  /* __GNUC__ */

#ifdef __cplusplus
}
#endif
#endif /* _RWIPA_H */

/*
** Local Variables:
** mode:c
** indent-tabs-mode:nil
** c-basic-offset:4
** End:
*/
