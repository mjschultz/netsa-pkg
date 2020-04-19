/*
** Copyright (C) 2006-2020 by Carnegie Mellon University.
**
** @OPENSOURCE_LICENSE_START@
** See license information in ../../LICENSE.txt
** @OPENSOURCE_LICENSE_END@
*/
#ifndef _INTDICT_H
#define _INTDICT_H
#ifdef __cplusplus
extern "C" {
#endif

#include <silk/silk.h>

RCSIDENTVAR(rcsID_INTDICT_H, "$SiLK: intdict.h ef14e54179be 2020-04-14 21:57:45Z mthomas $");

/*
**  intdict.h
**
**  Integer dictionaries
**
*/


typedef int32_t intkey_t;

struct int_dict_st;
typedef struct int_dict_st int_dict_t;

struct int_dict_iter_st;
typedef struct int_dict_iter_st int_dict_iter_t;

int_dict_t *
int_dict_create(
    size_t              value_size);

void
int_dict_destroy(
    int_dict_t         *dict);

void *
int_dict_get(
    int_dict_t         *dict,
    intkey_t            key,
    void               *value);

void *
int_dict_get_first(
    int_dict_t         *dict,
    intkey_t           *key,
    void               *value);

void *
int_dict_get_last(
    int_dict_t         *dict,
    intkey_t           *key,
    void               *value);

void *
int_dict_get_next(
    int_dict_t         *dict,
    intkey_t           *key,
    void               *value);

void *
int_dict_get_prev(
    int_dict_t         *dict,
    intkey_t           *key,
    void               *value);

int
int_dict_set(
    int_dict_t         *dict,
    intkey_t            key,
    void               *value);

int
int_dict_del(
    int_dict_t         *dict,
    intkey_t            key);

unsigned int
int_dict_get_count(
    int_dict_t         *dict);

int_dict_iter_t *
int_dict_open(
    int_dict_t         *dict);

void *
int_dict_next(
    int_dict_iter_t    *iter,
    intkey_t           *key,
    void               *value);

void
int_dict_close(
    int_dict_iter_t    *iter);

#ifdef __cplusplus
}
#endif
#endif /* _INTDICT_H */

/*
** Local Variables:
** mode:c
** indent-tabs-mode:nil
** c-basic-offset:4
** End:
*/
