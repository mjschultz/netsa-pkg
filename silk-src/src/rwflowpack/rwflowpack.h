/*
** Copyright (C) 2004-2020 by Carnegie Mellon University.
**
** @OPENSOURCE_LICENSE_START@
** See license information in ../../LICENSE.txt
** @OPENSOURCE_LICENSE_END@
*/

/*
**  rwflowpack.h
**
**    This header defines the structure of function pointers used by a
**    packing-logic plug-in for rwflowpack.
**
*/
#ifndef _RWFLOWPACK_H
#define _RWFLOWPACK_H
#ifdef __cplusplus
extern "C" {
#endif

#include <silk/silk.h>

RCSIDENTVAR(rcsID_RWFLOWPACK_H, "$SiLK: rwflowpack.h ef14e54179be 2020-04-14 21:57:45Z mthomas $");

#include <silk/silk_types.h>
#include <silk/libflowsource.h>

/**
 *  @file
 *
 *    Interface between rwflowpack and the packing logic plug-in that
 *    is used to decide how to pack each flow record.
 *
 *    This file is part of libflowsource.
 */


/**
 *    The maximum number of flowtype/sensors that a single flow can be
 *    packed to at one time.  Used to set array sizes.
 */
#define MAX_SPLIT_FLOWTYPES 16


/**
 *    Name of the function that rwflowpack calls when the plug-in is
 *    first loaded.
 */
#define SK_PACKLOGIC_INIT  "packLogicInitialize"


/**
 *    packing logic plug-in
 */
typedef struct packlogic_plugin_st packlogic_plugin_t;
struct packlogic_plugin_st {
    /**
     *    handle returned by dlopen()
     */
    void               *handle;

    /**
     *    path to the plugin
     */
    char               *path;

    /**
     *  Site-specific initialization function called when the plug-in
     *  is first loaded during options processing.  This funciton is
     *  called with this structure as its argument; it should set the
     *  function pointers listed below.
     */
    int
    (*initialize_fn)(
        packlogic_plugin_t *packlogic);

    /**
     *  Site-specific setup function, called after the site
     *  configuration file (silk.conf) has been loaded but before
     *  parsing the sensor.conf file.
     */
    int
    (*setup_fn)(
        void);

    /**
     *  Site-specific teardown function.
     */
    void
    (*teardown_fn)(
        void);


    /**
     *  Site-specific function to verify that a sensor has all the
     *  information it requires to pack flow records.
     */
    int
    (*verify_sensor_fn)(
        skpc_sensor_t  *sensor);

    /**
     *  A function that determines the flow type(s) and sensorID(s) of
     *  a flow record 'rwrec' and was collected from the 'probe'.  The
     *  function will compare the SNMP interfaces on the record with
     *  those specified in the probe for external, internal, and null
     *  flows.
     *
     *  'ftypes' and 'sensorids' should each be arrays having enough
     *  space to hold the expected number of flow_types and sensorIDs;
     *  NUM_FLOW_TYPES is the maximum that could be returned.  These
     *  arrays will be populated with the flow_type and sensorID
     *  pair(s) into which the record should be packed.
     *
     *  Excepting errors, the return value is always the number of
     *  flow_type/sensorID pairs into which the record should be
     *  packed.
     *
     *  A return value of 0 indicates no packing rules existed for
     *  this record from this probe; a value of -1 indicates an error
     *  condition.
     */
    int
    (*determine_flowtype_fn)(
        const skpc_probe_t *probe,
        const rwRec        *rwrec,
        sk_flowtype_id_t   *ftypes,
        sk_sensor_id_t     *sensorids);

    /**
     *  A function that determines the record format and version to
     *  use for records whose flowtype is 'ftype'.  The 'probe'
     *  parameter contains the probe where records are collected.
     *
     *  The caller is expected to set 'version' and return the format.
     *
     *  Added in SiLK 3.17.0.  If not defined, the
     *  determine_fileformat_fn is called if it is defined.
     */
    sk_file_format_t
    (*determine_formatversion_fn)(
        const skpc_probe_t *probe,
        sk_flowtype_id_t    ftype,
        sk_file_version_t  *version);

    /**
     *  A function that determines the file format to use for records
     *  whose flowtype is 'ftype'.  The 'probe' parameter contains the
     *  probe where records are collected.
     */
    sk_file_format_t
    (*determine_fileformat_fn)(
        const skpc_probe_t *probe,
        sk_flowtype_id_t    ftype);
};


/**
 *    Function that must exist in the packing logic plug-in.  This
 *    function should set the function pointers on 'packlogic'.
 */
int
packLogicInitialize(
    packlogic_plugin_t *packlogic);


#ifdef __cplusplus
}
#endif
#endif /* _RWFLOWPACK_H */

/*
** Local Variables:
** mode:c
** indent-tabs-mode:nil
** c-basic-offset:4
** End:
*/
