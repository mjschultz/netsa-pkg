/*
** Copyright (C) 2006-2020 by Carnegie Mellon University.
**
** @OPENSOURCE_LICENSE_START@
** See license information in ../../LICENSE.txt
** @OPENSOURCE_LICENSE_END@
*/
#ifndef _SKSITECONFIG_H
#define _SKSITECONFIG_H
#ifdef __cplusplus
extern "C" {
#endif

#include <silk/silk.h>

RCSIDENTVAR(rcs_SKSITECONFIG_H, "$SiLK: sksiteconfig.h ef14e54179be 2020-04-14 21:57:45Z mthomas $");

#include <silk/skvector.h>

#define SKSITECONFIG_MAX_INCLUDE_DEPTH 16

/* from sksite.c */

extern const char path_format_conversions[];


/* from sksiteconfig_parse.y */

extern int
sksiteconfig_error(
    char               *s);
extern int
sksiteconfig_parse(
    void);

extern int sksiteconfig_testing;


/* from sksiteconfig_lex.l */

extern int
sksiteconfig_lex(
    void);

int
sksiteconfigParse(
    const char         *filename,
    int                 verbose);

#ifdef TEST_PRINTF_FORMATS
#define sksiteconfigErr printf
#else
void
sksiteconfigErr(
    const char         *fmt,
    ...)
    SK_CHECK_PRINTF(1, 2);
#endif

int
sksiteconfigIncludePop(
    void);
void
sksiteconfigIncludePush(
    char               *filename);


/* this list of definitions is from the automake info page */
#define yymaxdepth  sksiteconfig_maxdepth
#define yyparse     sksiteconfig_parse
#define yylex       sksiteconfig_lex
#define yyerror     sksiteconfig_error
#define yylval      sksiteconfig_lval
#define yychar      sksiteconfig_char
#define yydebug     sksiteconfig_debug
#define yypact      sksiteconfig_pact
#define yyr1        sksiteconfig_r1
#define yyr2        sksiteconfig_r2
#define yydef       sksiteconfig_def
#define yychk       sksiteconfig_chk
#define yypgo       sksiteconfig_pgo
#define yyact       sksiteconfig_act
#define yyexca      sksiteconfig_exca
#define yyerrflag   sksiteconfig_errflag
#define yynerrs     sksiteconfig_nerrs
#define yyps        sksiteconfig_ps
#define yypv        sksiteconfig_pv
#define yys         sksiteconfig_s
#define yy_yys      sksiteconfig_yys
#define yystate     sksiteconfig_state
#define yytmp       sksiteconfig_tmp
#define yyv         sksiteconfig_v
#define yy_yyv      sksiteconfig_yyv
#define yyval       sksiteconfig_val
#define yylloc      sksiteconfig_lloc
#define yyreds      sksiteconfig_reds
#define yytoks      sksiteconfig_toks
#define yylhs       sksiteconfig_yylhs
#define yylen       sksiteconfig_yylen
#define yydefred    sksiteconfig_yydefred
#define yydgoto     sksiteconfig_yydgoto
#define yysindex    sksiteconfig_yysindex
#define yyrindex    sksiteconfig_yyrindex
#define yygindex    sksiteconfig_yygindex
#define yytable     sksiteconfig_yytable
#define yycheck     sksiteconfig_yycheck
#define yyname      sksiteconfig_yyname
#define yyrule      sksiteconfig_yyrule

#if 0
/* Newer versions of flex define these functions.  Declare them here
 * to avoid gcc warnings, and just hope that their signatures don't
 * change. */
int
sksiteconfig_get_leng(
    void);
char *
sksiteconfig_get_text(
    void);
int
sksiteconfig_get_debug(
    void);
void
sksiteconfig_set_debug(
    int                 bdebug);
int
sksiteconfig_get_lineno(
    void);
void
sksiteconfig_set_lineno(
    int                 line_number);
FILE *
sksiteconfig_get_in(
    void);
void
sksiteconfig_set_in(
    FILE               *in_str);
FILE *
sksiteconfig_get_out(
    void);
void
sksiteconfig_set_out(
    FILE               *out_str);
int
sksiteconfig_lex_destroy(
    void);
#endif  /* #if 0 */

#ifdef __cplusplus
}
#endif
#endif /* _SKSITECONFIG_H */

/*
** Local Variables:
** mode:c
** indent-tabs-mode:nil
** c-basic-offset:4
** End:
*/
