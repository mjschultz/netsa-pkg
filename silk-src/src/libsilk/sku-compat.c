/*
** Copyright (C) 2006-2020 by Carnegie Mellon University.
**
** @OPENSOURCE_LICENSE_START@
** See license information in ../../LICENSE.txt
** @OPENSOURCE_LICENSE_END@
*/

/*
**  sku-compat.c
**
**    Function
**
*/


#include <silk/silk.h>

RCSIDENT("$SiLK: sku-compat.c ef14e54179be 2020-04-14 21:57:45Z mthomas $");

#include <silk/utils.h>


/* Compute quotient and remainder in one structure like div(), but
 * with intmax_t's instead of int's. */
sk_imaxdiv_t
sk_imaxdiv(
    sk_intmax_t         numer,
    sk_intmax_t         denom)
{
    sk_imaxdiv_t res;
    res.quot = numer / denom;
    res.rem = numer % denom;
    return res;
}


/* Copy data from 'src' to 'dst' stopping at value 'c' or when 'len'
 * octets have been copied.  If 'c' was not found, NULL is returned;
 * else return value points to character after 'c' in 'dst'. */
void *
sk_memccpy(
    void               *dst,
    const void         *src,
    int                 c,
    size_t              len)
{
    uint8_t *c_dst = (uint8_t*)dst;
    const uint8_t *c_src = (const uint8_t*)src;

    for ( ; len > 0; --len) {
        *c_dst = *c_src;
        ++c_dst;
        if (*c_src == c) {
            return c_dst;
        }
        ++c_src;
    }
    return NULL;
}


/* Set environment variable 'name' to 'value', unless 'name' already
 * exists in the environment and 'overwrite' is 0. */
int
sk_setenv(
    const char         *name,
    const char         *value,
    int                 overwrite)
{
    /* This function allocates new memory on every call.  This will be
     * reported as a memory leak in valgrind, but that is hard to
     * avoid.  We could reduce memory usage by overwriting the current
     * value for 'name' when 'name' exists in the environment and
     * strlen(value) is not greater than strlen(getenv(name)). */

    char *buf;

    if (strchr(name, '=')) {
        errno = EINVAL;
        return -1;
    }

    if (overwrite || !getenv(name)) {
        buf = (char*)malloc(2 + strlen(name) + strlen(value));
        if (NULL == buf) {
            return -1;
        }
        strcpy(buf, name);
        strcat(buf, "=");
        strcat(buf, value);
        return putenv(buf);
    }

    return 0;
}


/* Return next 'delim'-delimited token from *stringp; move *stringp to
 * start of next token. */
char *
sk_strsep(
    char              **stringp,
    const char         *delim)
{
    char *cp = *stringp;
    size_t sz;

    if (NULL == cp) {
        return NULL;
    }

    sz = strcspn(cp, delim);
    if (cp[sz] == '\0') {
        /* reached end of string */
        *stringp = NULL;
    } else {
        cp[sz] = '\0';
        *stringp += (sz + 1);
    }

    return cp;
}


/* Inverse of gmtime(); convert a struct tm to time_t. */
time_t
sk_timegm(
    struct tm          *tm)
{
    time_t t_offset, t_2offset;
    struct tm tm_offset;

    /* compute time_t with the timezone offset */
    t_offset = mktime(tm);
    if (t_offset == -1) {
        /* see if adjusting the hour allows mktime() to work */
        tm->tm_hour--;
        t_offset = mktime(tm);
        if (t_offset == -1) {
            return -1;
        }
        /* adjusting hour worked; add back that time */
        t_offset += 3600;
    }

    /* compute a second value with another timezone offset */
    gmtime_r(&t_offset, &tm_offset);
    tm_offset.tm_isdst = 0;
    t_2offset = mktime(&tm_offset);
    if (t_2offset == -1) {
        tm_offset.tm_hour--;
        t_2offset = mktime(&tm_offset);
        if (t_2offset == -1) {
            return -1;
        }
        t_2offset += 3600;
    }

    /* difference in the two time_t's is one TZ offset */
    return (t_offset - (t_2offset - t_offset));
}


#if 0
/* just like snprintf(), but always NUL terminates */
int
sk_snprintf(
    char               *str,
    size_t              size,
    const char         *format,
    ...)
{
    va_list args;
    int rv;

    va_start(args, format);
    rv = sk_vsnprintf(str, size, format, args);
    va_end(args);
    return rv;
}


/* just like vsnprintf(), but always NUL terminates */
int
sk_vsnprintf(
    char               *str,
    size_t              size,
    const char         *format,
    va_list             args)
{
#undef vsnprintf
    int rv = vsnprintf(str, size, format, args);
    str[size-1] = '\0';
    return rv;
}
#endif


/*
** Local Variables:
** mode:c
** indent-tabs-mode:nil
** c-basic-offset:4
** End:
*/
