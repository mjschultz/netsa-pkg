/*
** Copyright (C) 2005-2020 by Carnegie Mellon University.
**
** @OPENSOURCE_LICENSE_START@
** See license information in ../../LICENSE.txt
** @OPENSOURCE_LICENSE_END@
*/
#ifndef _PROBECONFSCAN_H
#define _PROBECONFSCAN_H
#ifdef __cplusplus
extern "C" {
#endif

#include <silk/silk.h>

RCSIDENTVAR(rcsID_PROBECONFSCAN_H, "$SiLK: probeconfscan.h ef14e54179be 2020-04-14 21:57:45Z mthomas $");

/*
**  probeconfscan.h
**
**  Values needed for the lexer and parser to communicate.
**
*/

#include <silk/utils.h>
#include <silk/probeconf.h>
#include <silk/skvector.h>


/* Provide some grammar debugging info, if necessary */
#define YYDEBUG 1

#define PCSCAN_MAX_INCLUDE_DEPTH 8

/* error message strings */
#define PARSE_MSG_ERROR "Error while parsing file %s at line %d:\n"
#define PARSE_MSG_WARN  "Warning while parsing file %s at line %d:\n"


/* this list of definitions is from the automake info page */
#define yymaxdepth  probeconfscan_maxdepth
#define yyparse     probeconfscan_parse
#define yylex       probeconfscan_lex
#define yyerror     probeconfscan_error
#define yylval      probeconfscan_lval
#define yychar      probeconfscan_char
#define yydebug     probeconfscan_debug
#define yypact      probeconfscan_pact
#define yyr1        probeconfscan_r1
#define yyr2        probeconfscan_r2
#define yydef       probeconfscan_def
#define yychk       probeconfscan_chk
#define yypgo       probeconfscan_pgo
#define yyact       probeconfscan_act
#define yyexca      probeconfscan_exca
#define yyerrflag   probeconfscan_errflag
#define yynerrs     probeconfscan_nerrs
#define yyps        probeconfscan_ps
#define yypv        probeconfscan_pv
#define yys         probeconfscan_s
#define yy_yys      probeconfscan_yys
#define yystate     probeconfscan_state
#define yytmp       probeconfscan_tmp
#define yyv         probeconfscan_v
#define yy_yyv      probeconfscan_yyv
#define yyval       probeconfscan_val
#define yylloc      probeconfscan_lloc
#define yyreds      probeconfscan_reds
#define yytoks      probeconfscan_toks
#define yylhs       probeconfscan_yylhs
#define yylen       probeconfscan_yylen
#define yydefred    probeconfscan_yydefred
#define yydgoto     probeconfscan_yydgoto
#define yysindex    probeconfscan_yysindex
#define yyrindex    probeconfscan_yyrindex
#define yygindex    probeconfscan_yygindex
#define yytable     probeconfscan_yytable
#define yycheck     probeconfscan_yycheck
#define yyname      probeconfscan_yyname
#define yyrule      probeconfscan_yyrule


/* Last keyword */
extern char pcscan_clause[];

/* Global error count for return status of skpcParse */
extern int pcscan_errors;

extern int (*extra_sensor_verify_fn)(skpc_sensor_t *sensor);


int
yyparse(
    void);
int
yylex(
    void);
int
yyerror(
    char               *s);

typedef sk_vector_t number_list_t;

typedef sk_vector_t wildcard_list_t;


int
skpcParseSetup(
    void);

void
skpcParseTeardown(
    void);

#ifdef TEST_PRINTF_FORMATS
#define skpcParseErr printf
#else
int
skpcParseErr(
    const char         *fmt,
    ...)
    SK_CHECK_PRINTF(1, 2);
#endif

int
skpcParseIncludePop(
    void);

int
skpcParseIncludePush(
    char               *filename);


#if 0
/* Newer versions of flex define these functions.  Declare them here
 * to avoid gcc warnings, and just hope that their signatures don't
 * change. */
int
probeconfscan_get_leng(
    void);
char *
probeconfscan_get_text(
    void);
int
probeconfscan_get_debug(
    void);
void
probeconfscan_set_debug(
    int                 bdebug);
int
probeconfscan_get_lineno(
    void);
void
probeconfscan_set_lineno(
    int                 line_number);
FILE *
probeconfscan_get_in(
    void);
void
probeconfscan_set_in(
    FILE               *in_str);
FILE *
probeconfscan_get_out(
    void);
void
probeconfscan_set_out(
    FILE               *out_str);
int
probeconfscan_lex_destroy(
    void);
#endif  /* #if 0 */

#ifdef __cplusplus
}
#endif
#endif /* _PROBECONFSCAN_H */

/*
** Local Variables:
** mode:c
** indent-tabs-mode:nil
** c-basic-offset:4
** End:
*/
