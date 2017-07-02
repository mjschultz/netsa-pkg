/*
** Copyright (C) 2006-2017 by Carnegie Mellon University.
**
** @OPENSOURCE_LICENSE_START@
** See license information in ../../LICENSE.txt
** @OPENSOURCE_LICENSE_END@
*/

/*
 *  skstream.c
 *      Mark Thomas  July-2006
 *
 *      skstream provides a wrapper around file pointers and file
 *      descriptors.  It handles both textual and binary data.
 *
 *      The code can handle reading a gzipped stream, either from a
 *      regular file or from pipe.  In general, on the first read for
 *      any stream, we see if first two bytes are the gzip magic
 *      number.  If so, the code to process the gzipped stream is
 *      initialized so that any "read" gets uncompressed data.  If
 *      not, the first two bytes are copied into the read buffer of
 *      the function that was requesting a read.  An additional read
 *      is used to get the remainder of the callers request.
 */


#include <silk/silk.h>

RCSIDENT("$SiLK: skstream.c 714f88c3a772 2017-06-29 21:49:36Z mthomas $");

#include <silk/skstream.h>
#include <silk/sksite.h>
#include "skheader_priv.h"
#include "skstream_priv.h"

SK_DIAGNOSTIC_IGNORE_PUSH("-Wundef")

#if SK_ENABLE_SNAPPY
#include <snappy-c.h>
#endif

#if SK_ENABLE_LZO
#include SK_LZO_HEADER_NAME
#endif
#ifdef SK_HAVE_LZO1X_DECOMPRESS_ASM_FAST_SAFE
#include SK_LZO_ASM_HEADER_NAME
#endif

SK_DIAGNOSTIC_IGNORE_POP("-Wundef")

#ifdef SKSTREAM_TRACE_LEVEL
#define TRACEMSG_LEVEL SKSTREAM_TRACE_LEVEL
#endif
#define TRACEMSG(lvl, msg) TRACEMSG_TO_TRACEMSGLVL(lvl, msg)
#include <silk/sktracemsg.h>

#if TRACEMSG_LEVEL > 0
#define V(v_v) (void *)(v_v)
#if SK_HAVE_C99___FUNC__
#define FMT_FLS "%s:%d [%p] %s() "
#define FLS(v_v) __FILE__, __LINE__, V(v_v), __func__
#else
#define FMT_FLS "%s:%d [%p] "
#define FLS(v_v) __FILE__, __LINE__, V(v_v)
#endif
#endif


/* LOCAL DEFINES AND TYPEDEFS */

#define SKSTREAM_READ_INITIAL       2048

#define STREAM_BLOCK_HDR_DATA       0x80000001

#define STREAM_BLOCK_HDR_SIDECAR    0x80000002

#define STREAM_BLOCK_HDR_END        0xfeebdaed

#ifndef DEFAULT_FILE_FORMAT
#  define DEFAULT_FILE_FORMAT  FT_RWIPV6ROUTING
#endif

/*
 *    Name of environment variable that affects how to treat ICMP flow
 *    records.  This variable determines the setting of the
 *    'silk_icmp_nochange' global.  See the detailed note in
 *    skStreamReadRecord().
 */
#define SILK_ICMP_SPORT_HANDLER_ENVAR "SILK_ICMP_SPORT_HANDLER"


/*
 *    First two bytes of a gzip-stream are decimal 31,139 (RFC1952)
 */
#define STREAM_MAGIC_NUMBER_GZIP  0x1f8b

/*
 *    Octet-length required to check magic numbers
 */
#define STREAM_CHECK_MAGIC_BUFSIZE  sizeof(uint16_t)


/*
 *    Return SKSTREAM_ERR_NULL_ARGUMENT when 'srin_stream' is NULL.
 */
#define STREAM_RETURN_IF_NULL(srin_stream) \
    if (NULL == (srin_stream)) { return SKSTREAM_ERR_NULL_ARGUMENT; }


/*
 *    Set the 'is_silk_flow' bit on 'stream' if the format of the
 *    header indicates it contains SiLK Flow records.
 */
#define STREAM_SET_IS_SILK_FLOW(stream)                         \
    switch (skHeaderGetFileFormat((stream)->silk_hdr)) {        \
      case FT_RWAUGMENTED:                                      \
      case FT_RWAUGROUTING:                                     \
      case FT_RWAUGWEB:                                         \
      case FT_RWAUGSNMPOUT:                                     \
      case FT_RWFILTER:                                         \
      case FT_FLOWCAP:                                          \
      case FT_RWGENERIC:                                        \
      case FT_RWIPV6:                                           \
      case FT_RWIPV6ROUTING:                                    \
      case FT_RWNOTROUTED:                                      \
      case FT_RWROUTED:                                         \
      case FT_RWSPLIT:                                          \
      case FT_RWWWW:                                            \
        (stream)->is_silk_flow = 1;                             \
        break;                                                  \
      default:                                                  \
        (stream)->is_silk_flow = 0;                             \
        break;                                                  \
    }


#define streamPathnameIsStderr(sis_s)           \
    (!(strcmp("stderr", (sis_s)->pathname)))

#define streamPathnameIsStdin(sis_s)            \
    (!(strcmp("stdin", (sis_s)->pathname)       \
       && strcmp("-", (sis_s)->pathname)))

#define streamPathnameIsStdout(sis_s)           \
    (!(strcmp("stdout", (sis_s)->pathname)      \
       && strcmp("-", (sis_s)->pathname)))


union stream_block_header_un {
    uint8_t         bytes[4 * sizeof(uint32_t)];
    uint32_t        val[4];
    struct silk3_data_st {
        uint32_t    comp_length;
        uint32_t    uncomp_length;
    }               silk3_data;
    struct silk4_data_st {
        uint32_t    block_id;
        uint32_t    block_length;
        uint32_t    prev_block_length;
        uint32_t    uncomp_length;
    }               silk4_data;
};
typedef union stream_block_header_un stream_block_header_t;



/* LOCAL VARIABLES */

/*
 *    If nonzero, do not attempt process ICMP values in the sPort
 *    field.  This is 0 unless the SILK_ICMP_SPORT_HANDLER envar is
 *    set to "none".  See the detailed note in skStreamReadRecord().
 */
static int silk_icmp_nochange = 0;

#ifdef SILK_CLOBBER_ENVAR
/*
 *    If nonzero, enable clobbering (overwriting) of existing files
 */
static int silk_clobber = 0;
#endif


/* LOCAL FUNCTION PROTOTYPES */

static ssize_t
streamBasicBufSkip(
    skstream_t         *stream,
    size_t              count);
#if SK_ENABLE_ZLIB
static int
streamGZFlush2(
    skstream_t         *stream,
    const int           zflush);
#endif  /* SK_ENABLE_ZLIB */
static int
streamIOBufFlush(
    skstream_t         *stream);
static int
streamIOBufUncompress(
    const skstream_t   *stream,
    void               *dest,
    size_t             *destlen,
    const void         *source,
    size_t              sourcelen);


/* FUNCTION DEFINITIONS */

/*
 *    Set the 'basicbuf' member of 'stream' to use the byte array
 *    'buf', whose total size is 'bufsiz'.  Mark the basicbuf as
 *    having 'avail' bytes of data available, and position the read or
 *    write position accordingly.  Available bytes must start at
 *    offset 0.
 */
static void
streamBasicBufCreate(
    skstream_t         *stream,
    void               *buf,
    size_t              bufsiz,
    size_t              avail)
{
    stream_buffer_t *bb;

    assert(stream);
    assert(stream->fd != -1);
    assert(buf);
    assert(bufsiz);
    assert(avail <= bufsiz);

    bb = &stream->basicbuf;
    bb->b_bufsiz = bufsiz;
    bb->b_buf = (uint8_t *)buf;
    if (SK_IO_WRITE == stream->io_mode) {
        bb->b_pos = bb->b_buf + avail;
        bb->b_avail = bb->b_bufsiz - avail;
    } else {
        bb->b_max = bb->b_bufsiz;
        bb->b_pos = bb->b_buf;
        bb->b_avail = avail;
    }
}


/*
 *    Mark the 'basicbuf' member of 'stream' as no longer valid and
 *    destroy the byte array if it exists.
 */
static void
streamBasicBufDestroy(
    skstream_t         *stream)
{
    assert(stream);

    /* FIXME: consider adding a check for unflushed data */
    free(stream->basicbuf.b_buf);
    memset(&stream->basicbuf, 0, sizeof(stream->basicbuf));
}


/*
 *    Write any bytes in the 'basicbuf' member of 'stream' to the file
 *    descriptor.  Return 0 on success or -1 on failure.
 */
static ssize_t
streamBasicBufFlush(
    skstream_t         *stream)
{
    size_t len;
    ssize_t rv;

    assert(stream);
    assert(stream->basicbuf.b_buf);
    assert(stream->io_mode != SK_IO_READ);

    TRACEMSG(3, ((FMT_FLS "b_avail=%" SK_PRIuZ ", b_pos=%ld,"
                  " b_bufsiz=%" SK_PRIuZ),
                 FLS(stream), stream->basicbuf.b_avail,
                 (stream->basicbuf.b_pos - stream->basicbuf.b_buf),
                 stream->basicbuf.b_bufsiz));

    len = stream->basicbuf.b_pos - stream->basicbuf.b_buf;
    rv = skwriten(stream->fd,  stream->basicbuf.b_buf, len);
    if ((size_t)rv != len) {
        stream->errnum = errno;
        stream->err_info = SKSTREAM_ERR_WRITE;
        TRACEMSG(3, (FMT_FLS "=> -1, errno=%d", FLS(stream), errno));
        return -1;
    }
    /* reset the compression buffer */
    stream->basicbuf.b_pos = stream->basicbuf.b_buf;
    stream->basicbuf.b_avail = stream->basicbuf.b_bufsiz;

    return 0;
}


/*
 *    Copy 'count' bytes from the 'basicbuf' member of 'stream' into
 *    the byte array 'buf'.  Return the number of bytes copied or -1
 *    on failure.
 *
 *    If 'basicbuf' is empty when the function is called or if
 *    'basicbuf' becomes empty while copying data, read from the file
 *    descriptor.  The number of bytes read is the greater of the size
 *    of the 'basicbuf' or the number of bytes remaining to be copied
 *    into 'buf'.
 *
 *    If 'count' if 0 and the 'basicbuf' is empty, data is read from
 *    the file descriptor and 0 is returrned (or -1 on error).
 *
 *    If 'buf' is NULL, move forward 'count' bytes in the basicbuf or
 *    in the file descriptor stream.
 */
static ssize_t
streamBasicBufRead(
    skstream_t         *stream,
    void               *buf,
    size_t              count)
{
    uint8_t *bp = (uint8_t *)buf;
    const size_t wanted = count;
    stream_buffer_t *bb;
    ssize_t saw;
    size_t len;

    assert(stream);
    assert(stream->basicbuf.b_buf);
    assert(stream->io_mode != SK_IO_WRITE);

    bb = &stream->basicbuf;

    TRACEMSG(3, ((FMT_FLS "buf=%p, count=%" SK_PRIuZ ", b_avail=%" SK_PRIuZ
                  ", b_pos=%ld, b_max = %" SK_PRIuZ ", b_bufsiz=%" SK_PRIuZ),
                 FLS(stream), V(buf), count, bb->b_avail,
                 (bb->b_pos - bb->b_buf), bb->b_max, bb->b_bufsiz));

    /* avoid reading data when we can */
    if (NULL == buf && stream->is_seekable && count > bb->b_avail) {
        /* subtract what we have previously read from the number of
         * bytes we want to skip */
        count -= bb->b_avail;
        bb->b_pos += bb->b_avail;
        bb->b_avail = 0;

        saw = streamBasicBufSkip(stream, count);
        if (saw > 0 || (0 == saw && stream->is_seekable)) {
            count -= saw;
            TRACEMSG(3, (FMT_FLS "=> %ld", FLS(stream), wanted - count));
            return wanted - count;
        }
        if (-1 == saw) {
            TRACEMSG(3, (FMT_FLS "=> -1", FLS(stream)));
            return -1;
        }
        /* else stream is not seekable; drop into code below */
    }

    for (;;) {
        if (bb->b_avail) {
            len = ((count <= bb->b_avail) ? count : bb->b_avail);
            if (bp) {
                memcpy(bp, bb->b_pos, len);
                bp += len;
            }
            bb->b_avail -= len;
            bb->b_pos += len;
            count -= len;
            if (0 == count) {
                break;
            }
        }

        /* get data from the underlying file descriptor */
        if (buf && count > bb->b_max) {
            /* read directly into the caller's buffer */
            saw = skreadn(stream->fd, bp, count);
            if (-1 == saw) {
                stream->errnum = errno;
                stream->err_info = SKSTREAM_ERR_READ;
                return saw;
            }
            stream->offset += saw;
            count -= saw;
            break;
        }
        saw = skreadn(stream->fd, bb->b_buf, bb->b_max);
        if (saw <= 0) {
            if (0 == saw) {
                /* there is no more data; return whatever data we
                 * copied into 'buf' above */
                break;
            }
            stream->errnum = errno;
            stream->err_info = SKSTREAM_ERR_READ;
            TRACEMSG(3, (FMT_FLS "=> %" SK_PRIdZ, FLS(stream), saw));
            return saw;
        }
        stream->offset += saw;
        bb->b_avail = saw;
        bb->b_pos = bb->b_buf;
    }
    TRACEMSG(3, (FMT_FLS "=> %ld", FLS(stream), wanted-count));
    return (wanted - count);
}


/*
 *    A wrapper over streamBasicBufRead() that stops copying data into
 *    'buf' at the first occurrence of 'stop_char' in the basicbuf.
 *    The 'stop_char' is copied into 'buf'.
 *
 *    No more than 'count' bytes are copied into 'buf'.
 *
 *    When 'buf' is NULL, move forward 'count' bytes in the basicbuf
 *    on when 'stopchar' is encountered.
 */
static ssize_t
streamBasicBufReadToChar(
    skstream_t         *stream,
    void               *buf,
    size_t              count,
    int                 stop_char)
{
    const void *found_char = NULL;
    uint8_t *bp = (uint8_t *)buf;
    const size_t wanted = count;
    stream_buffer_t *bb;
    ssize_t saw;
    size_t len;

    assert(stream);
    assert(stream->basicbuf.b_buf);
    assert(stream->io_mode != SK_IO_WRITE);

    bb = &stream->basicbuf;

    for (;;) {
        /* ensure there is data in memory; use a length of 0 which
         * reads from the file descriptor but returns no data */
        saw = streamBasicBufRead(stream, buf, 0);
        if (saw < 0) {
            return saw;
        }
        assert(0 == saw);
        if (0 == bb->b_avail) {
            /* no more data */
            break;
        }
        len = ((count <= bb->b_avail) ? count : bb->b_avail);
        if (bp) {
            found_char = memccpy(bp, bb->b_pos, stop_char, len);
            if (found_char) {
                len = (uint8_t *)found_char - bp;
            }
            bp += len;
        } else {
            found_char = memchr(bb->b_pos, stop_char, len);
            if (found_char) {
                len = (uint8_t *)found_char - bb->b_pos + 1;
            }
        }
        bb->b_avail -= len;
        bb->b_pos += len;
        count -= len;
        if (0 == count || found_char) {
            break;
        }
    }
    return (wanted - count);
}


/*
 *    A helper function for streamBasicBufRead().
 *
 *    Use lseek() to move forward 'count' bytes in the file descriptor
 *    stream, stopping at the end of the file if it is reached first.
 *
 *    Return the number of bytes moved.
 *
 *    On an lseek() error, return 0 and clear the 'is_seekable' flag
 *    on 'stream; if the error is EPIPE.  Otherwise store the errno on
 *    'stream' and return -1.
 */
static ssize_t
streamBasicBufSkip(
    skstream_t         *stream,
    size_t              count)
{
    off_t cur;
    off_t end;
    off_t pos;

    assert(stream);
    assert(-1 != stream->fd);
    assert(stream->is_seekable);
    assert(NULL == stream->zlib);
    /* assert(NULL == stream->iobuf.rec_buf.b_buf); */
    assert(SK_IO_WRITE != stream->io_mode);
    assert(0 == stream->basicbuf.b_avail);

    errno = 0;
    /* get the current position */
    cur = lseek(stream->fd, 0, SEEK_CUR);
    if (-1 == cur) {
        if (ESPIPE == errno) {
            /* stream is not seekable; unset the is_seekable flag
             * and return 0 to the caller */
            stream->is_seekable = 0;
            return 0;
        }
        goto ERROR;
    }

    /* note the end of the file */
    end = lseek(stream->fd, 0, SEEK_END);
    if (-1 == end) {
        goto ERROR;
    }
    assert(end >= cur);

    /* seek to desired position; backtrack to end if desired position
     * is beyond the end of the file */
    pos = lseek(stream->fd, cur + count, SEEK_SET);
    if (pos > end) {
        pos = lseek(stream->fd, end, SEEK_SET);
        stream->is_eof = 1;
    }
    if (-1 == pos) {
        goto ERROR;
    }
    return pos - cur;

  ERROR:
    stream->errnum = errno;
    stream->err_info = SKSTREAM_ERR_SYS_LSEEK;
    return -1;
}


/*
 *    Copy 'count' bytes from the byte array 'buf' into the 'basicbuf'
 *    member of 'stream'.  The return value is either 'count' or -1 on
 *    failure.
 *
 *    When fewer than 'count' bytes are available in the basicbuf,
 *    streamBasicBufFlush() is called to empty the 'basicbuf'.
 *
 *    When 'count' is 0 and the 'basicbuf' has no space available,
 *    flush the 'basicbuf' and return 0.  The 'buf' parameter must be
 *    non-NULL even if 'count' is 0.
 *
 *    If 'count' is larger than the size of the 'basicbuf', the
 *    basicbuf is flushed and bytes are written directly from 'buf' to
 *    the file descriptor.
 *
 *    If 'buf' is NULL, move forward 'count' bytes in the basicbuf or
 *    the file descriptor stream.
 */
static ssize_t
streamBasicBufWrite(
    skstream_t         *stream,
    const void         *buf,
    size_t              count)
{
    const uint8_t *bp = (const uint8_t *)buf;
    size_t len;

    assert(stream);
    assert(stream->basicbuf.b_buf);
    assert(stream->io_mode != SK_IO_READ);
    assert(bp);

    TRACEMSG(3, ((FMT_FLS "buf=%p, count=%" SK_PRIuZ ", b_avail=%" SK_PRIuZ
                  ", b_pos=%ld, b_bufsiz=%" SK_PRIuZ),
                 FLS(stream), V(buf), count, stream->basicbuf.b_avail,
                 (stream->basicbuf.b_pos - stream->basicbuf.b_buf),
                 stream->basicbuf.b_bufsiz));

    if (count >= stream->basicbuf.b_bufsiz) {
        ssize_t rv;
        if (streamBasicBufFlush(stream)) {
            TRACEMSG(3, (FMT_FLS "=> -1, errno=%d", FLS(stream), errno));
            return -1;
        }
        rv = skwriten(stream->fd,  bp, count);
        if ((size_t)rv != count) {
            stream->errnum = errno;
            stream->err_info = SKSTREAM_ERR_WRITE;
            TRACEMSG(3, (FMT_FLS "=> -1, errno=%d", FLS(stream), errno));
            return -1;
        }
        TRACEMSG(3, (FMT_FLS "=> %" SK_PRIdZ, FLS(stream), rv));
        return rv;
    }

    do {
        if (stream->basicbuf.b_avail) {
            /* copy data to be written into the buffer */
            len = ((count <= stream->basicbuf.b_avail)
                   ? count
                   : stream->basicbuf.b_avail);
            memcpy(stream->basicbuf.b_pos, bp, len);
            stream->basicbuf.b_avail -= len;
            stream->basicbuf.b_pos += len;
            bp += len;
            if (len == count) {
                TRACEMSG(3, (FMT_FLS "=> %ld", FLS(stream),
                             bp - (const uint8_t *)buf));
                return bp - (const uint8_t *)buf;
            }
            count -= len;
        }
    } while (0 == streamBasicBufFlush(stream));

    TRACEMSG(3, (FMT_FLS "=> -1, errno=%d", FLS(stream), errno));
    return -1;
}


/*
 *  status = streamCheckAttributes(stream, io_mode_mask, content_type_mask);
 *
 *    If 'io_mode_mask' is not zero, verify that the read/write/append
 *    setting of 'stream' is present in 'io_mode_mask'.
 *
 *    If 'content_type_mask' is not zero, verify that the content type
 *    setting of 'stream' is present in 'content_type_mask'.
 */
static int
streamCheckAttributes(
    const skstream_t   *stream,
    unsigned            io_mode_mask,
    unsigned            content_type_mask)
{
    return ((io_mode_mask && !(stream->io_mode & io_mode_mask))
            ? SKSTREAM_ERR_UNSUPPORT_IOMODE
            : ((content_type_mask
                && !(stream->content_type & content_type_mask))
               ? SKSTREAM_ERR_UNSUPPORT_CONTENT
               : SKSTREAM_OK));
}


/*
 *  status = streamCheckModifiable(stream);
 *
 *    Return SKSTREAM_OK if the caller is still allowed to set aspects
 *    of 'stream'; otherwise return the reason why 'stream' cannot be
 *    modified.
 */
static int
streamCheckModifiable(
    const skstream_t   *stream)
{
    return ((stream->is_closed)
            ? SKSTREAM_ERR_CLOSED
            : ((stream->is_dirty)
               ? SKSTREAM_ERR_PREV_DATA
               : SKSTREAM_OK));
}


/*
 *  status = streamCheckOpen(stream);
 *
 *    Call this function on a stream which you expect to be open; it
 *    will return SKSTREAM_OK if 'stream' is open, or an error code
 *    explaining why 'stream' is not open.
 *
 *    A stream that has been opened and closed is neither open nor
 *    unopened.
 */
static int
streamCheckOpen(
    const skstream_t   *stream)
{
    return ((stream->is_closed)
            ? SKSTREAM_ERR_CLOSED
            : ((stream->fd == -1)
               ? SKSTREAM_ERR_NOT_OPEN
               : SKSTREAM_OK));
}


/*
 *  status = streamCheckUnopened(stream);
 *
 *    Call this function on a stream which you expect to be
 *    unopened---i.e., not yet open.  It will return SKSTREAM_OK if
 *    'stream' is unopened, or an error code explaining why 'stream'
 *    is not considered unopened.
 *
 *    A stream that has been opened and closed is neither open nor
 *    unopened.
 */
static int
streamCheckUnopened(
    const skstream_t   *stream)
{
    return ((stream->is_closed)
            ? SKSTREAM_ERR_CLOSED
            : ((stream->fd != -1)
               ? SKSTREAM_ERR_PREV_OPEN
               : SKSTREAM_OK));
}


/*
 *  status = streamGZCheck(stream, &is_compressed);
 *
 *    Set the referent of 'is_compressed' to 1 if the pathname of
 *    'stream' looks like the name of a compressed file, or to 0
 *    otherwise.
 *
 *    Essentially 'is_compressed' is set to 1 when the pathname ends
 *    in ".gz" or when 'stream' is open for read or append and the
 *    pathname contains the substring ".gz."---assuming the pathname
 *    has had a mkstemp() suffix added to it.
 *
 *    If the pathname does not look like a compressed file, return
 *    SKSTREAM_OK.
 *
 *    If the pathname looks like a compressed file, return SKSTREAM_OK
 *    unless:
 *
 *    (1) The stream is open for append; return
 *    SKSTREAM_ERR_UNSUPPORT_IOMODE.
 *
 *    (2) The stream is open for write and contains text; return
 *    SKSTREAM_ERR_UNSUPPORT_CONTENT.
 *
 *    (3) SiLK was compiled without zlib support; return
 *    SKSTREAM_ERR_COMPRESS_UNAVAILABLE.
 */
static int
streamGZCheck(
    skstream_t         *stream,
    int                *is_compressed)
{
    const char *gz;

    assert(stream);
    assert(is_compressed);

    /* check file extension; we want to find "foobar.gz" or
     * "foobar.gz.XXXXXX" via mkstemp() */
    gz = strstr(stream->pathname, ".gz");
    if (gz == NULL
        || (gz[3] != '\0' && gz[3] != '.')
        || (gz[3] == '.' && stream->io_mode == SK_IO_WRITE))
    {
        /* does not look like compressed file */
        *is_compressed = 0;
        return SKSTREAM_OK;
    }
    /* else looks like a compressed file name */
    *is_compressed = 1;

    if (SK_IO_APPEND == stream->io_mode) {
        /* cannot append to a compressed file */
        return SKSTREAM_ERR_UNSUPPORT_IOMODE;
    }
    if (SK_CONTENT_TEXT == stream->content_type
        && SK_IO_WRITE == stream->io_mode)
    {
        /* cannot compress textual output */
        return SKSTREAM_ERR_UNSUPPORT_CONTENT;
    }
#if !SK_ENABLE_ZLIB
    /* compression not supported */
    return SKSTREAM_ERR_COMPRESS_UNAVAILABLE;
#else
    return SKSTREAM_OK;
#endif  /* SK_ENABLE_ZLIB */
}


#if !SK_ENABLE_ZLIB
static int
streamGZNotAvailable(
    int                 line)
{
    skAppPrintErr(
        "zlib function called at %s:%d but %s built without zlib support",
        __FILE__, line, skAppName());
    skAbort();
}

#define streamGZClose(m_1)                      streamGZNotAvailable(__LINE__)
#define streamGZCreate(m_1, m_2, m_3, m_4)      streamGZNotAvailable(__LINE__)
#define streamGZFlush(m_1)                      streamGZNotAvailable(__LINE__)
#define streamGZRead(m_1, m_2, m_3)             streamGZNotAvailable(__LINE__)
#define streamGZReadToChar(m_1, m_2, m_3, m_4)  streamGZNotAvailable(__LINE__)
#define streamGZWrite(m_1, m_2, m_3)            streamGZNotAvailable(__LINE__)
#define streamGZWriteFromPipe(m_1)              streamGZNotAvailable(__LINE__)

#else

/*
 *  status = streamGZClose(stream);
 *
 *    Tell the zlib descriptor associated with 'stream' to completely
 *    flush the buffer and write the end-of-stream marker.
 *
 *    This function does not call inflateEnd() or deflateEnd().
 *
 *    This function is invoked directly by other skstream functions.
 */
static int
streamGZClose(
    skstream_t         *stream)
{
    assert(stream);
    assert(stream->zlib);

    if (SK_IO_READ == stream->io_mode) {
        return SKSTREAM_OK;
    }
    return (int)streamGZFlush2(stream, Z_FINISH);
}


/*
 *    Initialize the zlib library for 'stream' and have it use the
 *    byte array 'buf', having size 'bufsiz' as the compression-side
 *    buffer.
 *
 *    If 'stream' is open for read, the 'avail' parameter provides the
 *    number of bytes currently in 'buf'.  If 'stream' is open for
 *    write, 'avail' must be 0.
 */
static int
streamGZCreate(
    skstream_t         *stream,
    void               *buf,
    size_t              bufsiz,
    size_t              avail)
{
    int zerr;

    assert(stream);
    assert(buf);
    assert(bufsiz);
    assert(avail <= bufsiz);
    assert(stream->fd != -1);
    assert(NULL == stream->iobuf.rec_buf.b_buf);

    assert(stream->io_mode != SK_IO_APPEND);

    stream->is_seekable = 0;
    stream->zlib = sk_alloc(skstream_zlib_t);
    stream->zlib->zstrm.zalloc = Z_NULL;
    stream->zlib->zstrm.zfree = Z_NULL;
    stream->zlib->zstrm.opaque = Z_NULL;
    stream->zlib->zstrm.avail_in = 0;
    stream->zlib->zstrm.next_in = Z_NULL;

    stream->zlib->comp_buf = (uint8_t *)buf;
    stream->zlib->comp_bufsiz = bufsiz;

    if (SK_IO_WRITE == stream->io_mode) {
        if (avail) {
            skAbort();
        }
        /* in fourth argument: 15 to use maximum compresssion buffer;
         * 16 to write to the gzip format */
        zerr = deflateInit2(&stream->zlib->zstrm, Z_DEFAULT_COMPRESSION,
                            Z_DEFLATED, (15 + 16), 8, Z_DEFAULT_STRATEGY);
        if (zerr) {
            stream->errnum = zerr;
            return SKSTREAM_ERR_ZLIB;
        }
        stream->zlib->pos = stream->zlib->uncomp_buf;
        stream->zlib->avail = sizeof(stream->zlib->uncomp_buf);

    } else {
        assert(SK_IO_READ == stream->io_mode);

        /* in fourth argument: 15 to use maximum decompresssion buffer; 16
         * to allow automatic decoding of the gzip format */
        zerr = inflateInit2(&stream->zlib->zstrm, 15 + 16);
        if (zerr) {
            stream->errnum = zerr;
            return SKSTREAM_ERR_ZLIB;
        }
        stream->zlib->zstrm.avail_in = (uInt)avail;
        stream->zlib->zstrm.next_in = stream->zlib->comp_buf;
    }

    return SKSTREAM_OK;
}


/*
 *  status = streamGZFlush(stream);
 *
 *    Tell the zlib descriptor associated with 'stream' to flush any
 *    unwritten data to the stream.
 */
static int
streamGZFlush(
    skstream_t         *stream)
{
    assert(stream);
    assert(stream->zlib);

    if (sizeof(stream->zlib->uncomp_buf) == stream->zlib->avail) {
        assert(stream->zlib->pos == stream->zlib->uncomp_buf);
        return SKSTREAM_OK;
    }
    return (int)streamGZFlush2(stream, Z_SYNC_FLUSH);
}


/*
 *    Helper function for streamGZClose(), streamGZFlush(),
 *    streamGZWrite(), and streamGZWriteFromPipe().
 *
 *    This function calls deflate() to compress the data and it writes
 *    the compressed data to the file descriptor.
 */
static int
streamGZFlush2(
    skstream_t         *stream,
    const int           zflush)
{
    ssize_t rv;
    size_t len;
    int zerr;

    assert(stream);
    assert(stream->zlib);

    /* point the z_stream at the uncompression buffer */
    stream->zlib->zstrm.next_in = stream->zlib->uncomp_buf;
    stream->zlib->zstrm.avail_in
        = sizeof(stream->zlib->uncomp_buf) - stream->zlib->avail;

    do {
        stream->zlib->zstrm.next_out = stream->zlib->comp_buf;
        stream->zlib->zstrm.avail_out = stream->zlib->comp_bufsiz;
        zerr = deflate(&stream->zlib->zstrm, zflush);
        switch (zerr) {
          case Z_OK:
          case Z_STREAM_END:
            break;
          default:
            stream->err_info = SKSTREAM_ERR_ZLIB;
            return -1;
        }
        len = stream->zlib->comp_bufsiz - stream->zlib->zstrm.avail_out;
        rv = skwriten(stream->fd, stream->zlib->comp_buf, len);
        if ((size_t)rv != len) {
            stream->errnum = errno;
            stream->err_info = SKSTREAM_ERR_WRITE;
            return -1;
        }
        stream->offset += rv;
    } while (0 == stream->zlib->zstrm.avail_out);
    assert(0 == stream->zlib->zstrm.avail_in);

    /* reset the uncompression buffer */
    stream->zlib->pos = stream->zlib->uncomp_buf;
    stream->zlib->avail = sizeof(stream->zlib->uncomp_buf);

    return 0;
}


/*
 *  status = streamGZRead(stream, buf, count);
 *
 *    Read 'count' bytes from the zlib descriptor associated with
 *    'stream' and put them into 'buf'.  If 'buf' is NULL, skip
 *    forward 'count' bytes in the stream.
 */
static ssize_t
streamGZRead(
    skstream_t         *stream,
    void               *buf,
    size_t              count)
{
    uint8_t *bp = (uint8_t *)buf;
    const size_t wanted = count;
    ssize_t saw;
    size_t len;
    int zerr;

    assert(stream);
    assert(stream->zlib);
    assert(count < SSIZE_MAX);

    for (;;) {
        if (stream->zlib->avail) {
            /* there is already uncompressed data available */
            len = (count < stream->zlib->avail) ? count : stream->zlib->avail;
            if (bp) {
                memcpy(bp, stream->zlib->pos, len);
                bp += len;
            }
            stream->zlib->avail -= len;
            stream->zlib->pos += len;
            count -= len;
            if (0 == count) {
                return wanted;
            }
        }
        /* else uncompression buffer is empty/exhausted */

        /* set z_stream to use the uncompression buffer */
        stream->zlib->zstrm.next_out = stream->zlib->uncomp_buf;
        stream->zlib->zstrm.avail_out = sizeof(stream->zlib->uncomp_buf);
        stream->zlib->pos = stream->zlib->uncomp_buf;
        stream->zlib->avail = 0;

        do {
            if (stream->zlib->zstrm.avail_in == 0) {
                /* the compression buffer is empty; read compressed
                 * data from the underlying file descriptor */
                saw = skreadn(stream->fd, stream->zlib->comp_buf,
                              stream->zlib->comp_bufsiz);
                if (saw <= 0) {
                    if (0 == saw) {
                        /* there is no more data; return whatever data
                         * we copied into 'buf' above */
                        return (wanted - count);
                    }
                    stream->errnum = errno;
                    stream->err_info = SKSTREAM_ERR_READ;
                    return saw;
                }
                stream->offset += saw;
                stream->zlib->zstrm.avail_in = saw;
                stream->zlib->zstrm.next_in = stream->zlib->comp_buf;
            }

            zerr = inflate(&stream->zlib->zstrm, Z_NO_FLUSH);
            switch (zerr) {
              case Z_OK:
              case Z_STREAM_END:
                break;
              default:
                stream->err_info = SKSTREAM_ERR_ZLIB;
                return -1;
            }
            stream->zlib->avail = (sizeof(stream->zlib->uncomp_buf)
                                   - stream->zlib->zstrm.avail_out);
        } while (0 == stream->zlib->avail);
    }
}


/*
 *    A wrapper over streamGZRead() that stops copying data into 'buf'
 *    at the first occurrence of 'stop_char' in the zlib descriptor.
 *    The 'stop_char' is copied into 'buf'.
 *
 *    No more than 'count' bytes are copied into 'buf'.
 *
 *    When 'buf' is NULL, move forward 'count' bytes in the zlib
 *    descriptor on when 'stopchar' is encountered.
 */
static ssize_t
streamGZReadToChar(
    skstream_t         *stream,
    void               *buf,
    size_t              count,
    int                 stop_char)
{
    const void *found_char = NULL;
    uint8_t *bp = (uint8_t *)buf;
    const size_t wanted = count;
    ssize_t saw;
    size_t len;

    assert(stream);
    assert(stream->zlib);
    assert(stream->io_mode != SK_IO_WRITE);

    for (;;) {
        /* ensure there is data in memory; use a length of 0 which
         * reads from the file descriptor but returns no data */
        saw = streamGZRead(stream, buf, 0);
        if (saw < 0) {
            return saw;
        }
        assert(0 == saw);
        if (0 == stream->zlib->avail) {
            /* no more data */
            break;
        }
        len = (count < stream->zlib->avail) ? count : stream->zlib->avail;
        if (bp) {
            found_char = memccpy(bp, stream->zlib->pos, stop_char, len);
            if (found_char) {
                len = (uint8_t *)found_char - bp;
            }
            bp += len;
        } else {
            found_char = memchr(stream->zlib->pos, stop_char, len);
            if (found_char) {
                len = (uint8_t *)found_char - stream->zlib->pos + 1;
            }
        }
        stream->zlib->avail -= len;
        stream->zlib->pos += len;
        count -= len;
        if (0 == count || found_char) {
            break;
        }
    }
    return (wanted - count);
}


/*
 *  status = streamGZWrite(stream, buf, count);
 *
 *    Write 'count' bytes from 'buf' to the zlib descriptor associated
 *    with 'stream'.  'buf' may not be NULL.
 *
 *    NOTE: If 'count' is 0, take no action and return 0.
 */
static ssize_t
streamGZWrite(
    skstream_t         *stream,
    const void         *buf,
    size_t              count)
{
    const uint8_t *bp = (const uint8_t *)buf;
    size_t len;

    assert(stream);
    assert(stream->zlib);
    assert(count < SSIZE_MAX);

    if (0 == count) {
        return 0;
    }
    assert(bp);

    do {
        if (stream->zlib->avail) {
            /* copy data to be written into the uncompress buffer */
            len = (count < stream->zlib->avail) ? count : stream->zlib->avail;
            memcpy(stream->zlib->pos, bp, len);
            stream->zlib->avail -= len;
            stream->zlib->pos += len;
            bp += len;
            if (len == count) {
                return bp - (const uint8_t *)buf;
            }
            count -= len;
        }
    } while (0 == streamGZFlush2(stream, Z_NO_FLUSH));

    return -1;
}


/*
 *    For interfaces that can only write to a FILE*, this function is
 *    used to read from a pipe(2)---where the other end is the
 *    FILE*---and feed the data to the deflate() method for
 *    compression.
 */
static int
streamGZWriteFromPipe(
    skstream_t         *stream)
{
    ssize_t rv = 1;

    do {
        if (stream->zlib->avail) {
            rv = read(stream->zlib->pipe[0], stream->zlib->pos,
                      stream->zlib->avail);
            if (-1 == rv) {
                if (EWOULDBLOCK == errno) {
                    return SKSTREAM_OK;
                }
                stream->errnum = errno;
                stream->err_info = SKSTREAM_ERR_READ;
                return -1;
            }
            stream->zlib->pos += rv;
            stream->zlib->avail -= rv;
        }

        if (0 == stream->zlib->avail) {
            if (streamGZFlush2(stream, Z_NO_FLUSH)) {
                return -1;
            }
        }
    } while (rv);

    return streamGZFlush2(stream, Z_SYNC_FLUSH);
}
#endif  /* SK_ENABLE_ZLIB */


/*
 *    Copy a BlockBuffer's block header from the BasicBuf or zlib
 *    stream of 'stream' into 'block_hdr'.
 *
 *    Return 0 on success or a stream error code on failure.
 */
static int
streamIOBufBlockHeaderRead(
    skstream_t             *stream,
    stream_block_header_t  *block_hdr)
{
    ssize_t saw;

    assert(stream);
    assert(stream->fd != -1);
    assert(stream->io_mode != SK_IO_WRITE);
    assert(stream->is_silk);
    assert(stream->have_hdr);

    /* read from the handle */
    if (stream->basicbuf.b_buf) {
        saw = streamBasicBufRead(stream, block_hdr->bytes,
                                 stream->iobuf.header_len);
    } else {
        assert(stream->zlib);
        saw = streamGZRead(stream, block_hdr->bytes,
                           stream->iobuf.header_len);
    }

    TRACEMSG(3, ((FMT_FLS "header_len=%u, pos=%" PRId64 ", saw=%" SK_PRIdZ),
                 FLS(stream), stream->iobuf.header_len,
                 (int64_t)lseek(stream->fd, 0, SEEK_CUR), saw));
    switch (saw) {
      case 16:
        block_hdr->val[3] = ntohl(block_hdr->val[3]);
        block_hdr->val[2] = ntohl(block_hdr->val[2]);
        /* FALLTHROUGH */
      case 8:
        block_hdr->val[1] = ntohl(block_hdr->val[1]);
        /* FALLTHROUGH */
      case 4:
        block_hdr->val[0] = ntohl(block_hdr->val[0]);
        break;
      case 0:
        return SKSTREAM_ERR_EOF;
      case -1:
        return stream->err_info;
      default:
        /* short or unusual read */
        stream->errobj.num = saw;
        return SKSTREAM_ERR_BLOCK_SHORT_HDR;
    }

    if (stream->use_block_hdr) {
        TRACEMSG(3, ((FMT_FLS "id=%" PRIx32 ", block_length=%" PRIu32
                      ", uncomp_length=%" PRIu32 ", prev_length=%" PRIu32),
                     FLS(stream),
                     block_hdr->silk4_data.block_id,
                     block_hdr->silk4_data.block_length,
                     block_hdr->silk4_data.uncomp_length,
                     block_hdr->silk4_data.prev_block_length));
        switch (block_hdr->silk4_data.block_id) {
          case STREAM_BLOCK_HDR_DATA:
          case STREAM_BLOCK_HDR_SIDECAR:
            /* data or sidecar block */
            if (stream->iobuf.header_len != (size_t)saw) {
                stream->errobj.num = saw;
                return SKSTREAM_ERR_BLOCK_SHORT_HDR;
            }
            if (block_hdr->silk4_data.block_length < saw) {
                return SKSTREAM_ERR_BLOCK_INVALID_LEN;
            }
            return SKSTREAM_OK;
          case STREAM_BLOCK_HDR_END:
            /* end of stream block */
            if (stream->iobuf.header_len != (size_t)saw) {
                stream->errobj.num = saw;
                return SKSTREAM_ERR_BLOCK_SHORT_HDR;
            }
            if (block_hdr->silk4_data.block_length < saw) {
                return SKSTREAM_ERR_BLOCK_INVALID_LEN;
            }
            return SKSTREAM_ERR_EOF;
          case 0xdeadbeef:
            /* start of a new file */
            /* FIXME: need some way to return the bytes we read to the
             * caller */
            stream->is_eof = 1;
            return 1;
          default:
            /* Unknown ID */
            TRACEMSG(3, ((FMT_FLS "unknown block id %" PRIx32
                          " (%" PRIu32 ")"),
                         FLS(stream),
                         block_hdr->silk4_data.block_id,
                         block_hdr->silk4_data.block_id));
            return SKSTREAM_ERR_BLOCK_UNKNOWN_ID;
        }
    } else if (sizeof(uint32_t) == saw) {
        if (0 == block_hdr->silk3_data.comp_length) {
            /* this is a well-defined EOF */
            return SKSTREAM_ERR_EOF;
        }
        /* short or unusual read */
        stream->errobj.num = saw;
        return SKSTREAM_ERR_BLOCK_SHORT_HDR;

    } else {
        /* verify header sizes look valid */
        TRACEMSG(3, ((FMT_FLS "silk3 block, comp_length=%" PRIu32
                      ", uncomp_length=%" PRIu32),
                     FLS(stream), block_hdr->silk3_data.comp_length,
                     block_hdr->silk3_data.uncomp_length));
        if (block_hdr->silk3_data.comp_length > stream->iobuf.ext_buf.b_bufsiz
            || (block_hdr->silk3_data.uncomp_length
                > stream->iobuf.rec_buf.b_bufsiz))
        {
            return SKSTREAM_ERR_BAD_COMPRESSION_SIZE;
        }
    }

    return SKSTREAM_OK;
}


/*
 *    For a BlockBuffer on a 'stream' that is open for write or
 *    append, ensure that the record and sidecar output buffers have
 *    available at least 'record_size' and 'sidecar_size' empty bytes
 *    for data.  If not, flush the BlockBuffer to disk.  Return
 *    SKSTREAM_OK on success or -1 on failure.
 */
static int
streamIOBufCheckAvail(
    skstream_t         *stream,
    size_t              record_size,
    size_t              sidecar_size)
{
    assert(stream);
    assert(stream->io_mode == SK_IO_WRITE || stream->io_mode == SK_IO_APPEND);
    assert(stream->is_silk_flow);
    assert(stream->have_hdr);
    assert(stream->fd != -1);
    assert(stream->iobuf.rec_buf.b_buf);
    assert(stream->use_block_hdr);

    if (stream->iobuf.rec_buf.b_avail < record_size) {
        if (streamIOBufFlush(stream)) {
            return -1;
        }
        assert(stream->iobuf.rec_buf.b_avail >= record_size);
        assert((NULL == stream->iobuf.sc_buf.b_buf && 0 == sidecar_size)
               || stream->iobuf.sc_buf.b_avail >= sidecar_size);
    }
    if (stream->iobuf.sc_buf.b_buf) {
        assert(stream->sidecar);
        if (stream->iobuf.sc_buf.b_avail < sidecar_size) {
            if (streamIOBufFlush(stream)) {
                return -1;
            }
            assert(stream->iobuf.rec_buf.b_avail >= record_size);
            assert(stream->iobuf.sc_buf.b_avail >= sidecar_size);
        }
    } else if (sidecar_size) {
        return -1;
    }
    return SKSTREAM_OK;
}


/*
 *    Compress the 'sourcelen' bytes of data at 'source' into the
 *    buffer 'dest' using the compression method defined on 'stream'.
 *    The caller must set the referent of 'destlen' to the number of
 *    available bytes in 'dest'.  The function updates the referent to
 *    be the size of the compressed data.
 *
 *    Return 0 on success and -1 on failure.
 */
static int
streamIOBufCompress(
    skstream_t         *stream,
    void               *dest,
    uint32_t           *destlen,
    const void         *source,
    uint32_t            sourcelen)
{
    switch (stream->iobuf.compmethod) {
#if SK_ENABLE_ZLIB
      case SK_COMPMETHOD_ZLIB:
        {
            uLongf dl;
            uLong  sl;
            int    rv;

            assert(sizeof(sl) >= sizeof(sourcelen));
            assert(sizeof(dl) >= sizeof(*destlen));

            sl = sourcelen;
            dl = *destlen;
            rv = compress2((Bytef*)dest, &dl, (const Bytef*)source, sl,
                           stream->iobuf.comp_opts.zlib.level);
            *destlen = dl;

            if (Z_OK != rv) {
                stream->errnum = rv;
                stream->err_info = SKSTREAM_ERR_ZLIB;
                return -1;
            }
            return 0;
        }
#endif  /* SK_ENABLE_ZLIB */

#if SK_ENABLE_LZO
      case SK_COMPMETHOD_LZO1X:
        {
            lzo_uint sl, dl;
            int rv;

            assert(sizeof(sl) >= sizeof(sourcelen));
            assert(sizeof(dl) >= sizeof(*destlen));

            dl = *destlen;
            sl = sourcelen;
            rv = lzo1x_1_15_compress((const unsigned char*)source, sl,
                                     (unsigned char*)dest, &dl,
                                     stream->iobuf.comp_opts.lzo.scratch);
            *destlen = dl;

            return rv;
        }
#endif  /* SK_ENABLE_LZO */

#if SK_ENABLE_SNAPPY
      case SK_COMPMETHOD_SNAPPY:
        {
            size_t sl, dl;
            snappy_status rv;

            assert(sizeof(sl) >= sizeof(sourcelen));
            assert(sizeof(dl) >= sizeof(*destlen));

            dl = *destlen;
            sl = sourcelen;
            rv = snappy_compress((const char*)source, sl, (char*)dest, &dl);
            *destlen = dl;

            return (rv == SNAPPY_OK) ? 0 : -1;
        }
#endif  /* SK_ENABLE_SNAPPY */

      default:
        skAbortBadCase(stream->iobuf.compmethod);
    }
}


/*
 *    Create a BlockBuffer for 'stream'.
 *
 *    When the stream is open for write, the current data in the
 *    BasicBuf is flushed and the BasicBuf is destroyed.
 */
static ssize_t
streamIOBufCreate(
    skstream_t         *stream)
{
    const uint32_t buf_size = SKSTREAM_DEFAULT_BLOCKSIZE;
    stream_block_header_t block_hdr;
    size_t reclen;
    int rv = SKSTREAM_OK;

    assert(stream);
    assert(stream->fd != -1);
    assert(stream->is_silk);
    assert(stream->have_hdr);
    assert(NULL == stream->iobuf.rec_buf.b_buf);
    assert((!stream->basicbuf.b_buf && stream->zlib)
           || (stream->basicbuf.b_buf && !stream->zlib));

    /* quiet unused variable warning */
    (void)block_hdr;

    memset(&stream->iobuf, 0, sizeof(stream->iobuf));

    stream->iobuf.compmethod = skHeaderGetCompressionMethod(stream->silk_hdr);
    stream->iobuf.fileversion = skHeaderGetFileVersion(stream->silk_hdr);
    reclen = stream->rec_len;

    TRACEMSG(2, (FMT_FLS "compmethod=%d, reclen=%zu path='%s'",
                 FLS(stream), stream->iobuf.compmethod, reclen,
                 stream->pathname));

    assert(SK_COMPMETHOD_NONE != stream->iobuf.compmethod
           || SK_FILE_VERSION_BLOCK_HEADER == stream->iobuf.fileversion);

    /* /\* store location where the IOBuf was enabled *\/ */
    /* stream->pre_iobuf_pos = lseek(stream->fd, 0, SEEK_CUR); */

    /* make certain compression method is available---this should not
     * be necessary, but go ahead and check since creating the buffer
     * should not occur often */
    switch (skCompMethodCheck(stream->iobuf.compmethod)) {
      case SK_COMPMETHOD_IS_AVAIL:
        /* known, valid, and available */
        break;
      case SK_COMPMETHOD_IS_VALID:
        /* known and valid but not available */
        rv = SKSTREAM_ERR_COMPRESS_UNAVAILABLE;
        goto END;
      case SK_COMPMETHOD_IS_KNOWN:
        /* should never be undecided at this point */
        skAbort();
      default:
        rv = SKSTREAM_ERR_COMPRESS_INVALID;
        goto END;
    }

    if (SK_FILE_VERSION_BLOCK_HEADER == stream->iobuf.fileversion) {
        stream->iobuf.header_len = sizeof(block_hdr.silk4_data);
        assert(4 * sizeof(uint32_t) == stream->iobuf.header_len);
        stream->use_block_hdr = 1;
    } else {
        stream->iobuf.header_len = sizeof(block_hdr.silk3_data);
        assert(2 * sizeof(uint32_t) == stream->iobuf.header_len);
    }

    stream->iobuf.rec_buf.b_bufsiz = SKSTREAM_DEFAULT_BLOCKSIZE;

    /* When reading, the block header is read into the internal
     * buffer. When writing, the block header is stored in the
     * internal buffer only when the stream is not compressed. */
    if (SK_IO_READ == stream->io_mode
        || SK_COMPMETHOD_NONE == stream->iobuf.compmethod)
    {
        stream->iobuf.rec_buf.b_bufsiz += stream->iobuf.header_len;
        stream->iobuf.rec_buf.b_start = stream->iobuf.header_len;
    }

    stream->iobuf.rec_buf.b_buf
        = (uint8_t *)malloc(stream->iobuf.rec_buf.b_bufsiz);
    if (NULL == stream->iobuf.rec_buf.b_buf) {
        stream->iobuf.rec_buf.b_bufsiz = 0;
        rv = SKSTREAM_ERR_ALLOC;
        goto END;
    }
    if (SK_IO_READ == stream->io_mode) {
        stream->iobuf.rec_buf.b_avail = 0;
        stream->iobuf.rec_buf.b_pos
            = stream->iobuf.rec_buf.b_buf + stream->iobuf.rec_buf.b_bufsiz;
    } else {
        /* set maximum position to an integer multiple of the record
         * size */
        stream->iobuf.rec_buf.b_max
            = stream->iobuf.rec_buf.b_bufsiz - stream->iobuf.rec_buf.b_start;
        stream->iobuf.rec_buf.b_max -= (stream->iobuf.rec_buf.b_max % reclen);

        stream->iobuf.rec_buf.b_avail = stream->iobuf.rec_buf.b_max;
        stream->iobuf.rec_buf.b_pos
            = stream->iobuf.rec_buf.b_buf + stream->iobuf.rec_buf.b_start;
    }

    if (stream->sidecar) {
        /* allocate a sidecar buffer */
        stream->iobuf.sc_buf.b_start = stream->iobuf.rec_buf.b_start;
        stream->iobuf.sc_buf.b_bufsiz = stream->iobuf.rec_buf.b_bufsiz;
        stream->iobuf.sc_buf.b_buf
            = (uint8_t *)malloc(stream->iobuf.sc_buf.b_bufsiz);
        if (NULL == stream->iobuf.sc_buf.b_buf) {
            stream->iobuf.sc_buf.b_bufsiz = 0;
            rv = SKSTREAM_ERR_ALLOC;
            goto END;
        }
        if (SK_IO_READ == stream->io_mode) {
            stream->iobuf.sc_buf.b_avail = 0;
            stream->iobuf.sc_buf.b_pos
                = stream->iobuf.sc_buf.b_buf + stream->iobuf.sc_buf.b_bufsiz;
        } else {
            stream->iobuf.sc_buf.b_max
                = stream->iobuf.sc_buf.b_bufsiz - stream->iobuf.sc_buf.b_start;
            stream->iobuf.sc_buf.b_avail = stream->iobuf.sc_buf.b_max;
            stream->iobuf.sc_buf.b_pos
                = stream->iobuf.sc_buf.b_buf + stream->iobuf.sc_buf.b_start;
        }
    }

    /* compute the size of the external buffer for compressed data */
    switch (stream->iobuf.compmethod) {
      case SK_COMPMETHOD_NONE:
        stream->iobuf.ext_buf.b_bufsiz = 0;
        break;

#if SK_ENABLE_ZLIB
      case SK_COMPMETHOD_ZLIB:
        stream->iobuf.comp_opts.zlib.level = Z_DEFAULT_COMPRESSION;
#ifdef SK_HAVE_COMPRESSBOUND
        stream->iobuf.ext_buf.b_bufsiz = compressBound(buf_size);
#else
        stream->iobuf.ext_buf.b_bufsiz = buf_size + buf_size / 1000 + 12;
#endif
        break;
#endif  /* SK_ENABLE_ZLIB */

#if SK_ENABLE_LZO
      case SK_COMPMETHOD_LZO1X:
        /* The following formula is in the lzo faq:
           http://www.oberhumer.com/opensource/lzo/lzofaq.php */
        stream->iobuf.ext_buf.b_bufsiz
            = (buf_size + (buf_size >> 4) + 64 + 3);
        stream->iobuf.comp_opts.lzo.scratch
            = (uint8_t *)(uint8_t *)calloc(LZO1X_1_15_MEM_COMPRESS, 1);
        break;
#endif  /* SK_ENABLE_LZO */

#if SK_ENABLE_SNAPPY
      case SK_COMPMETHOD_SNAPPY:
        stream->iobuf.ext_buf.b_bufsiz
            = snappy_max_compressed_length(buf_size);
        break;
#endif  /* SK_ENABLE_SNAPPY */

      default:
        skAbortBadCase(stream->iobuf.compmethod);
    }

    /* create the external buffer */
    if (stream->iobuf.ext_buf.b_bufsiz) {
        if (SK_COMPMETHOD_NONE != stream->iobuf.compmethod
            && SK_IO_READ != stream->io_mode)
        {
            stream->iobuf.ext_buf.b_bufsiz += stream->iobuf.header_len;
            stream->iobuf.ext_buf.b_start = stream->iobuf.header_len;
        }
        stream->iobuf.ext_buf.b_buf
            = (uint8_t *)malloc(stream->iobuf.ext_buf.b_bufsiz);
        if (NULL == stream->iobuf.ext_buf.b_buf) {
            stream->err_info = SKSTREAM_ERR_ALLOC;
            return -1;
        }
        stream->iobuf.ext_buf.b_pos
            = stream->iobuf.ext_buf.b_buf + stream->iobuf.ext_buf.b_start;
    }

    if (stream->basicbuf.b_buf) {
        if (SK_IO_READ != stream->io_mode) {
            /* no longer need the basicbuf */
            rv = streamBasicBufFlush(stream);
            if (rv) { return rv; }
            streamBasicBufDestroy(stream);
        } else {
            /* reduce the maximum read sisze of the basicbuf */
            stream->basicbuf.b_max = SKSTREAM_READ_INITIAL >> 1;
#if 0
            /*
             *  FIXME: It used to be that we reduced the size of the
             *  basicbuf here.  However, that code did not account for
             *  the case of the basicbuf having more available data
             *  than the size we reduced it to.  That condition did
             *  not often happen, but I encountered it when processing
             *  a file with a very long header (rwsort created from
             *  over 10,000 files), and the basicbuf had been refilled
             *  after the initial SKSTREAM_READ_INITIAL bytes had been
             *  processed.
             */
            size_t bb_bufsiz = SKSTREAM_READ_INITIAL;
            stream_buffer_t *bb;
            uint8_t *b;

            bb = &stream->basicbuf;
            b = bb->b_buf;
            if (((bb->b_pos - bb->b_buf) + bb->b_avail) > bb_bufsiz) {
                TRACEMSG(1, ("Calling memmove on basicbuf"));
                memmove(bb->b_buf, bb->b_pos, bb->b_avail);
                bb_bufsiz = bb->b_avail;
            }
            bb->b_buf = (uint8_t *)realloc(bb->b_buf, bb_bufsiz);
            if (NULL == bb->b_buf) {
                bb->b_buf = b;
                rv = SKSTREAM_ERR_ALLOC;
                goto END;
            }
            bb->b_pos = bb->b_buf + (bb->b_pos - b);
            bb->b_bufsiz = bb_bufsiz;
#endif  /* 0 */
        }
    }

  END:
    return rv;
}


/*
 *    Destroy the BlockBuffer on 'stream' and clean up any state used
 *    by the compression methods.
 */
static void
streamIOBufDestroy(
    skstream_t         *stream)
{
    switch (stream->iobuf.compmethod) {
#if SK_ENABLE_LZO
      case SK_COMPMETHOD_LZO1X:
        free(stream->iobuf.comp_opts.lzo.scratch);
        stream->iobuf.comp_opts.lzo.scratch = NULL;
        break;
#endif  /* SK_ENABLE_LZO */
      default:
        break;
    }

    free(stream->iobuf.ext_buf.b_buf);
    free(stream->iobuf.rec_buf.b_buf);
    free(stream->iobuf.sc_buf.b_buf);

    memset(&stream->iobuf, 0, sizeof(stream->iobuf));
}


/*
 *    Write the record buffer and the sidecar buffer (if any) from the
 *    BlockBuffer to the stream.  Return 0 on success, -1 on failure.
 */
static int
streamIOBufFlush(
    skstream_t                 *stream)
{
    const uint32_t block_id[] = {
        STREAM_BLOCK_HDR_DATA, STREAM_BLOCK_HDR_SIDECAR
    };
    stream_block_header_t *block_hdr;
    stream_buffer_t *int_buf;
    const uint8_t *bp;
    uint32_t uncomp_size;
    uint32_t comp_size;
    ssize_t rv;
    size_t len;
    unsigned int i;

    assert(stream);
    assert(stream->io_mode == SK_IO_WRITE || stream->io_mode == SK_IO_APPEND);
    assert(stream->is_silk);
    assert(stream->fd != -1);
    assert(stream->iobuf.rec_buf.b_buf);

    /* must flush both of the internal buffers: the record buffer and
     * the sidecar buffer */
    int_buf = &stream->iobuf.rec_buf;
    for (i = 0; i < 2; ++i) {
        if (1 == i) {
            int_buf = &stream->iobuf.sc_buf;
            if (!stream->sidecar) {
                break;
            }
            assert(stream->use_block_hdr);
            assert(int_buf->b_buf);
        }

        assert(int_buf->b_buf + int_buf->b_start <= int_buf->b_pos);
        uncomp_size = (int_buf->b_pos - int_buf->b_buf) - int_buf->b_start;
        TRACEMSG(4, ((FMT_FLS "iobuf contains %" PRIu32 " bytes"),
                     FLS(stream), uncomp_size));
        if (0 == uncomp_size) {
#ifndef NDEBUG
            if (0 == i) {
                assert(0 == ((stream->iobuf.rec_buf.b_pos
                              - stream->iobuf.rec_buf.b_buf)
                             - stream->iobuf.rec_buf.b_start));
            } else if (stream->iobuf.sc_buf.b_buf) {
                assert(0 == ((stream->iobuf.sc_buf.b_pos
                              - stream->iobuf.sc_buf.b_buf)
                             - stream->iobuf.sc_buf.b_start));
            }
#endif  /* NDEBUG */
            assert(int_buf->b_avail == int_buf->b_max);
            continue;
        }

        if (SK_COMPMETHOD_NONE == stream->iobuf.compmethod) {
            assert(stream->use_block_hdr);
            bp = (const uint8_t *)int_buf->b_buf;
            /* len is complete block length, including block header */
            len = int_buf->b_pos - int_buf->b_buf;
            comp_size = 0;
        } else {
            /* Call the compression function */
            assert(0 == int_buf->b_start);
            assert(stream->iobuf.ext_buf.b_start == stream->iobuf.header_len);
            assert(stream->iobuf.ext_buf.b_buf + stream->iobuf.ext_buf.b_start
                   == stream->iobuf.ext_buf.b_pos);
            comp_size = (stream->iobuf.ext_buf.b_bufsiz
                         - stream->iobuf.ext_buf.b_start);
            if (streamIOBufCompress(stream, stream->iobuf.ext_buf.b_pos,
                                    &comp_size, int_buf->b_buf, uncomp_size))
            {
                return -1;
            }
            bp = (const uint8_t *)stream->iobuf.ext_buf.b_buf;
            len = comp_size + stream->iobuf.header_len;
        }

        /* FIXME: Non-Aligned Access */
        block_hdr = (stream_block_header_t *)bp;
        if (stream->use_block_hdr) {
            assert(4 * sizeof(uint32_t) == stream->iobuf.header_len);
            block_hdr->silk4_data.block_id = htonl(block_id[i]);
            block_hdr->silk4_data.block_length = htonl(len);
            block_hdr->silk4_data.prev_block_length
                = htonl(stream->iobuf.prev_block_len);
            block_hdr->silk4_data.uncomp_length = htonl(uncomp_size);
        } else {
            assert(0 == i);
            assert(2 * sizeof(uint32_t) == stream->iobuf.header_len);
            block_hdr->silk3_data.comp_length = htonl(comp_size);
            block_hdr->silk3_data.uncomp_length = htonl(uncomp_size);
        }

        /* Write the compressed data */
#if SK_ENABLE_ZLIB
        if (stream->zlib) {
            rv = streamGZWrite(stream, bp, len);
            if (rv != (ssize_t)len) {
                return -1;
            }
        } else
#endif  /* SK_ENABLE_ZLIB */
        {
            rv = skwriten(stream->fd, bp, len);
            if ((size_t)rv != len) {
                stream->errnum = errno;
                stream->err_info = SKSTREAM_ERR_WRITE;
                return -1;
            }
        }

        int_buf->b_avail = int_buf->b_max;
        int_buf->b_pos = int_buf->b_buf + int_buf->b_start;

        stream->iobuf.prev_block_len = len;
    }

    return 0;
}


/*
 *    Ignore the contents of the BlockBuffer whose header was just
 *    read from 'stream', where 'int_buf' holds the block_header.
 *
 *    This is designed as a helper function for streamIOBufRead() to
 *    ignore sidecar data blocks.
 *
 *    Return SKSTREAM_OK on success, or an error code on failure.
 */
static int
streamIOBufBlockIgnore(
    skstream_t         *stream,
    stream_buffer_t    *int_buf)
{
    stream_block_header_t *block_hdr;
    uint32_t comp_len;
    ssize_t saw;

    assert(stream);
    assert(stream->io_mode == SK_IO_READ);
    assert(stream->is_silk);
    assert(stream->have_hdr);
    assert(stream->fd != -1);
    assert(stream->use_block_hdr);
    assert(int_buf->b_start == stream->iobuf.header_len);
    assert(int_buf->b_buf);

    /* FIXME: Non-Aligned access */
    block_hdr = (stream_block_header_t *)int_buf->b_buf;
    comp_len = (block_hdr->silk4_data.block_length
                - stream->iobuf.header_len);
    assert((SK_COMPMETHOD_NONE != stream->iobuf.compmethod)
           || (comp_len == block_hdr->silk4_data.uncomp_length));

    if (stream->basicbuf.b_buf) {
        saw = streamBasicBufRead(stream, NULL, comp_len);
    } else {
        assert(stream->zlib);
        saw = streamGZRead(stream, NULL, comp_len);
    }
    if (saw < comp_len) {
        if (-1 == saw) {
            return stream->err_info;
        }
        stream->errobj.num = saw;
        return SKSTREAM_ERR_BLOCK_INCOMPLETE;
    }
    return SKSTREAM_OK;
}


/*
 *    Read (or maybe skip) an entire data block from 'stream' into the
 *    BlockBuffer's internal buffer specified by 'int_buf'.
 *
 *    If 'skip_count' is non-NULL and its referent contains a value
 *    larger than the number of uncompressed bytes in the block, the
 *    block is skipped and the referent of 'skip_count' is set to the
 *    number of bytes skipped.  (Skipping uses lseek() if possible;
 *    otherwise it reads the bytes but does not decompress them.)
 *
 *    If 'skip_count' is non-NULL and the block is not large enough to
 *    skip, the block is read and the referent of 'skip_count' is set
 *    to 0.  It is also set to 0 on error.
 *
 *    If 'block_wanted_id' does not match the ID of block that was
 *    read, SKSTREAM_ERR_BLOCK_WANTED_ID is returned and the caller
 *    may use streamIOBufBlockIgnore() to skip the block.
 *
 *    Return SKSTREAM_OK on success, or an error code on failure.
 */
static int
streamIOBufBlockRead(
    skstream_t         *stream,
    stream_buffer_t    *int_buf,
    size_t             *skip_count,
    uint32_t            block_wanted_id)
{
    stream_block_header_t *block_hdr;
    uint8_t *bp;
    uint32_t comp_len;
    uint32_t uncomp_len;
    ssize_t saw;
    ssize_t rv;

    assert(stream);
    assert(stream->io_mode == SK_IO_READ);
    assert(stream->is_silk);
    assert(stream->have_hdr);
    assert(stream->fd != -1);
    assert(int_buf->b_start == stream->iobuf.header_len);
    assert(int_buf->b_buf);

    TRACEMSG(3, (FMT_FLS "int_buf=%p, skip=%ld, wanted=%x",
                 FLS(stream), V(int_buf),(skip_count ? (long)*skip_count : -1),
                 block_wanted_id));

    /* FIXME: Non-Aligned access */
    block_hdr = (stream_block_header_t *)int_buf->b_buf;
    rv = streamIOBufBlockHeaderRead(stream, block_hdr);
    if (rv) {
        goto ERROR;
    }

    if (stream->use_block_hdr) {
        if (block_wanted_id != block_hdr->silk4_data.block_id) {
            TRACEMSG(3, (FMT_FLS "wanted-id %x, found-id %x",
                         FLS(stream), block_wanted_id,
                         block_hdr->silk4_data.block_id));
            rv = SKSTREAM_ERR_BLOCK_WANTED_ID;
            goto ERROR;
        }
        comp_len = (block_hdr->silk4_data.block_length
                    - stream->iobuf.header_len);
        uncomp_len = block_hdr->silk4_data.uncomp_length;
    } else {
        comp_len = block_hdr->silk3_data.comp_length;
        uncomp_len = block_hdr->silk3_data.uncomp_length;
    }

    if (uncomp_len > int_buf->b_bufsiz - int_buf->b_start) {
        TRACEMSG(3, ((FMT_FLS "uncomp_len=%" PRIu32
                      " > bufsize=%" SK_PRIdZ),
                     FLS(stream), uncomp_len,
                     int_buf->b_bufsiz - int_buf->b_start));
        rv = SKSTREAM_ERR_BAD_COMPRESSION_SIZE;
        goto ERROR;
    }
    if (SK_COMPMETHOD_NONE == stream->iobuf.compmethod) {
        if (comp_len != uncomp_len) {
            rv = SKSTREAM_ERR_BAD_COMPRESSION_SIZE;
            goto ERROR;
        }
    } else if (stream->iobuf.ext_buf.b_bufsiz - stream->iobuf.ext_buf.b_start
               < comp_len)
    {
        rv = SKSTREAM_ERR_BAD_COMPRESSION_SIZE;
        goto ERROR;
    }

    /* are we skipping? */
    if (skip_count) {
        if (uncomp_len <= *skip_count) {
            /* we can skip this entire block */
            if (stream->basicbuf.b_buf) {
                saw = streamBasicBufRead(stream, NULL, comp_len);
            } else {
                assert(stream->zlib);
                saw = streamGZRead(stream, NULL, comp_len);
            }
            if (saw < comp_len) {
                if (-1 == saw) {
                    rv = stream->err_info;
                } else {
                    stream->errobj.num = saw;
                    rv = SKSTREAM_ERR_BLOCK_INCOMPLETE;
                }
                goto ERROR;
            }
            *skip_count = uncomp_len;
            TRACEMSG(3, (FMT_FLS "=> 0, skip=%ld",
                         FLS(stream), (skip_count ? (long)*skip_count : -1)));
            return SKSTREAM_OK;
        }
        /* else cannot skip this block */
        *skip_count = 0;
    }

    /* determine which buffer to read data into */
    if (SK_COMPMETHOD_NONE == stream->iobuf.compmethod) {
        assert(stream->use_block_hdr);
        bp = int_buf->b_buf + stream->iobuf.header_len;
    } else {
        assert(stream->iobuf.ext_buf.b_buf);
        bp = stream->iobuf.ext_buf.b_buf;
    }
    assert(bp);

    /* read the block's data */
    if (stream->basicbuf.b_buf) {
        saw = streamBasicBufRead(stream, bp, comp_len);
    } else {
        assert(stream->zlib);
        saw = streamGZRead(stream, bp, comp_len);
    }
    if (saw < comp_len) {
        if (-1 == saw) {
            rv = stream->err_info;
        } else {
            stream->errobj.num = saw;
            rv = SKSTREAM_ERR_BLOCK_INCOMPLETE;
        }
        goto ERROR;
    }

    /* set the internal buffer to the data */
    if (SK_COMPMETHOD_NONE == stream->iobuf.compmethod) {
        assert(saw == uncomp_len && saw == comp_len);
        assert(bp - int_buf->b_buf == (ssize_t)int_buf->b_start);
        int_buf->b_avail = uncomp_len;
        int_buf->b_pos = bp;
    } else {
        /* uncompress */
        int_buf->b_avail = int_buf->b_bufsiz - int_buf->b_start;
        if (streamIOBufUncompress(stream, int_buf->b_buf + int_buf->b_start,
                                  &int_buf->b_avail, bp, comp_len))
        {
            rv = SKSTREAM_ERR_BLOCK_UNCOMPRESS;
            goto ERROR;
        }
        /* verify uncompressed block's size */
        if (int_buf->b_avail != uncomp_len) {
            rv = SKSTREAM_ERR_BLOCK_UNCOMPRESS;
            goto ERROR;
        }
        int_buf->b_pos = int_buf->b_buf + int_buf->b_start;
    }
    TRACEMSG(3, (FMT_FLS "=> 0, skip=%ld",
                 FLS(stream), (skip_count ? (long)*skip_count : -1)));
    return SKSTREAM_OK;

  ERROR:
    if (skip_count) {
        *skip_count = 0;
    }
    TRACEMSG(3, (FMT_FLS "=> %" SK_PRIdZ ", skip=%ld",
                 FLS(stream), rv, (skip_count ? (long)*skip_count : -1)));
    return rv;
}


/*
 *    Read 'count' bytes from the BlockBuffer on 'stream' into 'buf'
 *    or if 'buf' is NULL, move 'count' bytes forward in the stream.
 *    Return the number of bytes copied or moved, or return -1 on
 *    error.
 *
 *    This function skips sidecar blocks that appear in 'stream'.
 */
static ssize_t
streamIOBufRead(
    skstream_t         *stream,
    void               *buf,
    size_t              count)
{
    uint8_t *bp = (uint8_t *)buf;
    const size_t wanted = count;
    size_t len;
    ssize_t rv = SKSTREAM_OK;

    assert(stream);
    assert(stream->io_mode == SK_IO_READ);
    assert(stream->is_silk);
    assert(stream->have_hdr);
    assert(stream->fd != -1);
    assert(stream->iobuf.rec_buf.b_buf);

    TRACEMSG(3, (FMT_FLS "buf=%p, count=%" SK_PRIuZ,
                 FLS(stream), V(buf), count));

    for (;;) {
        if (stream->iobuf.rec_buf.b_avail) {
            len = ((count < stream->iobuf.rec_buf.b_avail)
                   ? count
                   : stream->iobuf.rec_buf.b_avail);
            if (bp) {
                memcpy(bp, stream->iobuf.rec_buf.b_pos, len);
                bp += len;
            }
            count -= len;
            stream->iobuf.rec_buf.b_avail -= len;
            stream->iobuf.rec_buf.b_pos += len;
            if (0 == count) {
                TRACEMSG(3, (FMT_FLS "=> %" SK_PRIdZ,
                             FLS(stream), wanted - count));
                return wanted - count;
            }
        }
        if (bp) {
            rv = streamIOBufBlockRead(stream, &stream->iobuf.rec_buf,
                                      NULL, STREAM_BLOCK_HDR_DATA);
        } else {
            /* skip reading/uncompressing when possible */
            len = count;
            rv = streamIOBufBlockRead(stream, &stream->iobuf.rec_buf,
                                      &len, STREAM_BLOCK_HDR_DATA);
            count -= len;
        }
        if (rv) {
            if (SKSTREAM_ERR_BLOCK_WANTED_ID == rv) {
                /* this function ignores sidecar blocks */
                rv = streamIOBufBlockIgnore(stream, &stream->iobuf.rec_buf);
            }
            if (rv) {
                stream->err_info = rv;
                if (wanted != count) {
                    TRACEMSG(3, (FMT_FLS "=> %" SK_PRIdZ,
                                 FLS(stream), wanted - count));
                    return wanted - count;
                }
                if (SKSTREAM_ERR_EOF == rv) {
                    TRACEMSG(3, (FMT_FLS "=> 0", FLS(stream)));
                    return 0;
                }
                TRACEMSG(3, (FMT_FLS "=> -1", FLS(stream)));
                return -1;
            }
        }
    }
}


/*
 *    Uncompress the 'sourcelen' bytes of data at 'source' into the
 *    buffer 'dest' using the compression method defined on 'stream'.
 *    The caller must set the referent of 'destlen' to the number of
 *    available bytes in 'dest'.  The function updates the referent to
 *    be the size of the uncompressed data.
 */
static int
streamIOBufUncompress(
    const skstream_t           *stream,
    void                       *dest,
    size_t                     *destlen,
    const void                 *source,
    size_t                      sourcelen)
{
    switch (stream->iobuf.compmethod) {
#if SK_ENABLE_ZLIB
      case SK_COMPMETHOD_ZLIB:
        {
            uLongf dl;
            uLong  sl;
            int    rv;

            assert(sizeof(sl) >= sizeof(sourcelen));
            assert(sizeof(dl) >= sizeof(*destlen));

            sl = sourcelen;
            dl = *destlen;
            rv = uncompress((Bytef*)dest, &dl, (Bytef*)source, sl);
            *destlen = dl;

            return (rv == Z_OK) ? 0 : -1;
        }
#endif  /* SK_ENABLE_ZLIB */

#if SK_ENABLE_LZO
      case SK_COMPMETHOD_LZO1X:
        {
            lzo_uint sl, dl;
            int rv;

            assert(sizeof(sl) >= sizeof(sourcelen));
            assert(sizeof(dl) >= sizeof(*destlen));

            dl = *destlen;
            sl = sourcelen;
#ifdef SK_HAVE_LZO1X_DECOMPRESS_ASM_FAST_SAFE
            rv = lzo1x_decompress_asm_fast_safe(source, sl, dest, &dl, NULL);
#else
            rv = lzo1x_decompress_safe((const unsigned char*)source, sl,
                                       (unsigned char*)dest, &dl, NULL);
#endif
            *destlen = dl;

            return rv;
        }
#endif  /* SK_ENABLE_LZO */


#if SK_ENABLE_SNAPPY
      case SK_COMPMETHOD_SNAPPY:
        {
            snappy_status rv;
            rv = snappy_uncompress((const char*)source, sourcelen,
                                   (char*)dest, destlen);
            return (rv == SNAPPY_OK) ? 0 : -1;
        }
        break;
#endif  /* SK_ENABLE_SNAPPY */

      default:
        skAbortBadCase(stream->iobuf.compmethod);
    }
}


/*
 *    Write 'count' bytes from 'buf' to 'stream'.  Return the number
 *    of bytes written.  Unless an error is encountered, the return
 *    value is never less than 'count'.
 *
 *    Return -1 on error and store the error code on 'stream'.
 *
 *    Do not store the return value in last_rv.
 */
static ssize_t
streamIOBufWrite(
    skstream_t         *stream,
    const void         *buf,
    size_t              count)
{
    const uint8_t *bp = (const uint8_t *)buf;
    size_t len;

    assert(stream);
    assert(stream->iobuf.rec_buf.b_buf);
    assert(stream->io_mode == SK_IO_WRITE || stream->io_mode == SK_IO_APPEND);
    assert(stream->is_silk);
    assert(stream->is_binary);
    assert(stream->fd != -1);
    assert(stream->have_hdr);
    assert(!stream->sidecar);
    assert(buf);

    if (0 == count) {
        return 0;
    }
    assert(bp);

    TRACEMSG(3, (FMT_FLS "buf=%p, count=%" SK_PRIuZ,
                 FLS(stream), V(buf), count));

    do {
        /* write the internal buffer if no room is available */
        if (stream->iobuf.rec_buf.b_avail) {
            /* number of bytes to copy into the buffer */
            len = ((count < stream->iobuf.rec_buf.b_avail)
                   ? count
                   : stream->iobuf.rec_buf.b_avail);
            memcpy(stream->iobuf.rec_buf.b_pos, bp, len);
            stream->iobuf.rec_buf.b_avail -= len;
            stream->iobuf.rec_buf.b_pos += len;
            bp += len;
            if (len == count) {
                TRACEMSG(3, (FMT_FLS "=> %ld", FLS(stream),
                             bp - (const uint8_t *)buf));
                return bp - (const uint8_t *)buf;
            }
            count -= len;
        }
    } while (0 == streamIOBufFlush(stream));

    TRACEMSG(3, (FMT_FLS "=> -1", FLS(stream)));
    return -1;
}




/*
 *  status = streamOpenAppend(stream);
 *
 *    Open the stream for appending.
 */
static int
streamOpenAppend(
    skstream_t         *stream)
{
    int flags = O_RDWR | O_APPEND;

    assert(stream);
    assert(stream->pathname);

    /* Open file for read and write; position at start. */
    stream->fd = open(stream->pathname, flags, 0);
    if (stream->fd == -1) {
        stream->errnum = errno;
        return SKSTREAM_ERR_SYS_OPEN;
    }
    if (-1 == lseek(stream->fd, 0, SEEK_SET)) {
        stream->errnum = errno;
        return SKSTREAM_ERR_SYS_LSEEK;
    }

    return SKSTREAM_OK;
}


/*
 *  status = streamOpenRead(stream);
 *
 *    Open the stream for reading.
 */
static int
streamOpenRead(
    skstream_t         *stream)
{
    int rv = SKSTREAM_OK;

    assert(stream);
    assert(stream->pathname);
    assert(stream->io_mode == SK_IO_READ);
    assert(-1 == stream->fd);

    if (stream->is_mpi) {
        /* for now, just set to a valid value.  we should replace the
         * checks of 'fd' with an 'is_open' flag */
        stream->fd = INT32_MAX;
    } else if (streamPathnameIsStdin(stream)) {
        stream->fd = STDIN_FILENO;
        stream->is_stdio = 1;
    } else {
        stream->fd = open(stream->pathname, O_RDONLY);
        if (stream->fd == -1) {
            rv = SKSTREAM_ERR_SYS_OPEN;
            stream->errnum = errno;
            goto END;
        }
    }

    assert(SKSTREAM_OK == rv);
  END:
    /* if something went wrong, close the file */
    if (rv != SKSTREAM_OK) {
        if (stream->fd != -1) {
            close(stream->fd);
            stream->fd = -1;
        }
    }
    return rv;
}


static int
streamOpenWrite(
    skstream_t         *stream)
{
    int rv = SKSTREAM_OK;

    assert(stream);
    assert(stream->pathname);
    assert(stream->io_mode == SK_IO_WRITE);

    if (streamPathnameIsStdout(stream)) {
        stream->fd = STDOUT_FILENO;
        stream->is_stdio = 1;
    } else if (streamPathnameIsStderr(stream)) {
        stream->fd = STDERR_FILENO;
        stream->is_stdio = 1;
    } else if (stream->is_mpi) {
        /* for now, just set to a valid value.  we should replace the
         * checks of 'fd' with an 'is_open' flag */
        stream->fd = INT32_MAX;
    } else {
        struct stat stbuf;
        int mode, flags;

        /* standard mode of 0666 */
        mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH;

        /* assume creating previously non-existent file */
        flags = O_WRONLY | O_CREAT | O_EXCL;

        /* try to open as a brand new file */
        stream->fd = open(stream->pathname, flags, mode);
        if (stream->fd == -1) {
            stream->errnum = errno;
            if ((stream->errnum == EEXIST)
                && (0 == stat(stream->pathname, &stbuf)))
            {
                /* file exists.  Try again with different flags when
                 * the file is a FIFO, the file is a character device
                 * ("/dev/null"), or the SILK_CLOBBER envar is set. */
                if (S_ISFIFO(stbuf.st_mode)) {
                    flags = O_WRONLY;
                } else if (S_ISCHR(stbuf.st_mode)) {
                    flags = O_WRONLY | O_NOCTTY;
#ifdef SILK_CLOBBER_ENVAR
                } else if (silk_clobber) {
                    /* overwrite an existing file */
                    flags = O_WRONLY | O_TRUNC;
#endif  /* SILK_CLOBBER_ENVAR */
                } else {
                    rv = SKSTREAM_ERR_FILE_EXISTS;
                    goto END;
                }

                /* try again with the new flags */
                stream->fd = open(stream->pathname, flags, mode);
            }

            /* if we (still) have an error, return */
            if (stream->fd == -1) {
                /* we set errnum above */
                rv = SKSTREAM_ERR_SYS_OPEN;
                goto END;
            }
        }
    }

    assert(SKSTREAM_OK == rv);
  END:
    return rv;
}


static int
streamPostOpen(
    skstream_t         *stream)
{
    int rv = SKSTREAM_OK;

    assert(stream);
    assert(stream->fd != -1);

    TRACEMSG(2, (FMT_FLS "io_mode=%d, fd=%d, path='%s'",
                 FLS(stream), stream->io_mode, stream->fd, stream->pathname));

    if (!stream->is_mpi) {
        if (isatty(stream->fd)) {
            stream->is_terminal = 1;
        } else if (lseek(stream->fd, 0, SEEK_CUR) != (off_t)-1) {
            stream->is_seekable = 1;
        }
    }

#if 0
    /* for a binary output file (that does not contains SiLK data),
     * create the output buffer now */
    if (stream->content_type == SK_CONTENT_OTHERBINARY
        && stream->io_mode != SK_IO_READ)
    {
        if (stream->io_mode == SK_IO_APPEND) {
            /* disallowed by skStreamCreate() */
            skAbort();
            /* if (-1 == lseek(stream->fd, 0, SEEK_END)) { */
            /*     stream->errnum = errno; */
            /*     return SKSTREAM_ERR_SYS_LSEEK; */
            /* } */
        }
        stream->is_dirty = 1;
        rv = streamGZCheck(stream, 1, NULL, NULL);
        if (rv) { goto END; }
        if (NULL == stream->zlib) {
            streamBasicBufCreate(stream, SKSTREAM_DEFAULT_BLOCKSIZE, NULL, 0);
        }
    }

    /* for a SiLK file open for write, create the IOBuf now */
    if (stream->is_silk && stream->io_mode == SK_IO_WRITE) {
        rv = streamIOBufCreate(stream, SK_COMPMETHOD_NONE, 1);
        if (rv) { goto END; }
    }
#endif

    assert(SKSTREAM_OK == rv);
/*  END: */
    return rv;
}


/*
 *    Read SKSTREAM_READ_INITIAL bytes from the file descriptor for
 *    'stream' and check whether the content appears to be compressed.
 *    If it is compressed, create an zlib stream, otherwise create a
 *    BasicBuf.
 *
 *    Return SKSTREAM_OK unless an allocation error occurs, the stream
 *    is compressed and SiLK does not have zlib support, or a read
 *    error occurs.
 */
static int
streamReadPrepare(
    skstream_t         *stream)
{
    uint16_t magic;
    uint8_t *buf;
    size_t bufsiz;
    ssize_t saw;
    int rv;

    assert(stream);
    assert(stream->io_mode == SK_IO_READ || stream->io_mode == SK_IO_APPEND);
    assert(stream->fd != -1);
    assert(NULL == stream->zlib);
    assert(NULL == stream->basicbuf.b_buf);
    assert(NULL == stream->iobuf.rec_buf.b_buf);

    bufsiz = SKSTREAM_DEFAULT_BLOCKSIZE;
    buf = (uint8_t *)malloc(bufsiz);
    if (NULL == buf) {
        return SKSTREAM_ERR_ALLOC;
    }

    rv = SKSTREAM_OK;
    stream->is_dirty = 1;
    saw = skreadn(stream->fd, buf, SKSTREAM_READ_INITIAL);
    /* check whether stream is compressed by an external library */
    if (saw >= (ssize_t)sizeof(magic)) {
        memcpy(&magic, buf, sizeof(magic));
        if (STREAM_MAGIC_NUMBER_GZIP == ntohs(magic)) {
#if SK_ENABLE_ZLIB
            return streamGZCreate(stream, buf, bufsiz, saw);
#else
            rv = SKSTREAM_ERR_COMPRESS_UNAVAILABLE;
#endif  /* SK_ENABLE_ZLIB */
        }
    } else if (saw <= 0) {
        if (0 == saw) {
            stream->is_eof = 1;
        } else {
            saw = 0;
            stream->errnum = errno;
            rv = SKSTREAM_ERR_READ;
        }
    }
    streamBasicBufCreate(stream, buf, bufsiz, saw);
    return rv;
}


#if 0
static int
streamRecordSidecarWrite(
    skstream_t         *stream,
    const rwRec        *rwrec)
{
    lua_State *L;
    size_t buflen;
    int rv;

    L = rwrec->lua_state;

    if (rwRecGetSidecar(rwrec) == LUA_NOREF) {
        lua_pushnil(L);
    } else {
        lua_rawgeti(L, LUA_REGISTRYINDEX, rwRecGetSidecar(rwrec));
    }

    buflen = stream->sidecar_buf.bufsiz - stream->sidecar_buf.pos;
    for (;;) {
        rv = (sk_sidecar_serialize_data(
                  stream->sidecar, L, lua_gettop(L),
                  &stream->sidecar_buf.buffer[stream->sidecar_buf.pos],
                  &buflen));
        if (SK_SIDECAR_OK == rv) {
            break;
        }
        if (SK_SIDECAR_E_NO_SPACE != rv) {
            goto END;
        }
        /* either grow buffer or flush records */
    }

  END:
    lua_pop(L, 1);
    return rv;
}
#endif  /* 0 */


/*
 *    Update 'stream' with the sensor, type, and starting-hour stored
 *    in the stream's header if 'stream' is bound to a packed hourly
 *    data file.
 */
static void
streamSilkFlowCacheHeader(
    skstream_t         *stream)
{
    union h_un {
        sk_header_entry_t          *he;
        sk_hentry_packedfile_t     *pf;
    } h;

    assert(stream);
    assert(stream->is_silk_flow);
    assert(stream->silk_hdr);

    h.he = skHeaderGetFirstMatch(stream->silk_hdr, SK_HENTRY_PACKEDFILE_ID);
    if (h.he) {
        stream->silkflow.hdr_starttime = skHentryPackedfileGetStartTime(h.pf);
        stream->silkflow.hdr_sensor    = skHentryPackedfileGetSensorID(h.pf);
        stream->silkflow.hdr_flowtype  = skHentryPackedfileGetFlowtypeID(h.pf);
    }
}


/*
 *    Invoke the SiLK Flow file format-specific function that sets the
 *    silkflow.unpack() and silkflow.pack() function pointers on 'stream'.
 */
static int
streamSilkFlowPrepare(
    skstream_t         *stream)
{
    int rv;

    assert(stream);
    assert(stream->is_silk);
    assert(stream->silk_hdr);

    switch (skHeaderGetFileFormat(stream->silk_hdr)) {
      case FT_RWAUGMENTED:
        rv = augmentedioPrepare(stream);
        break;

      case FT_RWAUGROUTING:
        rv = augroutingioPrepare(stream);
        break;

      case FT_RWAUGWEB:
        rv = augwebioPrepare(stream);
        break;

      case FT_RWAUGSNMPOUT:
        rv = augsnmpoutioPrepare(stream);
        break;

      case FT_RWFILTER:
        rv = filterioPrepare(stream);
        break;

      case FT_FLOWCAP:
        rv = flowcapioPrepare(stream);
        break;

      case FT_RWGENERIC:
        rv = genericioPrepare(stream);
        break;

      case FT_RWIPV6:
        stream->supports_ipv6 = 1;
        rv = ipv6ioPrepare(stream);
        break;

      case FT_RWIPV6ROUTING:
        stream->supports_ipv6 = 1;
        rv = ipv6routingioPrepare(stream);
        break;

      case FT_RWNOTROUTED:
        rv = notroutedioPrepare(stream);
        break;

      case FT_RWROUTED:
        rv = routedioPrepare(stream);
        break;

      case FT_RWSPLIT:
        rv = splitioPrepare(stream);
        break;

      case FT_RWWWW:
        rv = wwwioPrepare(stream);
        break;

      default:
        return SKSTREAM_ERR_UNSUPPORT_FORMAT;
    }

    if (rv) {
        return rv;
    }

    return SKSTREAM_OK;
}


/*
 *    Read the stream's SiLK header.  When 'only_start' is true, read
 *    only the start of the header (that is, not the variable-sized
 *    entires).  Otherwise read the entire header or the remainder of
 *    the header if the start of the header was read previously.
 *
 *    This is primarily a helper function for skStreamReadSilkHeader()
 *    and skStreamReadSilkHeaderStart(), but it is also called by
 *    other internal functions.
 */
static int
streamSilkHeaderRead(
    skstream_t         *stream,
    unsigned            only_start)
{
    int rv;
    sk_file_header_t *hdr;
    sk_header_lock_t locked;

    assert(stream);

    rv = streamCheckOpen(stream);
    if (rv) { return rv; }

    rv = streamCheckAttributes(stream, (SK_IO_READ | SK_IO_APPEND),
                               (SK_CONTENT_SILK | SK_CONTENT_SILK_FLOW));
    if (rv) { return rv; }

    assert(stream->silk_hdr);
    hdr = stream->silk_hdr;

    locked = skHeaderGetLockStatus(hdr);

    if (only_start && locked != SKHDR_LOCK_MODIFIABLE) {
        return SKSTREAM_ERR_PREV_DATA;
    }

    /* only read the header one time */
    if (stream->have_hdr) {
        return SKSTREAM_OK;
    }

    if (!stream->is_dirty) {
        rv = streamReadPrepare(stream);
        if (rv) { return rv; }
    }

    if (locked == SKHDR_LOCK_MODIFIABLE) {
        rv = skHeaderReadStart(stream, hdr);
        if (rv) { return rv; }

        /* check whether this stream contains flow data */
        if (SK_CONTENT_SILK_FLOW == stream->content_type) {
            STREAM_SET_IS_SILK_FLOW(stream);
            if (!stream->is_silk_flow) {
                return SKSTREAM_ERR_REQUIRE_SILK_FLOW;
            }
        }
        assert((SK_CONTENT_SILK == stream->content_type
                && !stream->is_silk_flow)
               || (SK_CONTENT_SILK_FLOW == stream->content_type
                   && stream->is_silk_flow));
        skHeaderSetLock(hdr, SKHDR_LOCK_ENTRY_OK);
    }

    if (only_start) {
        return SKSTREAM_OK;
    }

    rv = skHeaderReadEntries(stream, hdr);
    if (rv) { return rv; }

    skHeaderSetLock(hdr, SKHDR_LOCK_FIXED);

    if (stream->is_silk_flow) {
        /* swap bytes? */
        assert(SK_CONTENT_SILK_FLOW == stream->content_type);
        stream->swap_flag = !skHeaderIsNativeByteOrder(hdr);

        /* Cache values from the packedfile header */
        streamSilkFlowCacheHeader(stream);

        /* Create sidecar object if header contains sidecar entry */
        if (skHeaderGetFileVersion(stream->silk_hdr)
            == SK_FILE_VERSION_BLOCK_HEADER)
        {
            int err;

            stream->sidecar
                = sk_sidecar_create_from_header(stream->silk_hdr, &err);
            if (SK_SIDECAR_OK != err) {
                return SKSTREAM_ERR_ALLOC;
            }
        }

        /* Set pointers to the PackFn and UnpackFn functions for this
         * file format. */
        rv = streamSilkFlowPrepare(stream);
        if (rv) { return rv; }

        assert(stream->rec_len > 0);
        assert(stream->rec_len <= SK_MAX_RECORD_SIZE);
    }

    /* we have the complete header */
    stream->have_hdr = 1;

    /* If stream is open for append, seek to end of file and set up
     * the basicbuf for output */
    if (stream->io_mode == SK_IO_APPEND) {
        if (-1 == lseek(stream->fd, 0, SEEK_END)) {
            stream->errnum = errno;
            return SKSTREAM_ERR_SYS_LSEEK;
        }
        assert(NULL == stream->zlib);
        assert(stream->basicbuf.b_buf);
        stream->basicbuf.b_pos = stream->basicbuf.b_buf;
        stream->basicbuf.b_avail = stream->basicbuf.b_bufsiz;
    }

    if ((skHeaderGetFileVersion(hdr) == SK_FILE_VERSION_BLOCK_HEADER)
        || (skHeaderGetCompressionMethod(hdr) != SK_COMPMETHOD_NONE))
    {
        rv = streamIOBufCreate(stream);
        if (rv) { return rv; }
    }

    return SKSTREAM_OK;
}


/*
 *    If a pager has been set on 'stream' and 'stream' is connected to
 *    a terminal, invoke the pager.
 */
static int
streamTextInvokePager(
    skstream_t         *stream)
{
    int rv;
    pid_t pid;
    int wait_status;

    rv = streamCheckModifiable(stream);
    if (rv) { goto END; }

    assert(streamCheckAttributes(stream, SK_IO_WRITE, SK_CONTENT_TEXT)
           == SKSTREAM_OK);

    if (stream->pager == NULL) {
        goto END;
    }

    if ( !stream->is_terminal) {
        goto END;
    }

#if 1
    /* invoke the pager */
    stream->fp = popen(stream->pager, "w");
    if (NULL == stream->fp) {
        rv = SKSTREAM_ERR_NOPAGER;
        goto END;
    }

    /* see if pager started.  There is a race condition here, and this
     * assumes we have only one child, which should be true. */
    pid = wait4(0, &wait_status, WNOHANG, NULL);
    if (pid) {
        rv = SKSTREAM_ERR_NOPAGER;
        goto END;
    }
#else
    {
    int pipe_des[2];

    /* create pipe and fork */
    if (pipe(pipe_des) == -1) {
        stream->errnum = errno;
        rv = SKSTREAM_ERR_SYS_PIPE;
        goto END;
    }
    pid = fork();
    if (pid < 0) {
        stream->errnum = errno;
        rv = SKSTREAM_ERR_SYS_FORK;
        goto END;
    }

    if (pid == 0) {
        /* CHILD */

        /* close output side of pipe; set input to stdin */
        close(pipe_des[1]);
        if (pipe_des[0] != STDIN_FILENO) {
            dup2(pipe_des[0], STDIN_FILENO);
            close(pipe_des[0]);
        }

        /* invoke pager */
        execlp(pager, NULL);
        skAppPrintErr("Unable to invoke pager '%s': %s",
                      pager, strerror(errno));
        _exit(EXIT_FAILURE);
    }

    /* PARENT */

    /* close input side of pipe */
    close(pipe_des[0]);

    /* try to open the write side of the pipe */
    out = fdopen(pipe_des[1], "w");
    if (NULL == out) {
        stream->errnum = errno;
        rv = SKSTREAM_ERR_SYS_FDOPEN;
        goto END;
    }

    /* it'd be nice to have a slight pause here to give child time to
     * die if command cannot be exec'ed, but it's not worth the
     * trouble to use select(), and sleep(1) is too long. */

    /* see if child died unexpectedly */
    if (waitpid(pid, &wait_status, WNOHANG)) {
        rv = SKSTREAM_ERR_NOPAGER;
        goto END;
    }
    }
#endif /* 1: whether to use popen() */

    /* looks good. */
    stream->is_pager_active = 1;

    assert(SKSTREAM_OK == rv);
  END:
    return rv;
}


/*
 *    Prepare 'stream' for reading textual input or writing textual output.
 *
 *    For processing textual input, create an IO Buf, and support
 *    reading a stream compressed with gzip.
 *
 *    For textual output, invoke the pager if one is defined for the
 *    stream.  Otherwise, use fdopen() to get a file pointer for the
 *    file descriptor.
 */
static int
streamTextPrepare(
    skstream_t         *stream)
{
    const char *mode = NULL;
    int rv;

    assert(stream);
    assert(!stream->is_binary);
    assert(stream->fd != -1);

    if (stream->is_dirty) {
        return SKSTREAM_OK;
    }
    assert(stream->fp == NULL);

    switch (stream->io_mode) {
      case SK_IO_READ:
        rv = streamReadPrepare(stream);
        if (rv) {
            return rv;
        }
        break;

      case SK_IO_WRITE:
        if (stream->pager) {
            rv = streamTextInvokePager(stream);
            if (rv) {
                return rv;
            }
        }
        if (stream->fp == NULL) {
            mode = "w";
        }
        break;

      case SK_IO_APPEND:
        /* prevented by skStreamCreate() */
        skAbort();
        /* mode = "r+"; */
        /* break; */
    }

    if (mode) {
        stream->fp = fdopen(stream->fd, mode);
        if (stream->fp == NULL) {
            stream->errnum = errno;
            return SKSTREAM_ERR_SYS_FDOPEN;
        }
    }

    stream->is_dirty = 1;

    return SKSTREAM_OK;
}


/*
 *    If the pathname of 'stream' appears to name a compressed file,
 *    create an zlib stream, otherwise create a BasicBuf.
 *
 *    Return SKSTREAM_OK unless an allocation error occurs or the zlib
 *    stream cannot be created.
 */
static int
streamWritePrepare(
    skstream_t         *stream)
{
    unsigned int alloc_flag = SK_ALLOC_FLAG_NO_CLEAR | SK_ALLOC_FLAG_NO_EXIT;
    uint8_t *buf;
    size_t bufsiz;
    int is_compr;
    int rv;

    rv = streamGZCheck(stream, &is_compr);
    if (rv) { return rv; }

    bufsiz = SKSTREAM_DEFAULT_BLOCKSIZE;
    buf = sk_alloc_memory(uint8_t, bufsiz, alloc_flag);
    if (NULL == buf) {
        stream->err_info = SKSTREAM_ERR_ALLOC;
        return SKSTREAM_ERR_ALLOC;
    }
    stream->is_dirty = 1;

    if (is_compr) {
        return streamGZCreate(stream, buf, bufsiz, 0);
    }
    streamBasicBufCreate(stream, buf, bufsiz, 0);
    return SKSTREAM_OK;
}


/*
 * *********************************
 * PUBLIC / EXPORTED FUNCTIONS
 * *********************************
 */

/*
 *  status = skStreamBind(stream, path);
 *
 *    Set 'stream' to operate on the file specified in 'path'; 'path'
 *    may also be one of "stdin", "stdout", or "stderr".  Returns
 *    SKSTREAM_OK on success, or an error code on failure.
 */
int
skStreamBind(
    skstream_t         *stream,
    const char         *pathname)
{
    int rv = SKSTREAM_OK;
    int is_compr;
    FILE *s = NULL;

    /* check name */
    if (NULL == stream || NULL == pathname) {
        rv = SKSTREAM_ERR_NULL_ARGUMENT;
        goto END;
    }
    if ('\0' == *pathname || strlen(pathname) >= PATH_MAX) {
        rv = SKSTREAM_ERR_INVALID_INPUT;
        goto END;
    }
    if (stream->pathname) {
        rv = SKSTREAM_ERR_PREV_BOUND;
        goto END;
    }

    /* copy it into place */
    stream->pathname = strdup(pathname);
    if (stream->pathname == NULL) {
        rv = SKSTREAM_ERR_ALLOC;
        goto END;
    }

    if (0 == strcmp(pathname, "stdin")) {
        switch (stream->io_mode) {
          case SK_IO_READ:
            if (!stream->is_mpi && stream->is_binary && FILEIsATty(stdin)) {
                rv = SKSTREAM_ERR_ISTERMINAL;
                goto END;
            }
            break;
          case SK_IO_WRITE:
          case SK_IO_APPEND:
            /* cannot write or append to stdin */
            rv = SKSTREAM_ERR_UNSUPPORT_IOMODE;
            goto END;
        }
    } else if (0 == strcmp(pathname, "stdout")) {
        s = stdout;
    } else if (0 == strcmp(pathname, "stderr")) {
        s = stderr;
    } else if (0 == strcmp(pathname, "-")) {
        switch (stream->io_mode) {
          case SK_IO_READ:
            if (!stream->is_mpi && stream->is_binary && FILEIsATty(stdin)) {
                rv = SKSTREAM_ERR_ISTERMINAL;
                goto END;
            }
            break;
          case SK_IO_WRITE:
            s = stdout;
            break;
          case SK_IO_APPEND:
            /* cannot append to stdout */
            rv = SKSTREAM_ERR_UNSUPPORT_IOMODE;
            goto END;
        }
    }

    if (s) {
        switch (stream->io_mode) {
          case SK_IO_READ:
          case SK_IO_APPEND:
            /* cannot read or append to stdout/stderr */
            rv = SKSTREAM_ERR_UNSUPPORT_IOMODE;
            goto END;
          case SK_IO_WRITE:
            if (!stream->is_mpi && stream->is_binary && FILEIsATty(s)) {
                rv = SKSTREAM_ERR_ISTERMINAL;
                goto END;
            }
            break;
        }
    }

    /* check for appending to gzipped files, writing text to gzipped
     * files, or writing to a ".gz" file when zlib support is not
     * available */
    rv = streamGZCheck(stream, &is_compr);
    if (rv) { goto END; }

    /* cannot append to FIFOs */
    if (stream->io_mode == SK_IO_APPEND
        && isFIFO(pathname))
    {
        rv = SKSTREAM_ERR_UNSUPPORT_IOMODE;
        goto END;
    }

    assert(SKSTREAM_OK == rv);
  END:
    return (stream->last_rv = rv);
}




int
skStreamCheckCompmethod(
    skstream_t         *stream,
    sk_msg_fn_t         errfn)
{
#ifdef TEST_PRINTF_FORMATS
#  define P_ERR printf
#else
#  define P_ERR if (!errfn) { } else errfn
#endif
    sk_compmethod_t compmethod;
    int rv;

    STREAM_RETURN_IF_NULL(stream);

    rv = streamCheckOpen(stream);
    if (rv) { goto END; }
    rv = streamCheckAttributes(stream, 0,
                               (SK_CONTENT_SILK | SK_CONTENT_SILK_FLOW));
    if (rv) { goto END; }

    assert(stream->silk_hdr);

    compmethod = skHeaderGetCompressionMethod(stream->silk_hdr);
    switch (skCompMethodCheck(compmethod)) {
      case SK_COMPMETHOD_IS_AVAIL:
        /* known, valid, and available */
        assert(SKSTREAM_OK == rv);
        break;
      case SK_COMPMETHOD_IS_VALID:
        /* known and valid but not available */
        if (errfn) {
            char name[64];
            skCompMethodGetName(name, sizeof(name), compmethod);
            P_ERR("The %s compression method used by '%s' is not available",
                  name, stream->pathname);
        }
        rv = SKSTREAM_ERR_COMPRESS_UNAVAILABLE;
        break;
      case SK_COMPMETHOD_IS_KNOWN:
        /* this is an undecided value, only valid for write */
        if (SK_IO_WRITE == stream->io_mode) {
            assert(SKSTREAM_OK == rv);
            break;
        }
        /* FALLTHROUGH */
      default:
        if (errfn) {
            P_ERR("File '%s' is compressed with an unrecognized method %d",
                  stream->pathname, compmethod);
        }
        rv = SKSTREAM_ERR_COMPRESS_INVALID;
        break;
    }

  END:
    return (stream->last_rv = rv);

#undef P_ERR
}


int
skStreamCheckSilkHeader(
    skstream_t         *stream,
    sk_file_format_t    file_format,
    sk_file_version_t   min_version,
    sk_file_version_t   max_version,
    sk_msg_fn_t         errfn)
{
#ifdef TEST_PRINTF_FORMATS
#  define P_ERR printf
#else
#  define P_ERR if (!errfn) { } else errfn
#endif
    sk_file_header_t *hdr;
    sk_file_format_t fmt;
    sk_file_version_t vers;
    char fmt_name[SK_MAX_STRLEN_FILE_FORMAT+1];
    int rv;

    STREAM_RETURN_IF_NULL(stream);

    rv = streamCheckOpen(stream);
    if (rv) { goto END; }

    rv = streamCheckAttributes(stream, (SK_IO_READ | SK_IO_APPEND),
                               (SK_CONTENT_SILK | SK_CONTENT_SILK_FLOW));
    if (rv) { goto END; }

    assert(stream->silk_hdr);

    hdr = stream->silk_hdr;
    fmt = skHeaderGetFileFormat(hdr);
    vers = skHeaderGetRecordVersion(hdr);

    /* get the name of the requested format */
    skFileFormatGetName(fmt_name, sizeof(fmt_name), file_format);

    if (fmt != file_format) {
        P_ERR("File '%s' is not a %s file; format is 0x%02x",
              stream->pathname, fmt_name, fmt);
        rv = SKSTREAM_ERR_UNSUPPORT_FORMAT;
        goto END;
    }

    if ((vers < min_version) || (vers > max_version)) {
        P_ERR("This version of SiLK cannot process the %s v%u file %s",
              fmt_name, vers, stream->pathname);
        rv = SKSTREAM_ERR_UNSUPPORT_VERSION;
        goto END;
    }

    /* skStreamCheckCompmethod() sets stream->last_rv */
    return skStreamCheckCompmethod(stream, errfn);

  END:
    return (stream->last_rv = rv);

#undef errfn
}


int
skStreamClose(
    skstream_t         *stream)
{
#if SK_ENABLE_ZLIB
    int err;
#endif
    int rv;

    STREAM_RETURN_IF_NULL(stream);

    rv = streamCheckOpen(stream);
    if (rv) { goto END; }

    if (stream->fp) {
        if (stream->is_pager_active) {
            if (pclose(stream->fp) == -1) {
                stream->errnum = errno;
                rv = ((rv != SKSTREAM_OK) ? rv : SKSTREAM_ERR_WRITE);
            }
        } else if (stream->is_stdio) {
            if (stream->io_mode != SK_IO_READ) {
                if (EOF == fflush(stream->fp)) {
                    stream->errnum = errno;
                    rv = ((rv != SKSTREAM_OK) ? rv : SKSTREAM_ERR_WRITE);
                }
            }
        } else {
            if (EOF == fclose(stream->fp)) {
                stream->errnum = errno;
                rv = ((rv != SKSTREAM_OK) ? rv : SKSTREAM_ERR_WRITE);
            }
        }

#if SK_ENABLE_ZLIB
        if (stream->zlib) {
            err = streamGZWriteFromPipe(stream);
            rv = ((rv != SKSTREAM_OK) ? rv : err);

            err = close(stream->zlib->pipe[0]);
            if (err && rv == SKSTREAM_OK) {
                stream->errnum = errno;
                rv = SKSTREAM_ERR_WRITE;
            }
        }
#endif  /* SK_ENABLE_ZLIB */

    } else if (stream->fd != -1) {
        if (stream->io_mode != SK_IO_READ) {
            if (stream->iobuf.rec_buf.b_buf) {
                streamIOBufFlush(stream);
            }
            if (stream->zlib) {
                err = streamGZClose(stream);
                rv = ((rv != SKSTREAM_OK) ? rv : err);
            } else if (stream->basicbuf.b_buf) {
                err = streamBasicBufFlush(stream);
                rv = ((rv != SKSTREAM_OK) ? rv : err);
            }
        }


        if (stream->is_stdio == 0) {
            if (close(stream->fd) == -1) {
                stream->errnum = errno;
                rv = SKSTREAM_ERR_WRITE;
            }
        }
    }

    stream->fd = -1;
    stream->fp = NULL;
    stream->is_closed = 1;

  END:
    return (stream->last_rv = rv);
}


/*
 *  status = skStreamCreate(&out_stream, io_mode, content_type);
 *
 *    Create a stream (skstream_t*) and fill 'out_stream' with the
 *    address of the newly allocated stream.  In addition, bind the
 *    stream to the given 'path', with IO in the specified 'io_mode'.
 *    Return SKSTREAM_OK on success, or an error code on failure.
 */
int
skStreamCreate(
    skstream_t        **stream_ptr,
    skstream_mode_t     read_write_append,
    skcontent_t         content_type)
{
    skstream_t *stream;

    if (stream_ptr == NULL) {
        return SKSTREAM_ERR_NULL_ARGUMENT;
    }

    /* do not allow appending to text or to "otherbinary" files */
    if (SK_IO_APPEND == read_write_append
        && (SK_CONTENT_OTHERBINARY == content_type
            || SK_CONTENT_TEXT == content_type))
    {
        return SKSTREAM_ERR_UNSUPPORT_IOMODE;
    }

    stream = *stream_ptr = sk_alloc(skstream_t);

    stream->io_mode = read_write_append;
    stream->content_type = content_type;
    stream->fd = -1;

    /* Native format by default, so don't swap */
    stream->swap_flag = 0;

    switch (content_type) {
      case SK_CONTENT_TEXT:
        break;

      case SK_CONTENT_SILK_FLOW:
        stream->is_silk_flow = 1;
        /* FALLTHROUGH */

      case SK_CONTENT_SILK:
        stream->is_silk = 1;
        if (skHeaderCreate(&stream->silk_hdr)) {
            free(stream);
            *stream_ptr = NULL;
            return SKSTREAM_ERR_ALLOC;
        }
        /* Set sensor and flowtype to invalid values */
        stream->silkflow.hdr_sensor = SK_INVALID_SENSOR;
        stream->silkflow.hdr_flowtype = SK_INVALID_FLOWTYPE;
        /* FALLTHROUGH */

      case SK_CONTENT_OTHERBINARY:
        stream->is_binary = 1;
        break;
    }

    return (stream->last_rv = SKSTREAM_OK);
}


int
skStreamDestroy(
    skstream_t        **stream_ptr)
{
    skstream_t *stream;
    int rv;

    if ((NULL == stream_ptr) || (NULL == *stream_ptr)) {
        return SKSTREAM_OK;
    }
    stream = *stream_ptr;
    *stream_ptr = NULL;

    rv = skStreamUnbind(stream);

    streamIOBufDestroy(stream);

#if SK_ENABLE_ZLIB
    /* Destroy the zlib object */
    if (stream->zlib) {
        if (SK_IO_READ == stream->io_mode) {
            inflateEnd(&stream->zlib->zstrm);
        } else {
            deflateEnd(&stream->zlib->zstrm);
        }
        free(stream->zlib);
        stream->zlib = NULL;
    }
#endif  /* SK_ENABLE_ZLIB */
    if (stream->basicbuf.b_buf) {
        streamBasicBufDestroy(stream);
    }

    if (stream->sidecar) {
        sk_sidecar_destroy((sk_sidecar_t **)&stream->sidecar);
    }

    /* Destroy the header */
    skHeaderDestroy(&stream->silk_hdr);

    /* Free the pathname */
    free(stream->pathname);
    stream->pathname = NULL;

    free(stream);

    return rv;
}


int
skStreamFDOpen(
    skstream_t         *stream,
    int                 file_desc)
{
    int rv;

    STREAM_RETURN_IF_NULL(stream);

    rv = streamCheckUnopened(stream);
    if (rv) { goto END; }

    if (stream->pathname == NULL) {
        rv = SKSTREAM_ERR_NOT_BOUND;
        goto END;
    }

    if (file_desc == -1) {
        rv = SKSTREAM_ERR_INVALID_INPUT;
        goto END;
    }

    /* Check file modes */
    rv = fcntl(file_desc, F_GETFL, 0);
    if (rv == -1) {
        stream->errnum = errno;
        rv = SKSTREAM_ERR_SYS_FCNTL_GETFL;
        goto END;
    }
    switch (stream->io_mode) {
      case SK_IO_READ:
        if ((rv & O_ACCMODE) == O_WRONLY) {
            rv = SKSTREAM_ERR_UNSUPPORT_IOMODE;
            goto END;
        }
        break;
      case SK_IO_WRITE:
        if (((rv & O_ACCMODE) == O_RDONLY) || (rv & O_APPEND)) {
            rv = SKSTREAM_ERR_UNSUPPORT_IOMODE;
            goto END;
        }
        break;
      case SK_IO_APPEND:
        if (((rv & O_ACCMODE) != O_RDWR) || !(rv & O_APPEND)) {
            rv = SKSTREAM_ERR_UNSUPPORT_IOMODE;
            goto END;
        }
        break;
    }

    /* Check tty status if binary */
    if (stream->is_binary && isatty(file_desc)) {
        rv = SKSTREAM_ERR_ISTERMINAL;
        goto END;
    }

    /* Seek to beginning on append for the header.  Check this after
     * the tty status check, because that is a more useful error
     * message. */
    if ((stream->io_mode == SK_IO_APPEND)
        && (-1 == lseek(file_desc, 0, SEEK_SET)))
    {
        stream->errnum = errno;
        rv = SKSTREAM_ERR_SYS_LSEEK;
        goto END;
    }

    stream->fd = file_desc;

    rv = streamPostOpen(stream);
    if (rv) { goto END; }

    assert(SKSTREAM_OK == rv);
  END:
    return (stream->last_rv = rv);
}


int
skStreamFlush(
    skstream_t         *stream)
{
    int rv;

    STREAM_RETURN_IF_NULL(stream);

    rv = streamCheckOpen(stream);
    if (rv) { goto END; }

    if (stream->io_mode == SK_IO_READ) {
        /* nothing to do for a reader */
        goto END;
    }

    if (stream->iobuf.rec_buf.b_buf) {
        rv = streamIOBufFlush(stream);
        if (rv) { goto END; }
    }

    if (stream->fp) {
        if (EOF == fflush(stream->fp)) {
            stream->errnum = errno;
            rv = SKSTREAM_ERR_WRITE;
        }
    } else if (stream->basicbuf.b_buf) {
        if (streamBasicBufFlush(stream)) {
            rv = stream->err_info;
        }
    } else if (stream->zlib) {
        if (streamGZFlush(stream) == -1) {
            rv = stream->err_info;
        }
    }

  END:
    return (stream->last_rv = rv);
}


/* return the content type */
skcontent_t
skStreamGetContentType(
    const skstream_t   *stream)
{
    assert(stream);
    return stream->content_type;
}


/* return the file descriptor */
int
skStreamGetDescriptor(
    const skstream_t   *stream)
{
    assert(stream);
    return stream->fd;
}


/* return the cached errno value */
int
skStreamGetLastErrno(
    const skstream_t   *stream)
{
    assert(stream);
    return stream->errnum;
}


/* return the cached return value */
ssize_t
skStreamGetLastReturnValue(
    const skstream_t   *stream)
{
    assert(stream);
    return stream->last_rv;
}


/* fill 'value' with the limit implied by the error code */
int
skStreamGetLimit(
    const skstream_t   *stream,
    int                 limit_id,
    int64_t            *value)
{
    sk_file_format_t file_format;
    sk_file_version_t rec_version;
    int rv = SKSTREAM_OK;

    STREAM_RETURN_IF_NULL(stream);

    if (!stream->is_silk_flow || !stream->silk_hdr) {
        rv = SKSTREAM_ERR_REQUIRE_SILK_FLOW;
        goto END;
    }

    file_format = skHeaderGetFileFormat(stream->silk_hdr);
    rec_version = skHeaderGetRecordVersion(stream->silk_hdr);
    if (UINT8_MAX == file_format) {
        file_format = DEFAULT_FILE_FORMAT;
    }

    switch (limit_id) {
      case SKSTREAM_ERR_PKTS_ZERO:
        /* The record contains a 0 value in the packets field. */
        *value = 1;
        break;

      case SKSTREAM_ERR_STIME_UNDRFLO:
        /* The record's start time is less than the file's start
         * time */
        switch (file_format) {
          case FT_RWAUGMENTED:
          case FT_RWAUGROUTING:
          case FT_RWAUGSNMPOUT:
          case FT_RWAUGWEB:
          case FT_RWNOTROUTED:
          case FT_RWROUTED:
          case FT_RWSPLIT:
          case FT_RWWWW:
            *value = stream->silkflow.hdr_starttime;
            break;
          default:
            *value = 0;
            break;
        }
        break;

      case SKSTREAM_ERR_STIME_OVRFLO:
        /* The record's start time at least an hour greater than the
         * file's start time */
        *value = (int64_t)sktimeCreate(UINT32_MAX, 0);
        switch (file_format) {
          case FT_RWAUGMENTED:
          case FT_RWAUGROUTING:
          case FT_RWAUGSNMPOUT:
          case FT_RWAUGWEB:
          case FT_RWNOTROUTED:
          case FT_RWROUTED:
          case FT_RWSPLIT:
          case FT_RWWWW:
            *value = (stream->silkflow.hdr_starttime
                      + sktimeCreate((MAX_START_TIME - 1), 0));
            break;
          case FT_RWGENERIC:
            switch (rec_version) {
              case 5:
                *value = INT64_MAX;
                break;
            }
            break;
          case FT_RWIPV6:
            switch (rec_version) {
              case 2:
                *value = (stream->silkflow.hdr_starttime
                          + sktimeCreate((MAX_START_TIME - 1), 0));
                break;
              default:
                *value = INT64_MAX;
                break;
            }
            break;
          case FT_RWIPV6ROUTING:
            *value = INT64_MAX;
            break;
        }
        break;

      case SKSTREAM_ERR_ELPSD_OVRFLO:
          /* The record's elapsed time is greater than space allocated
           * for duration in this file format */
        switch (file_format) {
          case FT_RWAUGMENTED:
          case FT_RWAUGROUTING:
          case FT_RWAUGSNMPOUT:
            if (rec_version <= 4) {
                *value = (MAX_ELAPSED_TIME - 1) * 1000;
            } else {
                *value = (int64_t)UINT32_MAX;
            }
            break;
          case FT_RWAUGWEB:
            if (rec_version <= 4) {
                *value = (MAX_ELAPSED_TIME - 1) * 1000;
            } else {
                *value = (int64_t)MASKARRAY_30;
            }
            break;
          case FT_FLOWCAP:
            *value = UINT16_MAX * 1000;
            break;
          case FT_RWFILTER:
          case FT_RWNOTROUTED:
          case FT_RWROUTED:
          case FT_RWSPLIT:
          case FT_RWWWW:
            switch (rec_version) {
              case 1:
              case 2:
                *value = (MAX_ELAPSED_TIME_OLD - 1) * 1000;
                break;
              default:
                *value = (MAX_ELAPSED_TIME - 1) * 1000;
                break;
            }
            break;
          default:
            *value = (int64_t)UINT32_MAX;
            break;
        }
        break;

      case SKSTREAM_ERR_PKTS_OVRFLO:
        /* The record contains more than the number of packets allowed
         * in this file format */
        switch (file_format) {
          case FT_RWAUGMENTED:
          case FT_RWAUGROUTING:
          case FT_RWAUGSNMPOUT:
          case FT_RWAUGWEB:
            switch (rec_version) {
              case 5:
                *value = (int64_t)UINT32_MAX;
              default:
                *value = MAX_PKTS * PKTS_DIVISOR - 1;
                break;
            }
            break;
          case FT_RWFILTER:
          case FT_RWNOTROUTED:
          case FT_RWROUTED:
          case FT_RWSPLIT:
          case FT_RWWWW:
            *value = MAX_PKTS * PKTS_DIVISOR - 1;
            break;
          case FT_FLOWCAP:
            *value = MASKARRAY_24;
            break;
          case FT_RWGENERIC:
          case FT_RWIPV6:
          case FT_RWIPV6ROUTING:
            *value = (int64_t)UINT32_MAX;
            break;
        }
        break;

      case SKSTREAM_ERR_BPP_OVRFLO:
        /* The byte-per-packet value is too large to fit into the
         * space provided by this file format. */
        switch (file_format) {
          case FT_RWAUGMENTED:
          case FT_RWAUGROUTING:
          case FT_RWAUGSNMPOUT:
          case FT_RWAUGWEB:
            switch (rec_version) {
              case 5:
                *value = (int64_t)UINT32_MAX;
              default:
                *value = MASKARRAY_14;
                break;
            }
            break;
          case FT_RWFILTER:
          case FT_RWNOTROUTED:
          case FT_RWROUTED:
          case FT_RWSPLIT:
          case FT_RWWWW:
            *value = MASKARRAY_14;
            break;
          case FT_FLOWCAP:
          case FT_RWGENERIC:
          case FT_RWIPV6:
          case FT_RWIPV6ROUTING:
            *value = (int64_t)UINT32_MAX;
            break;
        }
        break;

      case SKSTREAM_ERR_SNMP_OVRFLO:
        /* The records contains an SNMP value too large to fit into
         * the space allocated in this file format. */
        *value = 0;
        switch (file_format) {
          case FT_RWAUGROUTING:
          case FT_RWAUGSNMPOUT:
          case FT_RWIPV6ROUTING:
            *value = UINT16_MAX;
            break;
          case FT_RWFILTER:
          case FT_RWNOTROUTED:
          case FT_RWROUTED:
            switch (rec_version) {
              case 1:
              case 2:
                *value = UINT8_MAX;
                break;
              default:
                *value = UINT16_MAX;
                break;
            }
            break;
          case FT_RWGENERIC:
            switch (rec_version) {
              case 0:
              case 1:
                *value = UINT8_MAX;
                break;
              default:
                *value = UINT16_MAX;
                break;
            }
            break;
          case FT_FLOWCAP:
            switch (rec_version) {
              case 2:
              case 3:
              case 4:
                *value = UINT8_MAX;
                break;
              default:
                *value = UINT16_MAX;
                break;
            }
            break;
        }
        break;

      case SKSTREAM_ERR_SENSORID_OVRFLO:
        /* The records contains a SensorID too large to fit into the
         * space allocated in this file format. */
        *value = UINT16_MAX;
        switch (file_format) {
          case FT_RWFILTER:
            switch (rec_version) {
              case 1:
                *value = MASKARRAY_06;
                break;
              case 2:
                *value = UINT8_MAX;
                break;
            }
          case FT_RWGENERIC:
            switch (rec_version) {
              case 0:
              case 1:
                *value = UINT8_MAX;
                break;
            }
            break;
        }
        break;

      case SKSTREAM_ERR_BYTES_OVRFLO:
        /* The record contains more than the number of bytes allowed
         * in this file format */
        *value = (int64_t)UINT32_MAX;
        break;

      default:
        /* unknown limit */
        rv = SKSTREAM_ERR_INVALID_INPUT;
        break;
    }

  END:
    return rv;
}


/* Get the next line from a text file */
int
skStreamGetLine(
    skstream_t         *stream,
    char               *out_buffer,
    size_t              buf_size,
    int                *lines_read)
{
    const int eol_char = '\n';
    char *cp;
    size_t len;
    ssize_t sz;
    int rv;

    STREAM_RETURN_IF_NULL(stream);

    rv = streamCheckAttributes(stream, SK_IO_READ, SK_CONTENT_TEXT);
    if (rv) { goto END; }

    if (!stream->basicbuf.b_buf && !stream->zlib) {
        rv = streamCheckOpen(stream);
        if (rv) { goto END; }

        if (!stream->is_dirty) {
            rv = streamTextPrepare(stream);
            if (rv) { goto END; }
        }
    }

    assert(out_buffer && buf_size);
    out_buffer[0] = '\0';

    /* read from the stream until we get a good line */
    for (;;) {
        /* substract 1 from 'buf_size' for final '\0' */
        if (stream->basicbuf.b_buf) {
            sz = streamBasicBufReadToChar(stream, out_buffer,
                                          buf_size - 1, eol_char);
        } else {
            sz = streamGZReadToChar(stream, out_buffer,
                                    buf_size - 1, eol_char);
        }
        if (sz <= 0) {
            if (sz == -1) {
                rv = stream->err_info;
            } else {
                rv = SKSTREAM_ERR_EOF;
            }
            break;
        }
        if ((sz == (ssize_t)buf_size-1) && ('\n' != out_buffer[sz-1])) {
            /* Found no newline in 'buf_size' characters... */
            rv = SKSTREAM_ERR_LONG_LINE;
            /* continue to read from the stream until we find a '\n',
             * overwriting the out_buffer for each read */
            continue;
        }

        /* terminate the string, either by replacing '\n' with a '\0',
         * or by putting a '\0' after the final character. */
        cp = &out_buffer[sz-1];
        if (*cp != '\n') {
            ++cp;
        }
        *cp = '\0';

        if (rv != SKSTREAM_OK) {
            if ((rv == SKSTREAM_ERR_LONG_LINE) && lines_read) {
                ++*lines_read;
            }
            break;
        }
        if (lines_read) {
            ++*lines_read;
        }

        /* Terminate line at first comment char */
        if (stream->comment_start) {
            cp = strstr(out_buffer, stream->comment_start);
            if (cp) {
                *cp = '\0';
            }
        }

        /* find first non-space character in the line */
        len = strspn(out_buffer, " \t\v\f\r\n");
        if (out_buffer[len] == '\0') {
            /* line contained whitespace only; ignore */
            continue;
        }

        /* got a line, break out of loop */
        break;
    }

  END:
    return (stream->last_rv = rv);
}


/* return the read/write/append mode */
skstream_mode_t
skStreamGetMode(
    const skstream_t   *stream)
{
    assert(stream);
    return stream->io_mode;
}


/* return the name of pager program */
const char*
skStreamGetPager(
    const skstream_t   *stream)
{
    if (stream->is_closed) {
        return NULL;
    } else if (stream->is_pager_active) {
        /* stream is open and pager is in use */
        return stream->pager;
    } else if (stream->fd == -1) {
        /* unopened, return pager we *may* use */
        return stream->pager;
    } else {
        /* stream is open and not using pager */
        return NULL;
    }
}


/* return the name of file associated with the stream */
const char*
skStreamGetPathname(
    const skstream_t   *stream)
{
    assert(stream);
    return stream->pathname;
}


/* return number of SiLK flow records processed */
uint64_t
skStreamGetRecordCount(
    const skstream_t   *stream)
{
    assert(stream);
    if (!stream->is_silk_flow) {
        return ((uint64_t)(-1));
    }
    return stream->rec_count;
}


const sk_sidecar_t *
skStreamGetSidecar(
    skstream_t         *stream)
{
    ssize_t rv;

    if (NULL == stream) {
        return NULL;
    }
    if (!stream->have_hdr) {
        if (streamCheckAttributes(stream, SK_IO_READ | SK_IO_APPEND,
                                  SK_CONTENT_SILK_FLOW))
        {
            return NULL;
        }
        rv = streamSilkHeaderRead(stream, 0);
        if (rv) {
            stream->last_rv = rv;
            return NULL;
        }
    }
    return stream->sidecar;
}


sk_file_header_t*
skStreamGetSilkHeader(
    const skstream_t   *stream)
{
    assert(stream);
    if (!stream->is_silk) {
        return NULL;
    }
    return stream->silk_hdr;
}


int
skStreamGetSupportsIPv6(
    const skstream_t   *stream)
{
    assert(stream);
    return stream->supports_ipv6;
}


off_t
skStreamGetUpperBound(
    skstream_t         *stream)
{
    assert(stream);
    assert(stream->fd != -1);

    if (stream->io_mode == SK_IO_READ) {
        return 0;
    }
    return lseek(stream->fd, 0, SEEK_CUR);
}


int
skStreamInitialize(
    void)
{
    const char *env;

    env = getenv(SILK_ICMP_SPORT_HANDLER_ENVAR);
    if (NULL != env && (0 == strcasecmp(env, "none"))) {
        silk_icmp_nochange = 1;
    }

#ifdef SILK_CLOBBER_ENVAR
    env = getenv(SILK_CLOBBER_ENVAR);
    if (NULL != env && *env && *env != '0') {
        silk_clobber = 1;
    }
#endif

    return 0;
}


int
skStreamIsSeekable(
    const skstream_t   *stream)
{
    assert(stream);
    return stream->is_seekable;
}


int
skStreamIsStdout(
    const skstream_t   *stream)
{
    assert(stream);
    return ((SK_IO_WRITE == stream->io_mode)
            && (NULL != stream->pathname)
            && (streamPathnameIsStdout(stream)));
}


int
skStreamLockFile(
    skstream_t         *stream)
{
    struct flock lock;
    int rv;

    lock.l_start = 0;             /* at SOF */
    lock.l_whence = SEEK_SET;     /* SOF */
    lock.l_len = 0;               /* EOF */

    STREAM_RETURN_IF_NULL(stream);

    rv = streamCheckOpen(stream);
    if (rv) { goto END; }

    /* Don't try to lock anything that is not a real file */
    if ( !stream->is_seekable) {
        goto END;
    }

    /* set the lock type and error code if we fail */
    if (stream->io_mode == SK_IO_READ) {
        lock.l_type = F_RDLCK;
        rv = SKSTREAM_ERR_RLOCK;
    } else {
        lock.l_type = F_WRLCK;
        rv = SKSTREAM_ERR_WLOCK;
    }

    /* get the lock, waiting if we need to */
    if (fcntl(stream->fd, F_SETLKW, &lock) == -1) {
        /* error */
        stream->errnum = errno;
        goto END;
    }

    /* success */
    rv = SKSTREAM_OK;

    assert(SKSTREAM_OK == rv);
  END:
    return (stream->last_rv = rv);
}


int
skStreamMakeDirectory(
    skstream_t         *stream)
{
    char dir[PATH_MAX];
    int rv;

    STREAM_RETURN_IF_NULL(stream);

    rv = streamCheckUnopened(stream);
    if (rv) { goto END; }

    /* Making directory to file only makes sense for writing */
    if (stream->io_mode != SK_IO_WRITE) {
        rv = SKSTREAM_ERR_UNSUPPORT_IOMODE;
        goto END;
    }

    if (stream->pathname == NULL) {
        rv = SKSTREAM_ERR_NOT_BOUND;
        goto END;
    }

    if (skDirname_r(dir, stream->pathname, sizeof(dir))) {
        if ( !skDirExists(dir)) {
            rv = skMakeDir(dir);
            if (rv) {
                stream->errnum = errno;
                rv = SKSTREAM_ERR_SYS_MKDIR;
                goto END;
            }
        }
    }

    assert(SKSTREAM_OK == rv);
  END:
    return (stream->last_rv = rv);
}


int
skStreamMakeTemp(
    skstream_t         *stream)
{
    int rv;

    STREAM_RETURN_IF_NULL(stream);

    rv = streamCheckUnopened(stream);
    if (rv) { goto END; }

    /* Temp files only make sense for writing */
    if (stream->io_mode != SK_IO_WRITE) {
        rv = SKSTREAM_ERR_UNSUPPORT_IOMODE;
        goto END;
    }

    if (stream->pathname == NULL) {
        rv = SKSTREAM_ERR_NOT_BOUND;
        goto END;
    }

    /* open file */
    stream->fd = mkstemp(stream->pathname);
    if (stream->fd == -1) {
        rv = SKSTREAM_ERR_SYS_MKSTEMP;
        stream->errnum = errno;
        goto END;
    }

    rv = streamPostOpen(stream);
    if (rv) { goto END; }

    assert(SKSTREAM_OK == rv);
  END:
    return (stream->last_rv = rv);
}


int
skStreamOpen(
    skstream_t         *stream)
{
    int rv;

    STREAM_RETURN_IF_NULL(stream);

    rv = streamCheckUnopened(stream);
    if (rv) { goto END; }

    if (stream->pathname == NULL) {
        rv = SKSTREAM_ERR_NOT_BOUND;
        goto END;
    }

    switch (stream->io_mode) {
      case SK_IO_WRITE:
        rv = streamOpenWrite(stream);
        if (rv) { goto END; }
        break;

      case SK_IO_READ:
        rv = streamOpenRead(stream);
        if (rv) { goto END; }
        break;

      case SK_IO_APPEND:
        rv = streamOpenAppend(stream);
        if (rv) { goto END; }
        break;
    }

    rv = streamPostOpen(stream);
    if (rv) { goto END; }

    assert(SKSTREAM_OK == rv);
  END:
    return (stream->last_rv = rv);
}


/* convenience function to create and open a SiLK flow file */
int
skStreamOpenSilkFlow(
    skstream_t        **stream_ptr,
    const char         *pathname,
    skstream_mode_t     read_write_append)
{
    skstream_t *stream = NULL;
    int rv;

    /* Allocate and initialize the stream */
    rv = skStreamCreate(stream_ptr, read_write_append, SK_CONTENT_SILK_FLOW);
    if (rv) { goto END; }

    stream = *stream_ptr;

    rv = skStreamBind(stream, pathname);
    if (rv) { goto END; }

    rv = skStreamOpen(stream);
    if (rv) { goto END; }

    switch (stream->io_mode) {
      case SK_IO_WRITE:
        break;

      case SK_IO_READ:
      case SK_IO_APPEND:
        rv = streamSilkHeaderRead(stream, 0);
        if (rv) {
            skStreamClose(stream);
            stream->last_rv = rv;
            goto END;
        }
        break;
    }

    assert(SKSTREAM_OK == rv);
  END:
    /* all functions above should have set stream->last_rv */
    return rv;
}


int
skStreamPageOutput(
    skstream_t         *stream,
    const char         *pager)
{
    int rv;

    STREAM_RETURN_IF_NULL(stream);

    rv = streamCheckModifiable(stream);
    if (rv) { goto END; }
    rv = streamCheckAttributes(stream, SK_IO_WRITE, SK_CONTENT_TEXT);
    if (rv) { goto END; }

    /* get pager from environment if not passed in */
    if (NULL == pager) {
        pager = getenv("SILK_PAGER");
        if (NULL == pager) {
            pager = getenv("PAGER");
        }
    }

    /* a NULL or an empty string pager means do nothing */
    if ((NULL == pager) || ('\0' == pager[0])) {
        if (stream->pager) {
            free(stream->pager);
            stream->pager = NULL;
        }
        goto END;
    }

    if (stream->pager) {
        free(stream->pager);
    }
    stream->pager = strdup(pager);
    if (stream->pager == NULL) {
        rv = SKSTREAM_ERR_ALLOC;
        goto END;
    }

    assert(SKSTREAM_OK == rv);
  END:
    return (stream->last_rv = rv);
}


#if !defined(skStreamPrint)
int
skStreamPrint(
    skstream_t         *stream,
    const char         *format,
    ...)
{
    int rv;
    va_list args;

    va_start(args, format);

    if (NULL == stream) {
        va_end(args);
        return SKSTREAM_ERR_NULL_ARGUMENT;
    }

    rv = streamCheckAttributes(stream, (SK_IO_WRITE | SK_IO_APPEND),
                               SK_CONTENT_TEXT);
    if (rv) { goto END; }

    if (!stream->fp) {
        rv = streamCheckOpen(stream);
        if (rv) { goto END; }
        rv = streamTextPrepare(stream);
        if (rv) { goto END; }
    }

    if (vfprintf(stream->fp, format, args) == -1) {
        stream->errnum = errno;
        rv = SKSTREAM_ERR_WRITE;
        goto END;
    }

    assert(SKSTREAM_OK == rv);
  END:
    va_end(args);
    return (stream->last_rv = rv);
}
#endif /* !defined(skStreamPrint) */


ssize_t
skStreamRead(
    skstream_t         *stream,
    void               *buf,
    size_t              count)
{
    ssize_t rv;

    if (NULL == stream) {
        return -1;
    }
    rv = streamCheckOpen(stream);
    if (rv) {
        stream->err_info = rv;
        return (stream->last_rv = -1);
    }
    if (stream->io_mode & SK_IO_READ) {
        /* good */
    } else if ((stream->io_mode != SK_IO_APPEND)
               || !(stream->content_type & (SK_CONTENT_SILK
                                            | SK_CONTENT_SILK_FLOW))
               || !stream->is_dirty || stream->have_hdr)
    {
        /* reading is only allowed on an append stream when reading
         * the header of a SiLK Flow file */
        stream->err_info = SKSTREAM_ERR_UNSUPPORT_IOMODE;
        return (stream->last_rv = -1);
    }
    if (SK_CONTENT_SILK_FLOW == stream->content_type) {
        if (!stream->is_dirty || stream->have_hdr) {
            /* May only use skStreamRead() on a flow stream while
             * reading the header, and skStreamReadSilkHeader() should
             * have set the is_dirty flag. */
            stream->err_info = SKSTREAM_ERR_UNSUPPORT_CONTENT;
            return (stream->last_rv = -1);
        }
    }

    for (;;) {
        if (stream->iobuf.rec_buf.b_buf) {
            assert(SK_CONTENT_SILK == stream->content_type);
            return (stream->last_rv = streamIOBufRead(stream, buf, count));
        }
        if (stream->basicbuf.b_buf) {
            return (stream->last_rv = streamBasicBufRead(stream, buf, count));
        }
        if (stream->zlib) {
            return (stream->last_rv = streamGZRead(stream, buf, count));
        }
        if (stream->is_dirty) {
            skAppPrintErr("Stream '%s' does not have a read buffer",
                          stream->pathname);
            skAbort();
        }
        switch (stream->content_type) {
          case SK_CONTENT_TEXT:
          case SK_CONTENT_OTHERBINARY:
            rv = streamReadPrepare(stream);
            if (rv) {
                stream->err_info = rv;
                return (stream->last_rv = -1);
            }
            break;
          case SK_CONTENT_SILK:
            /* Need to read the header */
            rv = streamSilkHeaderRead(stream, 0);
            if (rv) {
                stream->err_info = rv;
                return (stream->last_rv = -1);
            }
            assert(stream->have_hdr);
            break;
          case SK_CONTENT_SILK_FLOW:
          default:
            skAbortBadCase(stream->content_type);
        }
        if (!stream->is_dirty) {
            skAbort();
        }
    }
}


int
skStreamReadRecord(
    skstream_t         *stream,
    rwRec              *rwrec)
{
#ifndef SK_HAVE_ALIGNED_ACCESS_REQUIRED
    uint8_t ar[SK_MAX_RECORD_SIZE];
#else
    /* force 'ar' to be aligned on an 8byte boundary, since we treat
     * it as an rwRec and need to access the 64bit sTime. */
    union force_align_un {
        uint8_t  fa_ar[SK_MAX_RECORD_SIZE];
        uint64_t fa_u64;
    } force_align;
    uint8_t *ar = force_align.fa_ar;
#endif  /* SK_HAVE_ALIGNED_ACCESS_REQUIRED */
    ssize_t saw;
    int rv;

    STREAM_RETURN_IF_NULL(stream);

    rv = streamCheckOpen(stream);
    if (rv) { goto END; }

    if (stream->have_hdr) {
        rv = streamCheckAttributes(stream, (SK_IO_READ | SK_IO_APPEND),
                                   SK_CONTENT_SILK_FLOW);
    } else {
        rv = streamSilkHeaderRead(stream, 0);
    }
    if (rv) { goto END; }

    if (stream->is_eof) {
        rv = SKSTREAM_ERR_EOF;
        goto END;
    }

    /* label is used by the IPv6 policy to ignore a record */
  NEXT_RECORD:
    if (NULL == stream->sidecar) {
        if (stream->iobuf.rec_buf.b_buf) {
            saw = streamIOBufRead(stream, ar, stream->rec_len);
        } else if (stream->basicbuf.b_buf) {
            saw = streamBasicBufRead(stream, ar, stream->rec_len);
        } else {
            assert(stream->zlib);
            saw = streamGZRead(stream, ar, stream->rec_len);
        }
        if (saw != (ssize_t)stream->rec_len) {
            /* EOF or error */
            stream->is_eof = 1;
            if (saw == 0) {
                /* 0 means clean record boundary; simple EOF */
                rv = SKSTREAM_ERR_EOF;
            } else if (saw == -1) {
                /* error */
                rv = -1;
            } else {
                /* short read */
                stream->errobj.num = saw;
                rv = SKSTREAM_ERR_READ_SHORT;
            }
            goto END;
        }
        /* unpack the byte array to an rwRec */
        rwRecReset(rwrec);
        stream->silkflow.unpack(stream, rwrec, ar);
    } else {
        lua_State *L = rwrec->lua_state;
        int ref = LUA_NOREF;
        size_t buflen;

        /* get the fixed size record */
        if (stream->iobuf.rec_buf.b_avail < stream->rec_len) {
            if (0 != stream->iobuf.rec_buf.b_avail) {
                skAppPrintErr(("Warning: ignoring partial record in IObuf;"
                               " going to next block (%" SK_PRIuZ " bytes)"),
                              stream->iobuf.rec_buf.b_avail);
                stream->iobuf.rec_buf.b_pos += stream->iobuf.rec_buf.b_avail;
                stream->iobuf.rec_buf.b_avail = 0;
            }
            rv = streamIOBufBlockRead(stream, &stream->iobuf.rec_buf, NULL,
                                      STREAM_BLOCK_HDR_DATA);
            if (rv) {
                goto END;
            }
            if (0 != stream->iobuf.sc_buf.b_avail) {
                skAppPrintErr(("Warning: expected empty sidecar buffer"
                               " while refreshing record buffer but found"
                               " %" SK_PRIuZ " bytes"),
                              stream->iobuf.sc_buf.b_avail);
            } else {
                rv = streamIOBufBlockRead(stream, &stream->iobuf.sc_buf, NULL,
                                          STREAM_BLOCK_HDR_SIDECAR);
                if (rv) {
                    skAppPrintErr("Warning: failed to read sidecar buffer"
                                  " after refreshing record buffer");
                    goto END;
                }
            }
            if (stream->iobuf.rec_buf.b_avail < stream->rec_len) {
                rv = SKSTREAM_ERR_READ_SHORT;
                goto END;
            }
        }
        memcpy(ar, stream->iobuf.rec_buf.b_pos, stream->rec_len);
        stream->iobuf.rec_buf.b_avail -= stream->rec_len;
        stream->iobuf.rec_buf.b_pos += stream->rec_len;

        /* handle the sidecar data */
        if (0 == stream->iobuf.sc_buf.b_avail) {
            skAppPrintErr("Warning: empty sidecar buffer in IObuf");
            rv = streamIOBufBlockRead(stream, &stream->iobuf.sc_buf, NULL,
                                      STREAM_BLOCK_HDR_SIDECAR);
            if (rv) {
                goto END;
            }
        }
        buflen = stream->iobuf.sc_buf.b_avail;

        if (NULL == L) {
            rv = sk_sidecar_skip_data(stream->sidecar,
                                      stream->iobuf.sc_buf.b_pos, &buflen);
        } else {
            rv = (sk_sidecar_deserialize_data(
                      stream->sidecar, L, stream->iobuf.sc_buf.b_pos,
                      &buflen, &ref));
        }
        switch (rv) {
          case SK_SIDECAR_OK:
            TRACEMSG(3, (FMT_FLS "lua=%p top=%d, type=%s",
                         FLS(stream), V(L), lua_gettop(L),
                         luaL_typename(L, lua_gettop(L))));
            stream->iobuf.sc_buf.b_avail -= buflen;
            stream->iobuf.sc_buf.b_pos += buflen;
            break;
          case SK_SIDECAR_E_SHORT_DATA:
            rv = SKSTREAM_ERR_READ_SHORT;
            goto END;
          case SK_SIDECAR_E_DECODE_ERROR:
            rv = SKSTREAM_ERR_READ_SHORT;
            goto END;
          default:
            skAbortBadCase(rv);
        }

        rwRecReset(rwrec);
        stream->silkflow.unpack(stream, rwrec, ar);
        rwRecSetSidecar(rwrec, ref);
        rv = SKSTREAM_OK;
    }

    /* Handle incorrectly encoded ICMP Type/Code unless the
     * SILK_ICMP_SPORT_HANDLER environment variable is set to none. */
    if (rwRecIsICMP(rwrec)
        && rwRecGetSPort(rwrec) != 0
        && rwRecGetDPort(rwrec) == 0
        && !silk_icmp_nochange)
    {
        /*
         *  There are two ways for the ICMP Type/Code to appear in
         *  "sPort" instead of in "dPort".
         *
         *  (1) The flow was an IPFIX bi-flow record read prior to
         *  SiLK-3.4.0 where the sPort and dPort of the second record
         *  were reversed when they should not have been.  Here, the
         *  sPort contains ((type<<8)|code).
         *
         *  (2) The flow was a NetFlowV5 record read from a buggy
         *  Cisco router and read prior to SiLK-0.8.0.  Here, the
         *  sPort contains ((code<<8)|type).
         *
         *  The following assumes buggy ICMP flow records were created
         *  from IPFIX sources unless they were created prior to SiLK
         *  1.0 and appear in certain file formats more closely
         *  associated with NetFlowV5.
         *
         *  Prior to SiLK-3.4.0, the buggy ICMP record would propagate
         *  through the tool suite and be written to binary output
         *  files.  As of 3.4.0, we modify the record on read.
         */
        if (skHeaderGetFileVersion(stream->silk_hdr) >= 16) {
            /* File created by SiLK 1.0 or later; most likely the
             * buggy value originated from an IPFIX source. */
            rwRecSetDPort(rwrec, rwRecGetSPort(rwrec));
        } else {
            switch(skHeaderGetFileFormat(stream->silk_hdr)) {
              case FT_RWFILTER:
              case FT_RWNOTROUTED:
              case FT_RWROUTED:
              case FT_RWSPLIT:
              case FT_RWWWW:
                /* Most likely from a PDU source */
                rwRecSetDPort(rwrec, BSWAP16(rwRecGetSPort(rwrec)));
                break;
              default:
                /* Assume it is from an IPFIX source */
                rwRecSetDPort(rwrec, rwRecGetSPort(rwrec));
                break;
            }
        }
        rwRecSetSPort(rwrec, 0);
    }

    /* Write to the copy-input stream */
    if (stream->silkflow.copy_input) {
        skStreamWriteRecord(stream->silkflow.copy_input, rwrec);
    }

    /* got a record */
    ++stream->rec_count;

    switch (stream->v6policy) {
      case SK_IPV6POLICY_MIX:
        break;

      case SK_IPV6POLICY_IGNORE:
        if (rwRecIsIPv6(rwrec)) {
            goto NEXT_RECORD;
        }
        break;

      case SK_IPV6POLICY_ASV4:
        if (rwRecIsIPv6(rwrec)) {
            if (rwRecConvertToIPv4(rwrec)) {
                goto NEXT_RECORD;
            }
        }
        break;

      case SK_IPV6POLICY_FORCE:
        if (!rwRecIsIPv6(rwrec)) {
            rwRecConvertToIPv6(rwrec);
        }
        break;

      case SK_IPV6POLICY_ONLY:
        if (!rwRecIsIPv6(rwrec)) {
            goto NEXT_RECORD;
        }
        break;
    }

    assert(SKSTREAM_OK == rv);
  END:
    return (stream->last_rv = rv);
}


int
skStreamReadSilkHeader(
    skstream_t         *stream,
    sk_file_header_t  **hdr)
{
    int rv;

    STREAM_RETURN_IF_NULL(stream);

    rv = streamSilkHeaderRead(stream, 0);
    if (hdr && rv == SKSTREAM_OK) {
        *hdr = stream->silk_hdr;
    }
    return (stream->last_rv = rv);
}


int
skStreamReadSilkHeaderStart(
    skstream_t         *stream)
{
    STREAM_RETURN_IF_NULL(stream);

    return stream->last_rv = streamSilkHeaderRead(stream, 1);
}


/* Allocate and return a buffer containing remainder of the stream */
void*
skStreamReadToEndOfFile(
    skstream_t         *stream,
    ssize_t            *count)
{
#define READTOEND_INITIAL_READ  4
#define READTOEND_BUFSIZE       1024

    uint8_t *buf = NULL;
    uint8_t *bp;
    ssize_t saw;
    ssize_t total = 0;
    size_t bufsize = 0;
    int rv;

    if (NULL == stream) {
        return NULL;
    }
    rv = streamCheckOpen(stream);
    if (rv) {
        stream->last_rv = rv;
        return NULL;
    }
    rv = streamCheckAttributes(stream, (SK_IO_READ | SK_IO_APPEND),
                               ~SK_CONTENT_SILK_FLOW);
    if (rv) {
        stream->last_rv = rv;
        return NULL;
    }

    for (;;) {
        if (bufsize < 4 * READTOEND_BUFSIZE) {
            bufsize += READTOEND_BUFSIZE;
        } else {
            bufsize += bufsize >> 1;
        }
        bp = (uint8_t*)realloc(buf, bufsize);
        if (NULL == bp) {
            stream->errnum = errno;
            stream->err_info = SKSTREAM_ERR_ALLOC;
            stream->last_rv = stream->err_info;
            break;
        }
        buf = bp;
        bp += total;

        if (!stream->is_dirty) {
            /* create the buffer for reading the stream and check
             * whether the input is compressed */
            saw = skStreamRead(stream, bp, READTOEND_INITIAL_READ);
            if (-1 == saw) {
                stream->last_rv = saw;
                break;
            }
            bp += saw;
            total += saw;
        }
        if (stream->iobuf.rec_buf.b_buf) {
            saw = streamIOBufRead(stream, bp, (bufsize - total));
        } else if (stream->basicbuf.b_buf) {
            saw = streamBasicBufRead(stream, bp, (bufsize - total));
        } else {
            assert(stream->zlib);
            saw = streamGZRead(stream, bp, (bufsize - total));
        }
        if (-1 == saw) {
            stream->last_rv = saw;
            break;
        }

        total += saw;
        if (saw < (ssize_t)(bufsize - total)) {
            *count = total;
            buf[total] = '\0';
            return buf;
        }
    }

    /* only get here on error */
    if (buf) {
        free(buf);
    }
    return NULL;
}


int
skStreamSetCommentStart(
    skstream_t         *stream,
    const char         *comment_start)
{
    int rv;

    STREAM_RETURN_IF_NULL(stream);

    rv = streamCheckAttributes(stream, SK_IO_READ, SK_CONTENT_TEXT);
    if (rv) { goto END; }

    /* clear existing value */
    if (stream->comment_start) {
        free(stream->comment_start);
    }

    /* set to new value */
    if (comment_start == NULL) {
        stream->comment_start = NULL;
    } else {
        stream->comment_start = strdup(comment_start);
        if (stream->comment_start == NULL) {
            rv = SKSTREAM_ERR_ALLOC;
            goto END;
        }
    }

    assert(SKSTREAM_OK == rv);
  END:
    return (stream->last_rv = rv);
}


#ifndef skStreamSetCompressionMethod
int
skStreamSetCompressionMethod(
    skstream_t         *stream,
    sk_compmethod_t     comp_method)
{
    int rv;

    STREAM_RETURN_IF_NULL(stream);
    rv = streamCheckAttributes(stream, SK_IO_WRITE,
                               SK_CONSTENT_SILK | SK_CONTENT_SILK_FLOW);
    if (rv) {
        return (stream->last_rv = rv);
    }
    return skHeaderSetCompressionMethod(stream->silk_hdr, comp_method);
}
#endif  /* #ifndef skStreamSetCompressionMethod */


int
skStreamSetCopyInput(
    skstream_t         *read_stream,
    skstream_t         *write_stream)
{
    int rv;

    STREAM_RETURN_IF_NULL(read_stream);
    if (write_stream == NULL) {
        return (read_stream->last_rv = SKSTREAM_ERR_NULL_ARGUMENT);
    }

    rv = streamCheckAttributes(read_stream, SK_IO_READ, SK_CONTENT_SILK_FLOW);
    if (rv) {
        return (read_stream->last_rv = rv);
    }
    rv = streamCheckAttributes(write_stream, SK_IO_WRITE,SK_CONTENT_SILK_FLOW);
    if (rv) {
        return (read_stream->last_rv = rv);
    }

    if (read_stream->silkflow.copy_input) {
        return (read_stream->last_rv = SKSTREAM_ERR_PREV_COPYINPUT);
    }
    if (read_stream->rec_count) {
        return (read_stream->last_rv = SKSTREAM_ERR_PREV_DATA);
    }

    read_stream->silkflow.copy_input = write_stream;
    return (read_stream->last_rv = SKSTREAM_OK);
}


int
skStreamSetIPv6Policy(
    skstream_t         *stream,
    sk_ipv6policy_t     policy)
{
    int rv;

    STREAM_RETURN_IF_NULL(stream);

    rv = streamCheckAttributes(stream, 0,
                               (SK_CONTENT_SILK | SK_CONTENT_SILK_FLOW));
    if (rv) { goto END; }

    stream->v6policy = policy;

    assert(SKSTREAM_OK == rv);
  END:
    return (stream->last_rv = rv);
}


int
skStreamSetSidecar(
    skstream_t         *stream,
    const sk_sidecar_t *sidecar)
{
    sk_file_header_t *hdr;
    sk_sidecar_t *sc;
    int rv;

    STREAM_RETURN_IF_NULL(stream);
    if (NULL == sidecar) {
        rv = SKSTREAM_ERR_NULL_ARGUMENT;
        goto END;
    }

    rv = streamCheckAttributes(stream, SK_IO_WRITE, SK_CONTENT_SILK_FLOW);
    if (rv) { goto END; }

    if (stream->is_dirty) {
        rv = SKSTREAM_ERR_PREV_DATA;
        goto END;
    }

    hdr = stream->silk_hdr;
    assert(stream->silk_hdr);

    rv = skHeaderSetFileVersion(hdr, SK_FILE_VERSION_BLOCK_HEADER);
    if (rv) { goto END; }

    /* remove any existing sidecar */
    rv = skHeaderRemoveAllMatching(hdr, SK_HENTRY_SIDECAR_ID);
    if (rv) {
        goto END;
    }
    if (stream->sidecar) {
        sk_sidecar_destroy((sk_sidecar_t **)&stream->sidecar);
    }

    if (sk_sidecar_add_to_header(sidecar, hdr)) {
        if (skHeaderGetLockStatus(hdr) != SKHDR_LOCK_MODIFIABLE) {
            rv = SKSTREAM_ERR_PREV_DATA;
        } else {
            rv = SKSTREAM_ERR_ALLOC;
        }
        goto END;
    }

    /* create a copy of the sidecar that the stream will own */
    sk_sidecar_copy(&sc, sidecar);
    stream->sidecar = sc;

    assert(SKSTREAM_OK == rv);
  END:
    return (stream->last_rv = rv);
}


int
skStreamSetUnbuffered(
    skstream_t         *stream)
{
    int rv;

    STREAM_RETURN_IF_NULL(stream);

    rv = streamCheckUnopened(stream);
    if (rv) { goto END; }

    stream->is_unbuffered = 1;

    assert(SKSTREAM_OK == rv);
  END:
    return (stream->last_rv = rv);
}


int
skStreamSkipRecords(
    skstream_t         *stream,
    size_t              skip_count,
    size_t             *records_skipped)
{
    size_t local_records_skipped;
    ssize_t saw;
    ssize_t tmp;
    int rv;

    STREAM_RETURN_IF_NULL(stream);

    rv = streamCheckOpen(stream);
    if (rv) { goto END; }

    if (stream->is_eof) {
        rv = SKSTREAM_ERR_EOF;
        goto END;
    }

    if (NULL == records_skipped) {
        records_skipped = &local_records_skipped;
    }
    *records_skipped = 0;

    /* FIXME: Read all records individually due to sidecar */

    /* when some other stream is expecting to see the records, we need
     * to read each record individually */
    /*if (stream->silkflow.copy_input) */
    {
        size_t skipped = skip_count;
        rwRec rec;

        rwRecInitialize(&rec, NULL);

        while ((skipped > 0)
               && ((rv = skStreamReadRecord(stream, &rec)) == SKSTREAM_OK))
        {
            --skipped;
        }
        *records_skipped = skip_count - skipped;
        goto END;
    }


    while (skip_count > 0) {
        if (skip_count > (size_t)SSIZE_MAX / stream->rec_len) {
            tmp = SSIZE_MAX;
        } else {
            tmp = stream->rec_len * skip_count;
        }

        saw = streamIOBufRead(stream, NULL, tmp);
        if (-1 == saw) {
            rv = SKSTREAM_ERR_READ;
            goto END;
        }

        /* compute the number of records we actually read, update
         * counters, and check for any partially read records. */
        tmp = (saw / stream->rec_len);
        stream->rec_count += tmp;
        skip_count -= tmp;
        saw -= tmp * stream->rec_len;
        *records_skipped += tmp;

        if (saw != 0) {
            stream->errobj.num = saw;
            rv = SKSTREAM_ERR_READ_SHORT;
            goto END;
        }
        if (stream->is_eof) {
            rv = SKSTREAM_ERR_EOF;
            goto END;
        }
    }

    rv = SKSTREAM_OK;

    assert(SKSTREAM_OK == rv);
  END:
    return (stream->last_rv = rv);
}


void
skStreamTeardown(
    void)
{
    /* nothing to do */
}


off_t
skStreamTell(
    skstream_t         *stream)
{
    off_t pos;
    int rv;

    STREAM_RETURN_IF_NULL(stream);

    rv = streamCheckOpen(stream);
    if (rv) {
        stream->err_info = rv;
        return (stream->last_rv = -1);
    }

    pos = lseek(stream->fd, 0, SEEK_CUR);
    if (pos == (off_t)-1) {
        stream->errnum = errno;
        stream->err_info = SKSTREAM_ERR_SYS_LSEEK;
    }

    return (stream->last_rv = pos);
}


int
skStreamTruncate(
    skstream_t         *stream,
    off_t               length)
{
    int rv;

    STREAM_RETURN_IF_NULL(stream);

    rv = streamCheckOpen(stream);
    if (rv) { goto END; }

    rv = streamCheckAttributes(stream, (SK_IO_WRITE | SK_IO_APPEND),
                               (SK_CONTENT_SILK | SK_CONTENT_SILK_FLOW
                                | SK_CONTENT_OTHERBINARY));
    if (rv) { goto END; }

    if ( !stream->is_seekable) {
        rv = SKSTREAM_ERR_NOT_SEEKABLE;
        goto END;
    }

    /* attempt to truncate the file even if flush fails */
    rv = skStreamFlush(stream);
    if (ftruncate(stream->fd, length) == -1) {
        stream->errnum = errno;
        rv = SKSTREAM_ERR_SYS_FTRUNCATE;
    }

  END:
    return (stream->last_rv = rv);
}


int
skStreamUnbind(
    skstream_t         *stream)
{
    int rv = SKSTREAM_OK;

    if (!stream) {
        return rv;
    }
    if (stream->fd != -1) {
        rv = skStreamClose(stream);
    }

    if (stream->comment_start) {
        free(stream->comment_start);
        stream->comment_start = NULL;
    }
    if (stream->pager) {
        free(stream->pager);
        stream->pager = NULL;
    }
    if (stream->pathname) {
        free(stream->pathname);
        stream->pathname = NULL;
    }

    return (stream->last_rv = rv);
}


ssize_t
skStreamWrite(
    skstream_t         *stream,
    const void         *buf,
    size_t              count)
{
    int rv;

    if (NULL == stream) {
        return -1;
    }
    rv = streamCheckOpen(stream);
    if (rv) {
        stream->err_info = rv;
        return (stream->last_rv = -1);
    }

    switch (stream->content_type) {
      case SK_CONTENT_TEXT:
        stream->err_info = SKSTREAM_ERR_UNSUPPORT_CONTENT;
        return (stream->last_rv = -1);
      case SK_CONTENT_OTHERBINARY:
        if (SK_IO_WRITE != stream->io_mode) {
            stream->err_info = SKSTREAM_ERR_UNSUPPORT_IOMODE;
            return (stream->last_rv = -1);
        }
        break;
      case SK_CONTENT_SILK:
        if (SK_IO_WRITE != stream->io_mode) {
            if (SK_IO_APPEND != stream->io_mode) {
                stream->err_info = SKSTREAM_ERR_UNSUPPORT_IOMODE;
                return (stream->last_rv = -1);
            }
            if (!stream->have_hdr) {
                /* may only call write on silk stream open for append
                 * once the header has been read */
                stream->err_info = SKSTREAM_ERR_UNSUPPORT_CONTENT;
                return (stream->last_rv = -1);
            }
        }
        break;
      case SK_CONTENT_SILK_FLOW:
        if (!stream->is_dirty || stream->have_hdr) {
            /* May only use skStreamWrite() on a flow stream while
             * writing the header, and skStreamWriteSilkHeader()
             * should have set the is_dirty flag. */
            stream->err_info = SKSTREAM_ERR_UNSUPPORT_CONTENT;
            return (stream->last_rv = -1);
        }
        if (SK_IO_WRITE != stream->io_mode) {
            stream->err_info = SKSTREAM_ERR_UNSUPPORT_IOMODE;
            return (stream->last_rv = -1);
        }
        break;
      default:
        skAbortBadCase(stream->content_type);
    }

    for (;;) {
        if (stream->iobuf.rec_buf.b_buf) {
            assert(SK_CONTENT_SILK == stream->content_type);
            return (stream->last_rv = streamIOBufWrite(stream, buf, count));
        }
        if (stream->basicbuf.b_buf) {
            return (stream->last_rv = streamBasicBufWrite(stream, buf, count));
        }
        if (stream->zlib) {
            return (stream->last_rv = streamGZWrite(stream, buf, count));
        }
        if (stream->is_dirty) {
            skAppPrintErr("Stream '%s' does not have a write buffer",
                          stream->pathname);
            skAbort();
        }
        if (SK_CONTENT_SILK == stream->content_type) {
            rv = skStreamWriteSilkHeader(stream);
            if (rv) {
                stream->err_info = rv;
                return (stream->last_rv = -1);
            }
            assert(stream->have_hdr);
        } else if (SK_CONTENT_OTHERBINARY == stream->content_type) {
            rv = streamWritePrepare(stream);
            if (rv) { return rv; }
        } else {
            skAbortBadCase(stream->content_type);
        }
        if (!stream->is_dirty) {
            skAbort();
        }
    }
}


int
skStreamWriteRecord(
    skstream_t         *stream,
    const rwRec        *rwrec)
{
#ifndef SK_HAVE_ALIGNED_ACCESS_REQUIRED
    uint8_t ar[SK_MAX_RECORD_SIZE];
#else
    /* force 'ar' to be aligned on an 8byte boundary, since we treat
     * it as an rwRec and need to access the 64bit sTime. */
    union force_align_un {
        uint8_t  fa_ar[SK_MAX_RECORD_SIZE];
        uint64_t fa_u64;
    } force_align;
    uint8_t *ar = force_align.fa_ar;
#endif  /* SK_HAVE_ALIGNED_ACCESS_REQUIRED */

    rwRec rec_copy;
    int rv;
    const rwRec *rp = rwrec;

    STREAM_RETURN_IF_NULL(stream);

    rv = streamCheckOpen(stream);
    if (rv) { goto END; }
    rv = streamCheckAttributes(stream, (SK_IO_WRITE | SK_IO_APPEND),
                               SK_CONTENT_SILK_FLOW);
    if (rv) { goto END; }

    if (!stream->have_hdr) {
        if (SK_IO_APPEND == stream->io_mode) {
            rv = streamSilkHeaderRead(stream, 0);
        } else {
            rv = skStreamWriteSilkHeader(stream);
        }
        if (rv) { goto END; }
    }

    if (rwRecIsIPv6(rp)) {
        switch (stream->v6policy) {
          case SK_IPV6POLICY_MIX:
          case SK_IPV6POLICY_FORCE:
          case SK_IPV6POLICY_ONLY:
            /* flow already IPv6; verify that file format supports it */
            if (stream->supports_ipv6 == 0) {
                rv = SKSTREAM_ERR_UNSUPPORT_IPV6;
                goto END;
            }
            break;

          case SK_IPV6POLICY_IGNORE:
            /* we're ignoring IPv6, return */
            assert(SKSTREAM_OK == rv);
            goto END;

          case SK_IPV6POLICY_ASV4:
            /* attempt to convert IPv6 flow to v4 */
            memcpy(&rec_copy, rp, sizeof(rwRec));
            if (rwRecConvertToIPv4(&rec_copy)) {
                assert(SKSTREAM_OK == rv);
                goto END;
            }
            rp = &rec_copy;
            break;
        }
    } else {
        /* flow is IPv4 */
        switch (stream->v6policy) {
          case SK_IPV6POLICY_MIX:
          case SK_IPV6POLICY_IGNORE:
          case SK_IPV6POLICY_ASV4:
            /* flow is already IPv4; all file formats supported */
            break;

          case SK_IPV6POLICY_ONLY:
            /* we're ignoring IPv4 flows; return */
            assert(SKSTREAM_OK == rv);
            goto END;

          case SK_IPV6POLICY_FORCE:
            /* must convert flow to IPv6, but first verify that file
             * format supports IPv6 */
            if (stream->supports_ipv6 == 0) {
                rv = SKSTREAM_ERR_UNSUPPORT_IPV6;
                goto END;
            }
            /* convert */
            memcpy(&rec_copy, rp, sizeof(rwRec));
            rp = &rec_copy;
            rwRecConvertToIPv6(&rec_copy);
            break;
        }
    }

    /* Convert the record into a byte array in the appropriate byte order */
    rv = stream->silkflow.pack(stream, rp, ar);
    if (rv != SKSTREAM_OK) {
        stream->errobj.rec = rwrec;
        goto END;
    }

    if (NULL == stream->sidecar) {
        /* No sidecar header on stream; write the fixed-size rwRec */
        if (stream->iobuf.rec_buf.b_buf) {
            rv = streamIOBufWrite(stream, ar, stream->rec_len);
        } else if (stream->basicbuf.b_buf) {
            rv = streamBasicBufWrite(stream, ar, stream->rec_len);
        } else {
            assert(stream->zlib);
            rv = streamGZWrite(stream, ar, stream->rec_len);
        }
        if (rv != stream->rec_len) {
            rv = -1;
            goto END;
        }

    } else if (!rp->lua_state) {
        /* Sidecar description in the stream's header but no Lua state
         * on the record; must write empty sidecar data */
        uint16_t empty = 2 * sizeof(uint16_t);
        uint16_t empty_ns;

        if (streamIOBufCheckAvail(stream, stream->rec_len, empty)) {
            rv = -1;
            goto END;
        }
        /* handle fixed-size portion */
        memcpy(stream->iobuf.rec_buf.b_pos, ar, stream->rec_len);
        stream->iobuf.rec_buf.b_avail -= stream->rec_len;
        stream->iobuf.rec_buf.b_pos += stream->rec_len;
        /* handle the empty portion */
        empty_ns = htons(empty);
        memcpy(stream->iobuf.sc_buf.b_pos, &empty_ns, sizeof(empty_ns));
        memset(stream->iobuf.sc_buf.b_pos + sizeof(empty), 0, sizeof(empty));
        stream->iobuf.sc_buf.b_avail -= empty;
        stream->iobuf.sc_buf.b_pos += empty;

    } else {
        size_t buflen;

        if (streamIOBufCheckAvail(stream, stream->rec_len,
                                  2 * sizeof(uint16_t)))
        {
            rv = -1;
            goto END;
        }
        for (;;) {
            buflen = stream->iobuf.sc_buf.b_avail;
            rv = sk_sidecar_serialize_data(
                stream->sidecar, rp->lua_state, rwRecGetSidecar(rp),
                stream->iobuf.sc_buf.b_pos, &buflen);
            if (SK_SIDECAR_OK == rv) {
                assert(stream->iobuf.sc_buf.b_avail >= buflen);
                stream->iobuf.sc_buf.b_avail -= buflen;
                stream->iobuf.sc_buf.b_pos += buflen;

                /* handle fixed-size portion */
                assert(stream->iobuf.rec_buf.b_avail >= stream->rec_len);
                memcpy(stream->iobuf.rec_buf.b_pos, ar, stream->rec_len);
                stream->iobuf.rec_buf.b_avail -= stream->rec_len;
                stream->iobuf.rec_buf.b_pos += stream->rec_len;
                break;
            }
            if (SK_SIDECAR_E_NO_SPACE != rv) {
                skAppPrintErr(
                    "Unexpected return code %d from sidecar_serialize", rv);
                skAbort();
            }
            if (streamIOBufFlush(stream) == -1) {
                rv = -1;
                goto END;
            }
        }
    }

    ++stream->rec_count;
    rv = SKSTREAM_OK;

  END:
    return (stream->last_rv = rv);
}


int
skStreamWriteSilkHeader(
    skstream_t         *stream)
{
    sk_file_header_t *hdr;
    sk_compmethod_t compmethod;
    int rv;

    STREAM_RETURN_IF_NULL(stream);

    rv = streamCheckOpen(stream);
    if (rv) { goto END; }

    rv = streamCheckAttributes(stream, SK_IO_WRITE,
                               (SK_CONTENT_SILK | SK_CONTENT_SILK_FLOW));
    if (rv) { goto END; }

    if (stream->is_dirty) {
        rv = SKSTREAM_ERR_PREV_DATA;
        goto END;
    }

    assert(stream->is_silk);
    assert(stream->silk_hdr);
    hdr = stream->silk_hdr;

    /* handle the case where a specific record type has not yet
     * been specified. */
    if (skHeaderGetFileFormat(hdr) == UINT8_MAX) {
        if (SK_CONTENT_SILK == stream->content_type) {
            /* do not set format if content is not silk flow */
            return SKHEADER_ERR_BAD_FORMAT;
        }
        rv = skHeaderSetFileFormat(hdr, DEFAULT_FILE_FORMAT);
        if (rv) { goto END; }
    }

    /* unless a specific compression method was specified, do not use
     * compression when writing to a non-seekable destination */
    compmethod = skHeaderGetCompressionMethod(hdr);
    if (SK_COMPMETHOD_DEFAULT == compmethod
        || SK_COMPMETHOD_BEST == compmethod)
    {
        if (!stream->is_seekable && !stream->is_mpi) {
            compmethod = SK_COMPMETHOD_NONE;
        } else if (SK_COMPMETHOD_DEFAULT == compmethod) {
            compmethod = skCompMethodGetDefault();
        } else {
            assert(SK_COMPMETHOD_BEST == compmethod);
            compmethod = skCompMethodGetBest();
        }
        rv = skHeaderSetCompressionMethod(hdr, compmethod);
        if (rv) { goto END; }
    }

    /* check whether this stream contains flow data */
    if (SK_CONTENT_SILK_FLOW == stream->content_type) {
        /* caller expects flow records */
        assert(stream->is_silk_flow);
        STREAM_SET_IS_SILK_FLOW(stream);
        if (!stream->is_silk_flow) {
            return SKSTREAM_ERR_REQUIRE_SILK_FLOW;
        }
    }
    assert((SK_CONTENT_SILK == stream->content_type && !stream->is_silk_flow)
           || (SK_CONTENT_SILK_FLOW == stream->content_type
               && stream->is_silk_flow));

    if (stream->is_silk_flow) {
        /* Set the file version if it is "ANY", and set pointers to
         * the PackFn and UnpackFn functions for this file format. */
        rv = streamSilkFlowPrepare(stream);
        if (rv) { goto END; }

        assert(stream->rec_len > 0);
        assert(stream->rec_len <= SK_MAX_RECORD_SIZE);
        assert(stream->rec_len == skHeaderGetRecordLength(stream->silk_hdr));

        /* Set the swap_flag */
        stream->swap_flag = !skHeaderIsNativeByteOrder(hdr);

        /* Cache values from the packedfile header */
        streamSilkFlowCacheHeader(stream);

        /* Ensure the stream and its header are in sync: If the stream
         * has a sidecar entry, recreate the sidecar entry in the
         * header. If there is no sidecar object on the stream but the
         * header has a sidecar entry, create a sidecar object on the
         * stream. */
        if (stream->sidecar) {
            rv = skHeaderRemoveAllMatching(hdr, SK_HENTRY_SIDECAR_ID);
            if (rv) {
                goto END;
            }
            if (sk_sidecar_add_to_header(stream->sidecar, hdr)) {
                if (skHeaderGetLockStatus(hdr) != SKHDR_LOCK_MODIFIABLE) {
                    rv = SKSTREAM_ERR_PREV_DATA;
                } else {
                    rv = SKSTREAM_ERR_ALLOC;
                }
                goto END;
            }
        } else {
            int err;

            stream->sidecar
                = sk_sidecar_create_from_header(stream->silk_hdr, &err);
            if (err) {
                rv = SKSTREAM_ERR_ALLOC;
                goto END;
            }
        }

        if (stream->sidecar
            && skHeaderGetFileVersion(hdr) != SK_FILE_VERSION_BLOCK_HEADER)
        {
            rv = skHeaderSetFileVersion(hdr, SK_FILE_VERSION_BLOCK_HEADER);
            if (rv) { goto END; }
        }
    }

    skHeaderSetLock(hdr, SKHDR_LOCK_FIXED);

    if ( !stream->is_mpi) {
        rv = streamWritePrepare(stream);
        if (rv) { goto END; }
        rv = skHeaderWrite(stream, hdr);
        if (rv) { goto END; }
    }

    if (0 == stream->rec_len) {
        stream->rec_len = skHeaderGetRecordLength(stream->silk_hdr);
    }
    assert(stream->rec_len > 0);
    assert(stream->rec_len <= SK_MAX_RECORD_SIZE);
    assert(stream->rec_len == skHeaderGetRecordLength(stream->silk_hdr));

    stream->have_hdr = 1;
    if (skHeaderGetFileVersion(hdr) == SK_FILE_VERSION_BLOCK_HEADER
        || compmethod != SK_COMPMETHOD_NONE)
    {
        rv = streamIOBufCreate(stream);
        if (rv) { goto END; }
    }

    assert(SKSTREAM_OK == rv);
  END:
    return (stream->last_rv = rv);
}


/*
 *    These are used heavily by skstream, so define them here.
 */

/* Read count bytes from a file descriptor into buf */
ssize_t
skreadn(
    int                 fd,
    void               *buf,
    size_t              count)
{
    ssize_t rv;
    size_t  left = count;

    while (left) {
        rv = read(fd, buf, ((left < INT32_MAX) ? left : INT32_MAX));
        if (rv == -1) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (rv == 0) {
            break;
        }
        left -= rv;
        buf = ((uint8_t *)buf) + rv;
    }
    return (count - left);
}


/* Read count bytes from buf to a file descriptor */
ssize_t
skwriten(
    int                 fd,
    const void         *buf,
    size_t              count)
{
    ssize_t rv;
    size_t  left = count;

    while (left) {
        rv = write(fd, buf, ((left < INT32_MAX) ? left : INT32_MAX));
        if (rv == -1) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (rv == 0) {
            break;
        }
        left -= rv;
        buf = ((uint8_t *)buf) + rv;
    }
    return (count - left);
}


/*
** Local Variables:
** mode:c
** indent-tabs-mode:nil
** c-basic-offset:4
** End:
*/
