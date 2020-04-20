/*
** Copyright (C) 2008-2020 by Carnegie Mellon University.
**
** @OPENSOURCE_LICENSE_START@
** See license information in ../../LICENSE.txt
** @OPENSOURCE_LICENSE_END@
*/

/*
**  skoptions-notes.c
**
**    Provide support for the --note-add, --note-file-add, and
**    --note-strip switches.
**
*/


#include <silk/silk.h>

RCSIDENT("$SiLK: skoptions-notes.c ef14e54179be 2020-04-14 21:57:45Z mthomas $");

#include <silk/utils.h>
#include <silk/skstream.h>
#include <silk/skvector.h>


/* LOCAL DEFINES AND TYPEDEFS */

typedef enum {
    OPT_NOTE_STRIP,
    OPT_NOTE_ADD,
    OPT_NOTE_FILE_ADD
} noteopt_type_t;

typedef struct noteopt_arg_st {
    noteopt_type_t  type;
    const char     *arg;
} noteopt_arg_t;


/* LOCAL VARIABLE DEFINITIONS */

/* a vector of noteopt_arg_t that is filled in by the user's use of
 * the --note-add and --note-file-add switches. */
static sk_vector_t *noteopt_vec = NULL;

/* whether the application wants to ignore the --note-strip option */
static int noteopt_strip_ignored = 0;



/* OPTIONS SETUP */

static struct option noteopt_options[] = {
    {"note-strip",          NO_ARG,       0, OPT_NOTE_STRIP},
    {"note-add",            REQUIRED_ARG, 0, OPT_NOTE_ADD},
    {"note-file-add",       REQUIRED_ARG, 0, OPT_NOTE_FILE_ADD},
    {0,0,0,0}               /* sentinel */
};

static const char *noteopt_help[] = {
    "Do not copy notes from the input files to the output file",
    ("Store the textual argument in the output SiLK file's header\n"
     "\tas an annotation. Switch may be repeated to add multiple annotations"),
    ("Store the content of the named text file in the output\n"
     "\tSiLK file's header as an annotation.  Switch may be repeated."),
    (char*)NULL
};


/* FUNCTION DEFINITIONS */


static int
noteopt_handler(
    clientData          cData,
    int                 opt_index,
    char               *opt_arg)
{
    int *note_strip = (int*)cData;
    noteopt_arg_t note;

    switch ((noteopt_type_t)opt_index) {
      case OPT_NOTE_ADD:
      case OPT_NOTE_FILE_ADD:
        note.type = (noteopt_type_t)opt_index;
        note.arg = opt_arg;
        if (noteopt_vec == NULL) {
            noteopt_vec = skVectorNew(sizeof(noteopt_arg_t));
            if (noteopt_vec == NULL) {
                skAppPrintOutOfMemory(NULL);
                return 1;
            }
        }
        if (skVectorAppendValue(noteopt_vec, &note)) {
            skAppPrintOutOfMemory(NULL);
            return 1;
        }
        break;

      case OPT_NOTE_STRIP:
        assert(noteopt_strip_ignored == 0);
        *note_strip = 1;
        break;

    }

    return 0;
}


int
skOptionsNotesRegister(
    int                *note_strip)
{
    if (note_strip == NULL) {
        noteopt_strip_ignored = 1;
    }

    assert((sizeof(noteopt_options)/sizeof(struct option))
           == (sizeof(noteopt_help)/sizeof(char*)));

    return skOptionsRegister(&noteopt_options[noteopt_strip_ignored],
                             &noteopt_handler, (clientData)note_strip);
}


void
skOptionsNotesTeardown(
    void)
{
    if (noteopt_vec) {
        skVectorDestroy(noteopt_vec);
    }
    noteopt_vec = NULL;
}


void
skOptionsNotesUsage(
    FILE               *fh)
{
    int i;

    for (i = noteopt_strip_ignored; noteopt_options[i].name; ++i) {
        fprintf(fh, "--%s %s. %s\n", noteopt_options[i].name,
                SK_OPTION_HAS_ARG(noteopt_options[i]), noteopt_help[i]);
    }
}


int
skOptionsNotesAddToStream(
    skstream_t         *stream)
{
    sk_file_header_t *hdr = skStreamGetSilkHeader(stream);
    noteopt_arg_t *note;
    size_t i;
    int rv = 0;

    if (noteopt_vec) {
        for (i = 0;
             ((note = (noteopt_arg_t*)skVectorGetValuePointer(noteopt_vec, i))
              != NULL);
             ++i)
        {
            if (note->type == OPT_NOTE_ADD) {
                rv = skHeaderAddAnnotation(hdr, note->arg);
            } else {
                rv = skHeaderAddAnnotationFromFile(hdr, note->arg);
            }
            if (rv) {
                return rv;
            }
        }
    }

    return rv;
}


/*
** Local Variables:
** mode:c
** indent-tabs-mode:nil
** c-basic-offset:4
** End:
*/
