/*
** Copyright (C) 2001-2020 by Carnegie Mellon University.
**
** @OPENSOURCE_LICENSE_START@
** See license information in ../../LICENSE.txt
** @OPENSOURCE_LICENSE_END@
*/

/*
**  sku-times.c
**
**  various utility functions for dealing with time
**
**  Suresh L Konda
**  1/24/2002
*/


#include <silk/silk.h>

RCSIDENT("$SiLK: sku-times.c ef14e54179be 2020-04-14 21:57:45Z mthomas $");

#include <silk/utils.h>


/* Time to ASCII */
char *
sktimestamp_r(
    char               *outbuf,
    sktime_t            t,
    unsigned int        timestamp_flags)
{
    struct tm ts;
    struct tm *rv;
    imaxdiv_t t_div;
    time_t t_sec;
    const int form_mask = (SKTIMESTAMP_NOMSEC | SKTIMESTAMP_EPOCH
                           | SKTIMESTAMP_MMDDYYYY | SKTIMESTAMP_ISO);

    t_div = imaxdiv(t, 1000);
    t_sec = (time_t)(t_div.quot);

    if (timestamp_flags & SKTIMESTAMP_EPOCH) {
        if (timestamp_flags & SKTIMESTAMP_NOMSEC) {
            snprintf(outbuf, SKTIMESTAMP_STRLEN-1, ("%" PRIdMAX), t_div.quot);
        } else {
            snprintf(outbuf, SKTIMESTAMP_STRLEN-1, ("%" PRIdMAX ".%03" PRIdMAX),
                     t_div.quot, t_div.rem);
        }
        return outbuf;
    }

    switch (timestamp_flags & (SKTIMESTAMP_UTC | SKTIMESTAMP_LOCAL)) {
      case SKTIMESTAMP_UTC:
        /* force UTC */
        rv = gmtime_r(&t_sec, &ts);
        break;
      case SKTIMESTAMP_LOCAL:
        /* force localtime */
        rv = localtime_r(&t_sec, &ts);
        break;
      default:
        /* use default timezone */
#if  SK_ENABLE_LOCALTIME
        rv = localtime_r(&t_sec, &ts);
#else
        rv = gmtime_r(&t_sec, &ts);
#endif
        break;
    }
    if (NULL == rv) {
        memset(&ts, 0, sizeof(struct tm));
    }

    switch (timestamp_flags & form_mask) {
      case SKTIMESTAMP_EPOCH:
      case (SKTIMESTAMP_EPOCH | SKTIMESTAMP_NOMSEC):
        /* these should have been handled above */
        skAbortBadCase(timestamp_flags & form_mask);
        break;

      case SKTIMESTAMP_MMDDYYYY:
        /* "MM/DD/YYYY HH:MM:SS.sss" */
        snprintf(outbuf, SKTIMESTAMP_STRLEN-1,
                 ("%02d/%02d/%04d %02d:%02d:%02d.%03" PRIdMAX),
                 ts.tm_mon + 1, ts.tm_mday, ts.tm_year + 1900,
                 ts.tm_hour, ts.tm_min, ts.tm_sec, t_div.rem);
        break;

      case (SKTIMESTAMP_MMDDYYYY | SKTIMESTAMP_NOMSEC):
        /* "MM/DD/YYYY HH:MM:SS" */
        snprintf(outbuf, SKTIMESTAMP_STRLEN-1, "%02d/%02d/%04d %02d:%02d:%02d",
                 ts.tm_mon + 1, ts.tm_mday, ts.tm_year + 1900,
                 ts.tm_hour, ts.tm_min, ts.tm_sec);
        break;

      case SKTIMESTAMP_ISO:
        /* "YYYY-MM-DD HH:MM:SS.sss" */
        snprintf(outbuf, SKTIMESTAMP_STRLEN-1,
                 ("%04d-%02d-%02d %02d:%02d:%02d.%03" PRIdMAX),
                 ts.tm_year + 1900, ts.tm_mon + 1, ts.tm_mday,
                 ts.tm_hour, ts.tm_min, ts.tm_sec, t_div.rem);
        break;

      case (SKTIMESTAMP_ISO | SKTIMESTAMP_NOMSEC):
        /* "YYYY-MM-DD HH:MM:SS" */
        snprintf(outbuf, SKTIMESTAMP_STRLEN-1, "%04d-%02d-%02d %02d:%02d:%02d",
                 ts.tm_year + 1900, ts.tm_mon + 1, ts.tm_mday,
                 ts.tm_hour, ts.tm_min, ts.tm_sec);
        break;

      case SKTIMESTAMP_NOMSEC:
        /* "YYYY/MM/DDTHH:MM:SS" */
        snprintf(outbuf, SKTIMESTAMP_STRLEN-1, "%04d/%02d/%02dT%02d:%02d:%02d",
                 ts.tm_year + 1900, ts.tm_mon + 1, ts.tm_mday,
                 ts.tm_hour, ts.tm_min, ts.tm_sec);
        break;

      default:
        /* "YYYY/MM/DDTHH:MM:SS.sss" */
        snprintf(outbuf, SKTIMESTAMP_STRLEN-1,
                 ("%04d/%02d/%02dT%02d:%02d:%02d.%03" PRIdMAX),
                 ts.tm_year + 1900, ts.tm_mon + 1, ts.tm_mday,
                 ts.tm_hour, ts.tm_min, ts.tm_sec, t_div.rem);
        break;
    }

    return outbuf;
}


char *
sktimestamp(
    sktime_t            t,
    unsigned int        timestamp_flags)
{
    static char t_buf[SKTIMESTAMP_STRLEN];
    return sktimestamp_r(t_buf, t, timestamp_flags);
}



/*
 *  max_day = skGetMaxDayInMonth(year, month);
 *
 *    Return the maximum number of days in 'month' in the specified
 *    'year'.
 *
 *    NOTE:  Months are in the 1..12 range and NOT 0..11
 *
 */
int
skGetMaxDayInMonth(
    int                 yr,
    int                 mo)
{
    static int month_days[12] = {31,28,31,30,31,30,31,31,30,31,30,31};

    /* use a 0-based array */
    assert(1 <= mo && mo <= 12);

    /* if not February or not a potential leap year, return fixed
     * value from array */
    if ((mo != 2) || ((yr % 4) != 0)) {
        return month_days[mo-1];
    }
    /* else year is divisible by 4, need more tests */

    /* year not divisible by 100 is a leap year, and year divisible by
     * 400 is a leap year */
    if (((yr % 100) != 0) || ((yr % 400) == 0)) {
        return 1 + month_days[mo-1];
    }
    /* else year is divisible by 100 but not by 400; not a leap year */

    return month_days[mo-1];
}


/* like gettimeofday returning an sktime_t */
sktime_t
sktimeNow(
    void)
{
    struct timeval tv;

    (void)gettimeofday(&tv, NULL);
    return sktimeCreateFromTimeval(&tv);
}


/*
** Local Variables:
** mode:c
** indent-tabs-mode:nil
** c-basic-offset:4
** End:
*/
