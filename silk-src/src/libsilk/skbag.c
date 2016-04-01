/*
** Copyright (C) 2004-2016 by Carnegie Mellon University.
**
** @OPENSOURCE_HEADER_START@
**
** Use of the SILK system and related source code is subject to the terms
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
*/

/*
**  skbag.c
**
**    Implementation of skbag according to skbag.h
**
*/

#include <silk/silk.h>

RCSIDENT("$SiLK: skbag.c 5f2d30e80a6f 2016-03-30 22:09:49Z mthomas $");

#include <silk/hashlib.h>
#include <silk/redblack.h>
#include <silk/skbag.h>
#include <silk/skipaddr.h>
#include <silk/skmempool.h>
#include <silk/skstream.h>
#include <silk/utils.h>


/* LOCAL DEFINES AND TYPEDEFS */

/*
 *    Version number to write into the Bag's header.  KEY_FIXED
 *    assumes a fixed key of 4 bytes and a counter of 8 bytes;
 *    KEY_VARIES allows for variable key and value sizes.
 */
#define RWBAG_FILE_VERS_KEY_FIXED   3
#define RWBAG_FILE_VERS_KEY_VARIES  4


/*    Whether to use memcpy() */
#ifndef SKBAG_USE_MEMCPY
#ifdef SK_HAVE_ALIGNED_ACCESS_REQUIRED
#define SKBAG_USE_MEMCPY 1
#else
#define SKBAG_USE_MEMCPY 0
#endif
#endif


/*    For IPv6, whether the store the keys and values in the
 *    hashlib(1) or the redblack tree(0). */
#ifndef SKBAG_USE_HASHLIB
#define SKBAG_USE_HASHLIB 0
#endif


/*    Initializer for libhash hash table creation */
#define HASH_INITIAL_SIZE           500000

/*    Maximum number of octets allowed for keys and counters */
#define BAG_KEY_MAX_OCTETS          16
#define BAG_COUNTER_MAX_OCTETS      8


#if !SK_ENABLE_IPV6

/*
 *  BAG_KEY_TO_U32_V4(key, value)
 *
 *    Given the skBagTypedKey_t 'key', fill 'value' with a uint32_t
 *    representation of that value.
 */
#define BAG_KEY_TO_U32_V4(k2int_key, k2int_u32)                 \
    switch ((k2int_key)->type) {                                \
      case SKBAG_KEY_U8:                                        \
        k2int_u32 = (k2int_key)->val.u8;                        \
        break;                                                  \
      case SKBAG_KEY_U16:                                       \
        k2int_u32 = (k2int_key)->val.u16;                       \
        break;                                                  \
      case SKBAG_KEY_U32:                                       \
        k2int_u32 = (k2int_key)->val.u32;                       \
        break;                                                  \
      case SKBAG_KEY_IPADDR:                                    \
        k2int_u32 = skipaddrGetV4(&(k2int_key)->val.addr);      \
        break;                                                  \
      default:                                                  \
        skAbortBadCase((k2int_key)->type);                      \
    }

#else  /* #if !SK_ENABLE_IPV6 */

/*
 *  BAG_KEY_TO_U32_V6(key, value, is_v6)
 *
 *    Given the skBagTypedKey_t 'key', fill 'value' with a uint32_t
 *    representation of that value.  If 'key' contains an IPv6 address
 *    that is not an IPv4-encoded value, set 'is_v6' to 1.  If 'value'
 *    can store the key, set is_v6 is 0.
 */
#define BAG_KEY_TO_U32_V6(k2int_key, k2int_u32, k2int_isv6)             \
    switch ((k2int_key)->type) {                                        \
      case SKBAG_KEY_U8:                                                \
        k2int_isv6 = 0;                                                 \
        k2int_u32 = (k2int_key)->val.u8;                                \
        break;                                                          \
      case SKBAG_KEY_U16:                                               \
        k2int_isv6 = 0;                                                 \
        k2int_u32 = (k2int_key)->val.u16;                               \
        break;                                                          \
      case SKBAG_KEY_U32:                                               \
        k2int_isv6 = 0;                                                 \
        k2int_u32 = (k2int_key)->val.u32;                               \
        break;                                                          \
      case SKBAG_KEY_IPADDR:                                            \
        k2int_isv6 = skipaddrGetAsV4(&(k2int_key)->val.addr, &(k2int_u32)); \
        break;                                                          \
      default:                                                          \
        skAbortBadCase((k2int_key)->type);                              \
    }

/*
 *  BAG_KEY_TO_IPV6(key, ipv6_array)
 *
 *    Given the skBagTypedKey_t 'key', fill 'ipv6_array' with the
 *    byte-array representation of that value as an IPv6 address,
 *    where any integer value is written an an IPv4-encoded-IPv6
 *    address.
 */
#define BAG_KEY_TO_IPV6(k2ip_key, k2ip_array)                   \
    switch ((k2ip_key)->type) {                                 \
      case SKBAG_KEY_U8:                                        \
        memcpy((k2ip_array), bag_v4inv6, 15);                   \
        k2ip_array[15] = key->val.u8;                           \
        break;                                                  \
      case SKBAG_KEY_U16:                                       \
        memcpy((k2ip_array), bag_v4inv6, 14);                   \
        *(uint16_t*)(k2ip_array + 14) = htons(key->val.u16);    \
        break;                                                  \
      case SKBAG_KEY_U32:                                       \
        memcpy((k2ip_array), bag_v4inv6, 12);                   \
        *(uint32_t*)(k2ip_array + 12) = htonl(key->val.u32);    \
        break;                                                  \
      case SKBAG_KEY_IPADDR:                                    \
        skipaddrGetAsV6(&(k2ip_key)->val.addr, k2ip_array);     \
        break;                                                  \
      default:                                                  \
        skAbortBadCase((k2ip_key)->type);                       \
    }

#endif  /* else if #if !SK_ENABLE_IPV6 */


/*
 *  BAG_CHECK_INPUT(bag, key, counter);
 *
 *    Verify that 'bag', 'key', and 'counter' are not NULL, that the
 *    types of the skBagTypedKey_t 'key' and skBagTypedCounter_t
 *    'counter' are valid for input (that is, are not ANY), and that
 *    the value of 'counter' is not bag_counter_invalid.
 *
 *    Causes the function to return SKBAG_ERR_INPUT if any of those
 *    tests fail.
 */
#define BAG_CHECK_INPUT(bci_bag, bci_key, bci_counter)                  \
    if (NULL == bci_bag || NULL == bci_key || NULL == bci_counter       \
        || SKBAG_KEY_ANY == bci_key->type                               \
        || SKBAG_COUNTER_ANY == bci_counter->type                       \
        || bag_counter_invalid == bci_counter->val.u64)                 \
    {                                                                   \
        return SKBAG_ERR_INPUT;                                         \
    }


/*
 *  BAG_COUNTER_SET(counter, value);
 *
 *    Set 'counter', a pointer to an skBagTypedCounter_t, to 'value'.
 *    Note that 'value' is NOT a pointer.
 */
#define BAG_COUNTER_SET(bcs_counter, bcs_value)         \
    {                                                   \
        (bcs_counter)->type = SKBAG_COUNTER_U64;        \
        (bcs_counter)->val.u64 = (uint64_t)(bcs_value); \
    }


/*
 *  BAG_COUNTER_SET_ZERO(counter);
 *
 *    Set 'counter', a pointer to an skBagTypedCounter_t, to 0.
 */
#define BAG_COUNTER_SET_ZERO(bcsz_counter)      \
    BAG_COUNTER_SET(bcsz_counter, 0)


/*
 *  BAG_COUNTER_IS_ZERO(counter);
 *
 *    Return TRUE if 'counter' is the NULL counter.
 */
#define BAG_COUNTER_IS_ZERO(cin_counter)        \
    (SKBAG_COUNTER_MIN == (cin_counter))


/*
 *  BAG_MEMCPY_COUNTER(dest, src);
 *
 *    Set the counter pointer 'dest' to the value stored in the
 *    counter pointer 'src'.
 */
#if SKBAG_USE_MEMCPY
#define BAG_MEMCPY_COUNTER(cc_counter_ptr, cc_value_ptr)                \
    memcpy((cc_counter_ptr), (cc_value_ptr), sizeof(uint64_t))
#else
#define BAG_MEMCPY_COUNTER(cc_counter_ptr, cc_value_ptr)                \
    (*(uint64_t*)(cc_counter_ptr) = *(uint64_t*)(cc_value_ptr))
#endif


/*
 *    BagTree
 *
 *    The data structure used to store uint32_t keys has an array of
 *    nodes pointing to arrays of nodes that eventually point to an
 *    array of counters.
 */

/*    The number of initial entries to create in the memory pool.
 *    Nodes:    256 * (1 << 8) * sizeof(void*)    ==> 524,288 bytes
 *    Counters: 256 * (1 << 8) * sizeof(uint64_t) ==> 524,288 bytes
 */
#define BAGTREE_MEMPOOL_SIZE      0x100

/*    The number of bits of the key in use at this 'level' */
#define BAGTREE_GET_LEVEL_BITS(gls_bag, gls_level)      8

/*    The bit-offset into key at this 'level' */
#define BAGTREE_GET_LEVEL_OFFSET(glo_bag, glo_level)    \
    (((glo_bag)->levels - 1 - (glo_level))              \
     * BAGTREE_GET_LEVEL_BITS(glo_bag, glo_level))

/*    The number of nodes/leaves at this 'level' */
#define BAGTREE_GET_LEVEL_BLOCKS(glb_bag, glb_level)    \
    (1u << BAGTREE_GET_LEVEL_BITS(glo_bag, glo_level))

/*    The portion of 'key' in-use at this 'level'; used to index into
 *    arrays of nodes and counters. */
#define BAGTREE_GET_KEY_BITS(gkb_key, gkb_bag, gkb_level)               \
    GET_MASKED_BITS((gkb_key),                                          \
                    BAGTREE_GET_LEVEL_OFFSET((gkb_bag), (gkb_level)),   \
                    BAGTREE_GET_LEVEL_BITS((gkb_bag), (gkb_level)))

/*    nodes in the tree are pointers to arrays of other nodes or to
 *    arrays of counters */
typedef union bagtree_node_un bagtree_node_t;
union bagtree_node_un {
    bagtree_node_t     *child;
    uint64_t           *leaf;
};

/* this is the 'b_tree' element in skBag_st */
typedef struct bagtree_st {
    sk_mempool_t       *nodes;

    /* similar to the 'nodes' member, but for counters */
    sk_mempool_t       *counters;

    /* the root of the tree; this points to either a node block or a
     * counter block */
    bagtree_node_t      root;

    /* the number of levels in the tree */
    uint32_t            levels;
} bagtree_t;


/*
 *    Red Black Tree
 *
 *    For IPv6 entries, the data is stored in a red-black tree (unless
 *    the SKBAG_USE_HASHLIB macro is non-zero).
 *
 *    The data element of each node of the red-black tree is a
 *    bag_keycount128_t object that holds the IPv6 address and the
 *    64bit counter.
 *
 *    The bag_keycount128_t objects are stored in an sk_mempool_t.
 *    This allows us to allocate them in chunks, and it also makes for
 *    faster shut-down since we can deallocate them quickly.
 *
 */

/*    Number of elements to allocate at one time in the mempool.
 *    131,072 * sizeof(bag_keycount128_t) ==> 3,145,728 bytes */
#define BAG_REDBLACK_MEMPOOL_SIZE  0x80000

/*    This is the node that is stored in the redblack tree for IPv6
 *    keys. */
typedef struct bag_keycount128_st {
    uint8_t             key[16];
    uint64_t            counter;
} bag_keycount128_t;

/* this is the 'b_rbt' element in skBag_st */
typedef struct bag_redblack_st {
    /* the red-black tree */
    struct rbtree      *tree;

    /* pool of 'bag_keycount128_t'.  these are the data elements of
     * the nodes in the red-black tree. */
    sk_mempool_t       *datum;
} bag_redblack_t;


/* whether to determine min/max when computing statistics. there is
 * little need to do this in the library, since rwbagcat is the only
 * tool that cares, and it computes that information independently. */
#define  BAG_STATS_FIND_MIN_MAX  0

/* Definition of stats */
typedef struct bagstats_st {
    /* count of internal nodes allocated */
    uint64_t                nodes;
    /* number of bytes allocated to nodes */
    uint64_t                nodes_size;
    /* count of entries inserted in the tree */
    uint64_t                unique_keys;
#if BAG_STATS_FIND_MIN_MAX
    /* minimum (non-zero) counter value */
    uint64_t                min_counter;
    /* maximum counter value */
    uint64_t                max_counter;
    /* minimum key inserted */
    uint64_t                min_key;
    /* maximum key inserted */
    uint64_t                max_key;
    skipaddr_t              min_ipkey;
    skipaddr_t              max_ipkey;
#endif  /* 0 */
} bagstats_t;


/* The SiLK Bag */
struct skBag_st {
    union data_un {
        /* a tree of nodes and counters */
        bagtree_t              *b_tree;

#if SK_ENABLE_IPV6
#if SKBAG_USE_HASHLIB
        /* a hash-table of counters is used for keys larger than 4
         * octets */
        HashTable              *b_hash;
#else  /* SKBAG_USE_HASHLIB */
        /* a struct holding a red-black tree of key/counter pairs and
         * a memory pool for the key/counter pairs */
        bag_redblack_t         *b_rbt;
#endif  /* SKBAG_USE_HASHLIB */
#endif  /* SK_ENABLE_IPV6 */
    }                       d;

    /* number of octets that make up the key */
    uint16_t                key_octets;

    /* type of key and value */
    skBagFieldType_t        key_type;
    skBagFieldType_t        counter_type;

    /* whether autoconversion is allowed */
    uint8_t                 no_autoconvert;
};
/* typedef struct skBag_st skBag_t;  // bagtree.h */


/* Definition of the iterator structure */
struct skBagIterator_st {
    /* pointer to the bag to which this iterator was created */
    const skBag_t      *bag;
    /* when working with a sorted keys, the number of keys and the
     * current position in that list */
    uint32_t            pos;
    uint32_t            num_entries;

    /* number of octets that made up the bag's key when the iterator
     * was created. */
    uint16_t            key_octets;

    unsigned            sorted   :1;

    union iter_body_un {
#if SK_ENABLE_IPV6
#if SKBAG_USE_HASHLIB
        struct iter_body_hash_st {
            /* array of keys that existed when the iterator was Reset */
            uint8_t            *keys;

            HASH_ITER           h_iter;
        }                   i_hash;
#else
        struct iter_body_redblack_st {
            /* read one element ahead so we can delete the current
             * node */
            RBLIST                     *rb_iter;
            const bag_keycount128_t    *next;
        }                   i_rbt;
#endif  /* SKBAG_USE_HASHLIB */
#endif  /* SK_ENABLE_IPV6 */
        struct iter_body_bagtree_st {
            /* start searching for next entry using this key value */
            uint32_t            key;
            /* stop iterating when key is this value */
            uint32_t            max_key;
            unsigned            no_more_entries :1;
        }                   i_tree;
    }                   d;
};
/* typedef struct skBagIterator_st skBagIterator_t;  // bagtree.h */


/*    operations on a bag */
typedef enum bag_operation_en {
    BAG_OP_GET, BAG_OP_SET, BAG_OP_ADD, BAG_OP_SUBTRACT
} bag_operation_t;


/*    contains the size and name for the various SKBAG_FIELD_*
 *    values. */
typedef struct bag_field_info_st {
    size_t      octets;
    const char  *name;
} bag_field_info_t;


/* LOCAL VARIABLES */

#if SK_ENABLE_IPV6
static const uint8_t bag_v4inv6[16] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xFF, 0xFF, 0, 0, 0, 0
};
#endif

/* ensure that SKBAG_MAX_FIELD_BUFLEN is larger than maximum name */
static const bag_field_info_t bag_field_info[] = {
    { 4, "sIPv4"},            /* SKBAG_FIELD_SIPv4 */
    { 4, "dIPv4"},            /* SKBAG_FIELD_DIPv4 */
    { 2, "sPort"},            /* SKBAG_FIELD_SPORT */
    { 2, "dPort"},            /* SKBAG_FIELD_DPORT */
    { 1, "protocol"},         /* SKBAG_FIELD_PROTO */
    { 4, "packets"},          /* SKBAG_FIELD_PACKETS */
    { 4, "bytes"},            /* SKBAG_FIELD_BYTES */
    { 1, "flags"},            /* SKBAG_FIELD_FLAGS */

    { 4, "sTime"},            /* SKBAG_FIELD_STARTTIME */
    { 4, "duration"},         /* SKBAG_FIELD_ELAPSED */
    { 4, "eTime"},            /* SKBAG_FIELD_ENDTIME */
    { 2, "sensor"},           /* SKBAG_FIELD_SID */
    { 2, "input"},            /* SKBAG_FIELD_INPUT */
    { 2, "output"},           /* SKBAG_FIELD_OUTPUT */
    { 4, "nhIPv4"},           /* SKBAG_FIELD_NHIPv4 */
    { 1, "initialFlags"},     /* SKBAG_FIELD_INIT_FLAGS */

    { 1, "sessionFlags"},     /* SKBAG_FIELD_REST_FLAGS */
    { 1, "attributes"},       /* SKBAG_FIELD_TCP_STATE */
    { 2, "application"},      /* SKBAG_FIELD_APPLICATION */
    { 1, "class"},            /* SKBAG_FIELD_FTYPE_CLASS */
    { 1, "type"},             /* SKBAG_FIELD_FTYPE_TYPE */
    { 0, "starttime-msec"},   /* SKBAG_FIELD_STARTTIME_MSEC */
    { 0, "endtime-msec"},     /* SKBAG_FIELD_ENDTIME_MSEC */
    { 0, "elapsed-msec"},     /* SKBAG_FIELD_ELAPSED_MSEC */

    { 2, "icmpTypeCode"},     /* SKBAG_FIELD_ICMP_TYPE_CODE */
    {16, "sIPv6"},            /* SKBAG_FIELD_SIPv6 */
    {16, "dIPv6"},            /* SKBAG_FIELD_DIPv6 */
    {16, "nhIPv6"},           /* SKBAG_FIELD_NHIPv6 */
    { 8, "records"},          /* SKBAG_FIELD_RECORDS */
    { 8, "sum-packets"},      /* SKBAG_FIELD_SUM_PACKETS */
    { 8, "sum-bytes"},        /* SKBAG_FIELD_SUM_BYTES */
    { 8, "sum-duration"},     /* SKBAG_FIELD_SUM_ELAPSED */

    { 4, "any-IPv4"},         /* SKBAG_FIELD_ANY_IPv4 */
    {16, "any-IPv6"},         /* SKBAG_FIELD_ANY_IPv6 */
    { 2, "any-port"},         /* SKBAG_FIELD_ANY_PORT */
    { 2, "any-snmp"},         /* SKBAG_FIELD_ANY_SNMP */
    { 4, "any-time"},         /* SKBAG_FIELD_ANY_TIME */
    { 2, "sip-country"},      /* SKBAG_FIELD_SIP_COUNTRY */
    { 2, "dip-country"},      /* SKBAG_FIELD_DIP_COUNTRY */
    { 2, "any-country"},      /* SKBAG_FIELD_ANY_COUNTRY */

    { 4, "sip-pmap"},         /* SKBAG_FIELD_SIP_PMAP */
    { 4, "dip-pmap"},         /* SKBAG_FIELD_DIP_PMAP */
    { 4, "any-ip-pmap"},      /* SKBAG_FIELD_ANY_IP_PMAP */
    { 4, "sport-pmap"},       /* SKBAG_FIELD_SPORT_PMAP */
    { 4, "dport-pmap"},       /* SKBAG_FIELD_DPORT_PMAP */
    { 4, "any-port-pmap"}     /* SKBAG_FIELD_ANY_PORT_PMAP */
};

static const bag_field_info_t bag_field_info_custom = {
    SKBAG_OCTETS_CUSTOM, "custom" /* SKBAG_FIELD_CUSTOM */
};

/*    number of non-custom SKBAG_FIELD_* values that are defined */
#define BAG_NUM_FIELDS                                                  \
    ((skBagFieldType_t)(sizeof(bag_field_info)/sizeof(bag_field_info[0])))

/*
 *  field_ptr = BAG_GET_FIELD_INFO(field_id);
 *
 *    Set the bag_field_info_t* 'field_ptr' to the structure indexed
 *    by 'field_id'.  If 'field_id' is out of range or not supported;
 *    set 'field_ptr' to NULL.
 */
#define BAG_GET_FIELD_INFO(bgbf_field_id)               \
    (((bgbf_field_id) < BAG_NUM_FIELDS)                 \
     ? ((0 == bag_field_info[(bgbf_field_id)].octets)   \
        ? NULL                                          \
        : &bag_field_info[(bgbf_field_id)])             \
     : ((SKBAG_FIELD_CUSTOM == (bgbf_field_id))         \
        ? &bag_field_info_custom                        \
        : NULL))

static const uint64_t bag_counter_invalid = UINT64_C(1) + SKBAG_COUNTER_MAX;

static const skBagTypedCounter_t bag_counter_zero = {
    SKBAG_COUNTER_U64, { SKBAG_COUNTER_MIN }
};
static const skBagTypedCounter_t bag_counter_incr = {
    SKBAG_COUNTER_U64, { 1 }
};


/* EXPORTED VARIABLES */

const skBagTypedCounter_t *skbag_counter_zero = &bag_counter_zero;
const skBagTypedCounter_t *skbag_counter_incr = &bag_counter_incr;


/* FUNCTION PROTOTYPES */

static int
bagtreeIterNext(
    skBagIterator_t    *iter,
    uint32_t           *key,
    uint64_t           *counter);
#if SK_ENABLE_IPV6
#if SKBAG_USE_HASHLIB
static skBagErr_t
bagOperationHash(
    skBag_t                *bag,
    const uint8_t           ipv6[16],
    const uint64_t          change_value,
    skBagTypedCounter_t    *result_value,
    bag_operation_t         op);
#else  /* SKBAG_USE_HASHLIB */
static skBagErr_t
bagOperationRedblack(
    skBag_t                *bag,
    const uint8_t           ipv6[16],
    const uint64_t          change_value,
    skBagTypedCounter_t    *result_value,
    bag_operation_t         op);
#endif  /* SKBAG_USE_HASHLIB */
#endif  /* SK_ENABLE_IPV6 */


/* FUNCTION DEFINITIONS */


/*
 *  ok = bagCheckTypesAndSizes(key_type,counter_type,&key_octets,&counter_octets);
 *
 *    Verify that 'key_type' and 'counter_type' are known types.  In
 *    addition, verify that 'key_octets' and 'counter_octets' are
 *    valid given the 'key_type' and 'counter_type'.  If the
 *    'key_octets' or 'counter_octets' are SKBAG_OCTETS_FIELD_DEFAULT,
 *    modify them to be the size to use in the bag.
 */
static skBagErr_t
bagCheckTypesAndSizes(
    skBagFieldType_t    key_type,
    skBagFieldType_t    counter_type,
    size_t             *key_octets,
    size_t             *counter_octets)
{
    const bag_field_info_t *bf;
    uint32_t high_bits;

    /* check the key type and octets */
    bf = BAG_GET_FIELD_INFO(key_type);
    if (NULL == bf) {
        return SKBAG_ERR_INPUT;
    }
    if (SKBAG_OCTETS_FIELD_DEFAULT == *key_octets) {
        /* use length based on key_type */
        if (SKBAG_OCTETS_CUSTOM == bf->octets) {
            /* DEFAULT is not a valid size for CUSTOM */
            return SKBAG_ERR_INPUT;
        } else if (8 == bf->octets) {
            /* key size of 8 is not supported, so use 4 instead */
            *key_octets = 4;
        } else {
            *key_octets = bf->octets;
        }
    } else if ((SKBAG_OCTETS_CUSTOM == *key_octets)
               || (SKBAG_OCTETS_NO_CHANGE == *key_octets)
               || (SKBAG_OCTETS_UNKNOWN == *key_octets)
               || (*key_octets == 8)
               || (*key_octets > BAG_KEY_MAX_OCTETS))
    {
        return SKBAG_ERR_INPUT;
    }
    /* ensure it is a power of 2 */
    BITS_IN_WORD32(&high_bits, (uint32_t)*key_octets);
    if (high_bits != 1) {
        return SKBAG_ERR_INPUT;
    }

    /* repeat entire process for the counter */
    bf = BAG_GET_FIELD_INFO(counter_type);
    if (NULL == bf) {
        return SKBAG_ERR_INPUT;
    }
    if (SKBAG_OCTETS_FIELD_DEFAULT == *counter_octets) {
        /* use length based on counter_type */
        if (SKBAG_OCTETS_CUSTOM == bf->octets) {
            /* DEFAULT is not a valid size for CUSTOM */
            return SKBAG_ERR_INPUT;
        }
        /* always use size of 8 */
        *counter_octets = sizeof(uint64_t);
#if 0
        /* #if 0 out since counter_octets must be 8 */
    } else if ((SKBAG_OCTETS_CUSTOM == *counter_octets)
               || (SKBAG_OCTETS_NO_CHANGE == *counter_octets)
               || (SKBAG_OCTETS_UNKNOWN == *counter_octets)
               || (*counter_octets > BAG_COUNTER_MAX_OCTETS))
    {
        return SKBAG_ERR_INPUT;
#endif  /* 0 */
    } else if (sizeof(uint64_t) != *counter_octets) {
        return SKBAG_ERR_INPUT;
    }

    return SKBAG_OK;
}


/*
 *  cmp = bagCompareKeys8(a, b)
 *  cmp = bagCompareKeys16(a, b)
 *  cmp = bagCompareKeys32(a, b)
 *  cmp = bagCompareKeys64(a, b)
 *  cmp = bagCompareKeys128(a, b)
 *
 *    Compare the values at 'a' and 'b', where 'a' and 'b' are
 *    pointers to values having the number of bits specified in the
 *    function name.
 */
#if 0
static int
bagCompareKeys8(
    const void         *v_key_a,
    const void         *v_key_b)
{
    if (*(uint8_t*)v_key_a < *(uint8_t*)v_key_b) {
        return -1;
    }
    return (*(uint8_t*)v_key_a > *(uint8_t*)v_key_b);
}

static int
bagCompareKeys16(
    const void         *v_key_a,
    const void         *v_key_b)
{
#if SK_BIG_ENDIAN
    return memcmp(v_key_a, v_key_b, 2);
#elif SKBAG_USE_MEMCPY
    uint16_t key_a;
    uint16_t key_b;

    memcpy(&key_a, v_key_a, sizeof(uint16_t));
    memcpy(&key_b, v_key_b, sizeof(uint16_t));

    if (key_a < key_b) {
        return -1;
    }
    return (key_a > key_b);
#else
    if (*(uint16_t*)v_key_a < *(uint16_t*)v_key_b) {
        return -1;
    }
    return (*(uint16_t*)v_key_a > *(uint16_t*)v_key_b);
#endif
}

static int
bagCompareKeys32(
    const void         *v_key_a,
    const void         *v_key_b)
{
#if SK_BIG_ENDIAN
    return memcmp(v_key_a, v_key_b, 4);
#elif SKBAG_USE_MEMCPY
    uint32_t key_a;
    uint32_t key_b;

    memcpy(&key_a, v_key_a, sizeof(uint32_t));
    memcpy(&key_b, v_key_b, sizeof(uint32_t));

    if (key_a < key_b) {
        return -1;
    }
    return (key_a > key_b);
#else
    if (*(uint32_t*)v_key_a < *(uint32_t*)v_key_b) {
        return -1;
    }
    return (*(uint32_t*)v_key_a > *(uint32_t*)v_key_b);
#endif
}

static int
bagCompareKeys64(
    const void         *v_key_a,
    const void         *v_key_b)
{
#if SK_BIG_ENDIAN
    return memcmp(v_key_a, v_key_b, 8);
#elif SKBAG_USE_MEMCPY
    uint64_t key_a;
    uint64_t key_b;

    memcpy(&key_a, v_key_a, sizeof(uint64_t));
    memcpy(&key_b, v_key_b, sizeof(uint64_t));

    if (key_a < key_b) {
        return -1;
    }
    return (key_a > key_b);
#else
    if (*(uint64_t*)v_key_a < *(uint64_t*)v_key_b) {
        return -1;
    }
    return (*(uint64_t*)v_key_a > *(uint64_t*)v_key_b);
#endif
}
#endif  /* 0 */

#if SK_ENABLE_IPV6
#if SKBAG_USE_HASHLIB
static int
bagCompareKeys128(
    const void         *v_key_a,
    const void         *v_key_b)
{
    return memcmp(v_key_a, v_key_b, sizeof(bag_v4inv6));
}
#else  /* SKBAG_USE_HASHLIB */
static int
bagCompareKeys128(
    const void         *v_key_a,
    const void         *v_key_b,
    const void  UNUSED(*config))
{
    return memcmp(v_key_a, v_key_b, sizeof(bag_v4inv6));
}
#endif  /* SKBAG_USE_HASHLIB */
#endif  /* SK_ENABLE_IPV6 */


/*
 *  bagComputeStatsHash(bag, stats);
 *  bagComputeStatsRedblack(bag, stats);
 *  bagComputeStatsTree(bag, stats);
 *  bagComputeStats(bag, stats);
 *
 *    Given the bag 'bag', update the 'stats' structure with various
 *    statistics about the bag.
 */
#if SK_ENABLE_IPV6
#if SKBAG_USE_HASHLIB
static void
bagComputeStatsHash(
    const skBag_t      *bag,
    bagstats_t         *stats)
{
#if BAG_STATS_FIND_MIN_MAX
    skipaddr_t key;
#endif
    HASH_ITER h_iter;
    uint8_t *key_ptr;
    uint8_t *val_ptr;
    uint64_t counter;

    stats->nodes = hashlib_count_buckets(bag->d.b_hash);
    stats->nodes_size = (stats->nodes
                         * (bag->key_octets + sizeof(uint64_t)));

    h_iter = hashlib_create_iterator(bag->d.b_hash);

    while (hashlib_iterate(bag->d.b_hash, &h_iter, &key_ptr, &val_ptr)
           == OK)
    {
        BAG_MEMCPY_COUNTER(&counter, val_ptr);
        if (!BAG_COUNTER_IS_ZERO(counter)) {
            ++stats->unique_keys;
#if BAG_STATS_FIND_MIN_MAX
            skipaddrSetV6(&key, key_ptr);
            if (skipaddrCompare(&key, &stats->min_ipkey) < 0) {
                skipaddrCopy(&stats->min_ipkey, &key);
            }
            if (skipaddrCopy(&key, &stats->max_ipkey) > 0) {
                skipaddrCopy(&stats->max_ipkey, &key);
            }
            if (counter < stats->min_counter) {
                stats->min_counter = counter;
            }
            if (counter > stats->max_counter) {
                stats->max_counter = counter;
            }
#endif  /* BAG_STATS_FIND_MIN_MAX */
        }
    }
}
#else  /* SKBAG_USE_HASHLIB */

static void
bagComputeStatsRedblack(
    const skBag_t      *bag,
    bagstats_t         *stats)
{
    RBLIST *rb_iter;
    bag_keycount128_t *node;

    rb_iter = rbopenlist(bag->d.b_rbt->tree);
    if (NULL == rb_iter) {
        return;
    }
    while ((node = (bag_keycount128_t*)rbreadlist(rb_iter)) != NULL) {
        ++stats->unique_keys;
#if BAG_STATS_FIND_MIN_MAX
        skipaddrSetV6(&key, node->key);
        if (skipaddrCompare(&key, &stats->min_ipkey) < 0) {
            skipaddrCopy(&stats->min_ipkey, &key);
        }
        if (skipaddrCopy(&key, &stats->max_ipkey) > 0) {
            skipaddrCopy(&stats->max_ipkey, &key);
        }
        if (node->counter < stats->min_counter) {
            stats->min_counter = counter;
        }
        if (node->counter > stats->max_counter) {
            stats->max_counter = counter;
        }
#endif  /* BAG_STATS_FIND_MIN_MAX */
    }
    rbcloselist(rb_iter);

    stats->nodes = stats->unique_keys;
    stats->nodes_size = (stats->nodes * sizeof(bag_keycount128_t));
}
#endif  /* SKBAG_USE_HASHLIB */
#endif  /* SK_ENABLE_IPV6 */

static void
bagComputeStatsTree(
    const skBag_t      *bag,
    bagstats_t         *stats)
{
    skBagIterator_t *iter;
    uint32_t key;
    uint64_t counter;

    if (skBagIteratorCreate(bag, &iter)) {
        return;
    }
#if BAG_STATS_FIND_MIN_MAX
    /* get the first key */
    if (!bagtreeIterNext(iter, &key, &counter)) {
        /* bag is empty */
        return;
    }
    ++stats->unique_keys;
    stats->min_key = stats->max_key = key;
    stats->min_counter = stats->max_counter = counter;
#endif  /* BAG_STATS_FIND_MIN_MAX */

    while (bagtreeIterNext(iter, &key, &counter)) {
        ++stats->unique_keys;
#if BAG_STATS_FIND_MIN_MAX
        stats->max_key = key;
        if (counter < stats->min_counter) {
            stats->min_counter = counter;
        } else if (counter > stats->max_counter) {
            stats->max_counter = counter;
        }
#endif
    }
    skBagIteratorDestroy(iter);

#if BAG_STATS_FIND_MIN_MAX
    skipaddrSetV4(&stats->min_ipkey, &stats->min_key);
    skipaddrSetV4(&stats->max_ipkey, &stats->max_key);
#endif
}

#if 0
static void
bagComputeStats64(
    const skBag_t      *bag,
    bagstats_t         *stats)
{
#if BAG_STATS_FIND_MIN_MAX
    uint64_t key;
#endif
    HASH_ITER h_iter;
    uint8_t *key_ptr;
    uint8_t *val_ptr;
    uint64_t counter;

    stats->nodes = hashlib_count_buckets(bag->d.b_hash);
    stats->nodes_size = (stats->nodes
                         * (bag->key_octets + sizeof(uint64_t)));

    h_iter = hashlib_create_iterator(bag->d.b_hash);
    while (hashlib_iterate(bag->d.b_hash, &h_iter, &key_ptr, &val_ptr)
           == OK)
    {
        BAG_MEMCPY_COUNTER(&counter, val_ptr);
        if (!BAG_COUNTER_IS_ZERO(counter)) {
            ++stats->unique_keys;
#if BAG_STATS_FIND_MIN_MAX
#if SKBAG_USE_MEMCPY
            mempcy(&key, key_ptr, sizeof(key));
#else
            key = *(uint64_t*)key_ptr;
#endif
            if (key < stats->min_key) {
                stats->min_key = key;
            }
            if (key > stats->max_key) {
                stats->max_key = key;
            }
            if (counter < stats->min_counter) {
                stats->min_counter = counter;
            }
            if (counter > stats->max_counter) {
                stats->max_counter = counter;
            }
#endif  /* BAG_STATS_FIND_MIN_MAX */
        }
    }
}
#endif  /* 0 */

static void
bagComputeStats(
    const skBag_t      *bag,
    bagstats_t         *stats)
{
    assert(bag);
    assert(stats);

    memset(stats, 0, sizeof(bagstats_t));
#if BAG_STATS_FIND_MIN_MAX
    stats->min_key = SKBAG_KEY_MAX;
    stats->min_counter = SKBAG_COUNTER_MAX;
#endif

    switch (bag->key_octets) {
      case 1:
      case 2:
      case 4:
        bagComputeStatsTree(bag, stats);
        break;
#if SK_ENABLE_IPV6
      case 16:
#if SKBAG_USE_HASHLIB
        bagComputeStatsHash(bag, stats);
#else
        bagComputeStatsRedblack(bag, stats);
#endif  /* SKBAG_USE_HASHLIB */
        break;
#endif  /* SK_ENABLE_IPV6 */
      case 8:
      default:
        skAbortBadCase(bag->key_octets);
    }
}


/*
 *  err = bagIterCreate(bag, iter, sorted);
 *
 *    Helper function for skBagIteratorCreate() and
 *    skBagIteratorCreateUnsorted().  Create a new iterator for 'bag'.
 */
static skBagErr_t
bagIterCreate(
    const skBag_t      *bag,
    skBagIterator_t   **iter,
    int                 sorted)
{
    skBagErr_t rv;

    /* check inputs */
    if (NULL == bag || NULL == iter) {
        return SKBAG_ERR_INPUT;
    }

    /* allocate iterator */
    *iter = (skBagIterator_t*)calloc(1, sizeof(skBagIterator_t));
    if (NULL == *iter) {
        return SKBAG_ERR_MEMORY;
    }

    (*iter)->bag = bag;
    (*iter)->key_octets = bag->key_octets;
    (*iter)->sorted = (sorted ? 1 : 0);
    rv = skBagIteratorReset(*iter);
    if (SKBAG_OK != rv) {
        skBagIteratorDestroy(*iter);
        *iter = NULL;
    }
    return rv;
}


/*
 *  err = bagIterNextHash(iter, key, counter)
 *  err = bagIterNextRedblack(iter, key, counter)
 *  err = bagIterNextTree(iter, key, counter)
 *
 *    Helper functions for skBagIteratorNext().
 *
 *    Move 'iter' to the next value in the data structure and fill
 *    'key' and 'counter' with that value.  Return SKBAG_OK on
 *    success, or SKBAG_ERR_KEY_NOT_FOUND when the iterator has
 *    visited all entries.
 */
#if SK_ENABLE_IPV6
#if SKBAG_USE_HASHLIB
static skBagErr_t
bagIterNextHash(
    skBagIterator_t        *iter,
    skBagTypedKey_t        *key,
    skBagTypedCounter_t    *counter)
{
    uint8_t *key_ptr;
    uint8_t *val_ptr;
    skBagTypedCounter_t tmp_counter;

    if (0 == iter->sorted) {
        /* data is unsorted */
        while (hashlib_iterate(iter->bag->d.b_hash, &iter->d.i_hash.h_iter,
                               &key_ptr, &val_ptr) == OK)
        {
#if SKBAG_USE_MEMCPY
            uint64_t val;
            BAG_MEMCPY_COUNTER(&val, val_ptr);
            if (BAG_COUNTER_IS_ZERO(val)) {
                continue;
            }
#else
            if (BAG_COUNTER_IS_ZERO(*(uint64_t*)val_ptr)) {
                continue;
            }
#endif  /* SKBAG_USE_MEMCPY */

            /* found an entry to return to user---assuming the key can
             * hold an ipaddr */
            switch (key->type) {
              case SKBAG_KEY_ANY:
                key->type = SKBAG_KEY_IPADDR;
                /* FALLTHROUGH */

              case SKBAG_KEY_IPADDR:
                skipaddrSetV6(&key->val.addr, key_ptr);
#if SKBAG_USE_MEMCPY
                BAG_COUNTER_SET(counter, val);
#else
                BAG_COUNTER_SET(counter, *(uint64_t*)val_ptr);
#endif
                return SKBAG_OK;

              case SKBAG_KEY_U8:
                if (0 == memcmp(key_ptr, bag_v4inv6, 15)) {
                    key->val.u8 = key_ptr[15];
#if SKBAG_USE_MEMCPY
                    BAG_COUNTER_SET(counter, val);
#else
                    BAG_COUNTER_SET(counter, *(uint64_t*)val_ptr);
#endif
                    return SKBAG_OK;
                }
                break;

              case SKBAG_KEY_U16:
                if (0 == memcmp(key_ptr, bag_v4inv6, 14)) {
#if SKBAG_USE_MEMCPY
                    memcpy(&key->val.u16, &key_ptr[14], sizeof(uint16_t));
                    key->val.u16 = ntohs(key->val.u16);
                    BAG_COUNTER_SET(counter, val);
#else
                    key->val.u16 = ntohs(*(uint16_t*)&key_ptr[14]);
                    BAG_COUNTER_SET(counter, *(uint64_t*)val_ptr);
#endif
                    return SKBAG_OK;
                }
                break;

              case SKBAG_KEY_U32:
                if (0 == memcmp(key_ptr, bag_v4inv6, 12)) {
#if SKBAG_USE_MEMCPY
                    memcpy(&key->val.u32, &key_ptr[12], sizeof(uint32_t));
                    key->val.u32 = ntohl(key->val.u32);
                    BAG_COUNTER_SET(counter, val);
#else
                    key->val.u32 = ntohl(*(uint32_t*)&key_ptr[12]);
                    BAG_COUNTER_SET(counter, *(uint64_t*)val_ptr);
#endif
                    return SKBAG_OK;
                }
                break;
            }
        }

    } else {
        /* working with sorted entries */
        while (iter->pos < iter->num_entries) {
            key_ptr = iter->d.i_hash.keys+(iter->pos * iter->bag->key_octets);
            ++iter->pos;

            switch (key->type) {
              case SKBAG_KEY_ANY:
                key->type = SKBAG_KEY_IPADDR;
                /* FALLTHROUGH */

              case SKBAG_KEY_IPADDR:
                bagOperationHash((skBag_t*)iter->bag, key_ptr, 0,
                                 &tmp_counter, BAG_OP_GET);
                if (!BAG_COUNTER_IS_ZERO(tmp_counter.val.u64)) {
                    memcpy(counter, &tmp_counter, sizeof(tmp_counter));
                    skipaddrSetV6(&key->val.addr, key_ptr);
                    return SKBAG_OK;
                }
                break;

              case SKBAG_KEY_U8:
                if (0 != memcmp(key_ptr, bag_v4inv6, 15)) {
                    return SKBAG_ERR_KEY_NOT_FOUND;
                }
                bagOperationHash((skBag_t*)iter->bag, key_ptr, 0,
                                 &tmp_counter, BAG_OP_GET);
                if (!BAG_COUNTER_IS_ZERO(tmp_counter.val.u64)) {
                    memcpy(counter, &tmp_counter, sizeof(tmp_counter));
                    key->val.u8 = key_ptr[15];
                    return SKBAG_OK;
                }
                break;

              case SKBAG_KEY_U16:
                if (0 != memcmp(key_ptr, bag_v4inv6, 14)) {
                    return SKBAG_ERR_KEY_NOT_FOUND;
                }
                bagOperationHash((skBag_t*)iter->bag, key_ptr, 0,
                                 &tmp_counter, BAG_OP_GET);
                if (!BAG_COUNTER_IS_ZERO(tmp_counter.val.u64)) {
                    memcpy(counter, &tmp_counter, sizeof(tmp_counter));
#if SKBAG_USE_MEMCPY
                    memcpy(&key->val.u16, &key_ptr[14], sizeof(uint16_t));
                    key->val.u16 = ntohs(key->val.u16);
#else
                    key->val.u16 = ntohs(*(uint16_t*)&key_ptr[14]);
#endif
                    return SKBAG_OK;
                }
                break;

              case SKBAG_KEY_U32:
                if (0 != memcmp(key_ptr, bag_v4inv6, 12)) {
                    return SKBAG_ERR_KEY_NOT_FOUND;
                }
                bagOperationHash((skBag_t*)iter->bag, key_ptr, 0,
                                 &tmp_counter, BAG_OP_GET);
                if (!BAG_COUNTER_IS_ZERO(tmp_counter.val.u64)) {
                    memcpy(counter, &tmp_counter, sizeof(tmp_counter));
#if SKBAG_USE_MEMCPY
                    memcpy(&key->val.u32, &key_ptr[12], sizeof(uint32_t));
                    key->val.u32 = ntohl(key->val.u32);
#else
                    key->val.u32 = ntohl(*(uint32_t*)&key_ptr[12]);
#endif
                    return SKBAG_OK;
                }
                break;
            }
        }
    }

    return SKBAG_ERR_KEY_NOT_FOUND;
}
#else  /* SKBAG_USE_HASHLIB */

static skBagErr_t
bagIterNextRedblack(
    skBagIterator_t        *iter,
    skBagTypedKey_t        *key,
    skBagTypedCounter_t    *counter)
{
    const bag_keycount128_t *node;

    node = iter->d.i_rbt.next;
    if (NULL == node) {
        return SKBAG_ERR_KEY_NOT_FOUND;
    }
    iter->d.i_rbt.next
        = (const bag_keycount128_t*)rbreadlist(iter->d.i_rbt.rb_iter);

    /* found an entry to return to user---assuming the key can hold an
     * ipaddr */
    switch (key->type) {
      case SKBAG_KEY_ANY:
        key->type = SKBAG_KEY_IPADDR;
        /* FALLTHROUGH */

      case SKBAG_KEY_IPADDR:
        skipaddrSetV6(&key->val.addr, node->key);
        BAG_COUNTER_SET(counter, node->counter);
        return SKBAG_OK;

      case SKBAG_KEY_U8:
        if (0 == memcmp(node->key, bag_v4inv6, 15)) {
            key->val.u8 = node->key[15];
            BAG_COUNTER_SET(counter, node->counter);
            return SKBAG_OK;
        }
        break;

      case SKBAG_KEY_U16:
        if (0 == memcmp(node->key, bag_v4inv6, 14)) {
#if SKBAG_USE_MEMCPY
            memcpy(&key->val.u16, &node->key[14], sizeof(uint16_t));
            key->val.u16 = ntohs(key->val.u16);
#else
            key->val.u16 = ntohs(*(uint16_t*)&node->key[14]);
#endif
            BAG_COUNTER_SET(counter, node->counter);
            return SKBAG_OK;
        }
        break;

      case SKBAG_KEY_U32:
        if (0 == memcmp(node->key, bag_v4inv6, 12)) {
#if SKBAG_USE_MEMCPY
            memcpy(&key->val.u32, &node->key[12], sizeof(uint32_t));
            key->val.u32 = ntohl(key->val.u32);
#else
            key->val.u32 = ntohl(*(uint32_t*)&node->key[12]);
#endif
            BAG_COUNTER_SET(counter, node->counter);
            return SKBAG_OK;
        }
        break;
    }

    return SKBAG_ERR_KEY_NOT_FOUND;
}
#endif  /* SKBAG_USE_HASHLIB */
#endif  /* SK_ENABLE_IPV6 */

static skBagErr_t
bagIterNextTree(
    skBagIterator_t        *iter,
    skBagTypedKey_t        *key,
    skBagTypedCounter_t    *counter)
{
    uint32_t int_key;
    uint64_t int_counter;

    if (!bagtreeIterNext(iter, &int_key, &int_counter)) {
        return SKBAG_ERR_KEY_NOT_FOUND;
    }

    BAG_COUNTER_SET(counter, int_counter);

    switch (key->type) {
      case SKBAG_KEY_U8:
        if (int_key > UINT8_MAX) {
            iter->d.i_tree.no_more_entries = 1;
            return SKBAG_ERR_KEY_NOT_FOUND;
        }
        key->val.u8 = (uint8_t)int_key;
        break;

      case SKBAG_KEY_U16:
        if (int_key > UINT16_MAX) {
            iter->d.i_tree.no_more_entries = 1;
            return SKBAG_ERR_KEY_NOT_FOUND;
        }
        key->val.u16 = (uint16_t)int_key;
        break;

      case SKBAG_KEY_ANY:
        key->type = SKBAG_KEY_U32;
        /* FALLTHROUGH */

      case SKBAG_KEY_U32:
        key->val.u32 = int_key;
        break;

      case SKBAG_KEY_IPADDR:
        skipaddrSetV4(&key->val.addr, &int_key);
        break;
    }

    return SKBAG_OK;
}


/*
 *  err = bagIterResetHash(iter)
 *  err = bagIterResetRedblack(iter)
 *  err = bagIterResetTree(iter)
 *
 *    Reset the iterator depending on what type of data structure the
 *    bag contains.
 */
#if SK_ENABLE_IPV6
#if SKBAG_USE_HASHLIB
static skBagErr_t
bagIterResetHash(
    skBagIterator_t    *iter)
{
    HASH_ITER h_iter;
    uint8_t *key_ptr;
    uint8_t *val_ptr;
    uint8_t *key;
    uint32_t num_entries;
    int (*compare_fn)(const void *, const void*);

    if (0 == iter->sorted) {
        /* unsorted iterator */
        iter->d.i_hash.h_iter = hashlib_create_iterator(iter->bag->d.b_hash);
        return SKBAG_OK;
    }

    switch (iter->bag->key_octets) {
      case 16:
        compare_fn = bagCompareKeys128;
        break;
      case 8:
      default:
        skAbortBadCase(iter->bag->key_octets);
    }

    num_entries = hashlib_count_entries(iter->bag->d.b_hash);
    if (0 == num_entries) {
        iter->num_entries = 0;
        return SKBAG_OK;
    }
    if (iter->d.i_hash.keys) {
        /* reset an existing iterator */
        if (num_entries > iter->num_entries) {
            /* need to realloc the array */
            key = (uint8_t*)realloc(iter->d.i_hash.keys,
                                    (num_entries * iter->bag->key_octets));
            if (NULL == key) {
                return SKBAG_ERR_MEMORY;
            }
            iter->d.i_hash.keys = key;
        }
    } else {
        /* Allocate space for keys */
        iter->d.i_hash.keys
            = (uint8_t*)malloc(num_entries * iter->bag->key_octets);
        if (NULL == iter->d.i_hash.keys) {
            return SKBAG_ERR_MEMORY;
        }
    }
    iter->num_entries = num_entries;

    /* copy keys into an array */
    key = iter->d.i_hash.keys;
    h_iter = hashlib_create_iterator(iter->bag->d.b_hash);
    while (OK == hashlib_iterate(iter->bag->d.b_hash, &h_iter,
                                 &key_ptr, &val_ptr))
    {
        assert(key < (iter->d.i_hash.keys
                      + (iter->num_entries * iter->bag->key_octets)));
        memcpy(key, key_ptr, iter->bag->key_octets);
        key += iter->bag->key_octets;
    }

    skQSort(iter->d.i_hash.keys, iter->num_entries, iter->bag->key_octets,
            compare_fn);

    return SKBAG_OK;
}
#else  /* SKBAG_USE_HASHLIB */

static skBagErr_t
bagIterResetRedblack(
    skBagIterator_t    *iter)
{
    iter->d.i_rbt.rb_iter = rbopenlist(iter->bag->d.b_rbt->tree);
    if (NULL == iter->d.i_rbt.rb_iter) {
        return SKBAG_ERR_MEMORY;
    }
    iter->d.i_rbt.next
        = (const bag_keycount128_t*)rbreadlist(iter->d.i_rbt.rb_iter);
    return SKBAG_OK;
}
#endif  /* SKBAG_USE_HASHLIB */
#endif  /* SK_ENABLE_IPV6 */

static skBagErr_t
bagIterResetTree(
    skBagIterator_t    *iter)
{
    iter->d.i_tree.key = 0;
    iter->d.i_tree.max_key = (UINT32_MAX
                              >> (CHAR_BIT * (4 - iter->bag->key_octets)));
    iter->d.i_tree.no_more_entries = 0;

    return SKBAG_OK;
}


/*
 *  err = bagOperationHash(bag, key, counter, result, op)
 *  err = bagOperationRedblack(bag, key, counter, result, op)
 *  err = bagOperationTree(bag, key, counter, result, op)
 *
 *    Perform the operation 'op' on the counter at 'key' in 'bag'.
 *
 *    If 'op' is GET, 'result' is set to the current counter.
 *
 *    If 'op' is SET, the counter is set to the value 'counter'.
 *
 *    If 'op' is ADD or SUBTRACT, 'counter' is the value by which to
 *    modify the current counter.  In addition, if 'result' is
 *    specified, it is set to the new value.
 */
#if SK_ENABLE_IPV6
#if SKBAG_USE_HASHLIB
static skBagErr_t
bagOperationHash(
    skBag_t                *bag,
    const uint8_t           ipv6[16],
    const uint64_t          change_value,
    skBagTypedCounter_t    *result_value,
    bag_operation_t         op)
{
    uint8_t *val_ptr;
    int rv;

    if (BAG_OP_ADD != op) {
        /* check whether the value exists */
        if (hashlib_lookup(bag->d.b_hash, ipv6, &val_ptr) == OK) {
            /* found it in the hash */
            switch (op) {
              case BAG_OP_GET:
#if SKBAG_USE_MEMCPY
                result_value->type = SKBAG_COUNTER_U64;
                BAG_MEMCPY_COUNTER(&result_value->val.u64, val_ptr);
#else
                BAG_COUNTER_SET(result_value, *(uint64_t*)val_ptr);
#endif
                return SKBAG_OK;

              case BAG_OP_SET:
#if SKBAG_USE_MEMCPY
                BAG_MEMCPY_COUNTER(val_ptr, &change_value);
#else
                *(uint64_t*)val_ptr = change_value;
#endif
                return SKBAG_OK;

              case BAG_OP_SUBTRACT:
#if SKBAG_USE_MEMCPY
                {
                    uint64_t counter;
                    BAG_MEMCPY_COUNTER(&counter, val_ptr);
                    if (counter < change_value) {
                        /* would underflow; return error */
                        return SKBAG_ERR_OP_BOUNDS;
                    }
                    counter -= change_value;
                    BAG_MEMCPY_COUNTER(val_ptr, &counter);
                    if (result_value) {
                        BAG_COUNTER_SET(result_value, counter);
                    }
                }
#else
                if (*(uint64_t*)val_ptr < change_value) {
                    /* would underflow; return error */
                    return SKBAG_ERR_OP_BOUNDS;
                }
                *(uint64_t*)val_ptr -= change_value;
                if (result_value) {
                    BAG_COUNTER_SET(result_value, *(uint64_t*)val_ptr);
                }
#endif  /* SKBAG_USE_MEMCPY */
                return SKBAG_OK;

              case BAG_OP_ADD:
                skAbortBadCase(op);
            }
        } else {
            /* key was not found in the hash */
            switch (op) {
              case BAG_OP_GET:
                BAG_COUNTER_SET_ZERO(result_value);
                return SKBAG_OK;

              case BAG_OP_SET:
                if (BAG_COUNTER_IS_ZERO(change_value)) {
                    /* do not modify the hash table */
                    return SKBAG_OK;
                }
                /* else go into the add code below */
                break;

              case BAG_OP_SUBTRACT:
                if (!BAG_COUNTER_IS_ZERO(change_value)) {
                    /* would underflow; return error */
                    return SKBAG_ERR_OP_BOUNDS;
                }
                if (result_value) {
                    BAG_COUNTER_SET_ZERO(result_value);
                }
                return SKBAG_OK;

              case BAG_OP_ADD:
                skAbortBadCase(op);
            }
        }
    }

    /* this is either an add operation, or a set operation where the
     * value is not in the hash */

    /* get existing counter or allocate it */
    rv = hashlib_insert(bag->d.b_hash, ipv6, &val_ptr);
    switch (rv) {
      case ERR_OUTOFMEMORY:
      case ERR_NOMOREBLOCKS:
        return SKBAG_ERR_MEMORY;

      case OK:
        /* new value */
        BAG_MEMCPY_COUNTER(val_ptr, &change_value);
        if (result_value) {
            BAG_COUNTER_SET(result_value, change_value);
        }
        break;

      case OK_DUPLICATE:
        assert(BAG_OP_SET != op);
#if SKBAG_USE_MEMCPY
        {
            /* check whether (*counter + change_value > SKBAG_COUNTER_MAX) */
            uint64_t counter;
            BAG_MEMCPY_COUNTER(&counter, val_ptr);
            if (counter > (SKBAG_COUNTER_MAX - change_value)) {
                /* would overflow, return error */
                return SKBAG_ERR_OP_BOUNDS;
            }
            counter += change_value;
            BAG_MEMCPY_COUNTER(val_ptr, &counter);
            if (result_value) {
                BAG_COUNTER_SET(result_value, counter);
            }
        }
#else
        /* check whether (*counter + change_value > SKBAG_COUNTER_MAX) */
        if (*(uint64_t*)val_ptr > (SKBAG_COUNTER_MAX - change_value)) {
            /* would overflow, return error */
            return SKBAG_ERR_OP_BOUNDS;
        }
        *(uint64_t*)val_ptr += change_value;
        if (result_value) {
            BAG_COUNTER_SET(result_value, *(uint64_t*)val_ptr);
        }
#endif  /* SKBAG_USE_MEMCPY */
        break;
    }

    return SKBAG_OK;
}
#else  /* SKBAG_USE_HASHLIB */

static skBagErr_t
bagOperationRedblack(
    skBag_t                *bag,
    const uint8_t           ipv6[16],
    const uint64_t          change_value,
    skBagTypedCounter_t    *result_value,
    bag_operation_t         op)
{
    bag_redblack_t *brb;
    bag_keycount128_t *node = NULL;
    bag_keycount128_t wanted;

    brb = bag->d.b_rbt;

    memcpy(wanted.key, ipv6, sizeof(wanted.key));

    /* check whether the value exists */
    node = (bag_keycount128_t*)rbfind(&wanted, brb->tree);
    if (node) {
        /* found it in the redblack tree */
        switch (op) {
          case BAG_OP_GET:
            BAG_COUNTER_SET(result_value, node->counter);
            break;

          case BAG_OP_SET:
            if (BAG_COUNTER_IS_ZERO(change_value)) {
                rbdelete(node, brb->tree);
                skMemPoolElementFree(brb->datum, node);
            } else {
                node->counter = change_value;
            }
            break;

          case BAG_OP_SUBTRACT:
            if (node->counter < change_value) {
                /* would underflow; return error */
                return SKBAG_ERR_OP_BOUNDS;
            }
            if (node->counter == change_value) {
                rbdelete(node, brb->tree);
                skMemPoolElementFree(brb->datum, node);
                if (result_value) {
                    BAG_COUNTER_SET_ZERO(result_value);
                }
            } else {
                node->counter -= change_value;
                if (result_value) {
                    BAG_COUNTER_SET(result_value, node->counter);
                }
            }
            break;

          case BAG_OP_ADD:
            /* check whether (*counter + change_value > SKBAG_COUNTER_MAX) */
            if (node->counter > (SKBAG_COUNTER_MAX - change_value)) {
                /* would overflow, return error */
                return SKBAG_ERR_OP_BOUNDS;
            }
            node->counter += change_value;
            if (result_value) {
                BAG_COUNTER_SET(result_value, node->counter);
            }
            break;
        }
    } else {
        /* key was not found in the redblack tree */
        switch (op) {
          case BAG_OP_GET:
            BAG_COUNTER_SET_ZERO(result_value);
            break;

          case BAG_OP_ADD:
          case BAG_OP_SET:
            if (BAG_COUNTER_IS_ZERO(change_value)) {
                /* nothing to do */
                if (result_value) {
                    BAG_COUNTER_SET_ZERO(result_value);
                }
                break;
            }
            node = (bag_keycount128_t*)skMemPoolElementNew(brb->datum);
            if (NULL == node) {
                return SKBAG_ERR_MEMORY;
            }
            memcpy(node->key, ipv6, sizeof(node->key));
            node->counter = change_value;
            if (NULL == rbsearch(node, brb->tree)) {
                return SKBAG_ERR_MEMORY;
            }
            if (result_value) {
                BAG_COUNTER_SET(result_value, change_value);
            }
            break;

          case BAG_OP_SUBTRACT:
            if (!BAG_COUNTER_IS_ZERO(change_value)) {
                /* would underflow; return error */
                return SKBAG_ERR_OP_BOUNDS;
            }
            if (result_value) {
                BAG_COUNTER_SET_ZERO(result_value);
            }
            break;
        }
    }

    return SKBAG_OK;
}
#endif  /* SKBAG_USE_HASHLIB */
#endif  /* SK_ENABLE_IPV6 */

static skBagErr_t
bagOperationTree(
    skBag_t                *bag,
    const uint32_t          key,
    const uint64_t          change_value,
    skBagTypedCounter_t    *result_value,
    bag_operation_t         op)
{
    bagtree_t *bt;
    bagtree_node_t *subtree;
    uint32_t lvl;
    uint32_t key_bits;

    bt = bag->d.b_tree;
    subtree = &bt->root;

    if (BAG_OP_GET == op || BAG_OP_SUBTRACT == op
        || BAG_COUNTER_IS_ZERO(change_value))
    {
        /* trace down to the counter, but do not allocate anything */
        for (lvl = 0; lvl < bt->levels - 1; ++lvl) {
            if (!subtree->child) {
                /* not found */
                if (BAG_OP_SUBTRACT == op
                    && !BAG_COUNTER_IS_ZERO(change_value))
                {
                    return SKBAG_ERR_OP_BOUNDS;
                }
                if (result_value) {
                    BAG_COUNTER_SET_ZERO(result_value);
                }
                return SKBAG_OK;
            }
            key_bits = BAGTREE_GET_KEY_BITS(key, bt, lvl);
            subtree = &(subtree->child[key_bits]);
        }
        /* we are currently on the last node level, our child should
         * be a leaf  */
        if (!subtree->leaf) {
            /* not found */
            if (BAG_OP_SUBTRACT == op
                && !BAG_COUNTER_IS_ZERO(change_value))
            {
                return SKBAG_ERR_OP_BOUNDS;
            }
            if (result_value) {
                BAG_COUNTER_SET_ZERO(result_value);
            }
            return SKBAG_OK;
        }
        /* key was in the tree */
        key_bits = BAGTREE_GET_KEY_BITS(key, bt, lvl);
        switch (op) {
          case BAG_OP_SET:
            subtree->leaf[key_bits] = change_value;
            break;
          case BAG_OP_GET:
          case BAG_OP_ADD:
            break;
          case BAG_OP_SUBTRACT:
            if (subtree->leaf[key_bits] < change_value) {
                /* would underflow, return error */
                return SKBAG_ERR_OP_BOUNDS;
            }
            subtree->leaf[key_bits] -= change_value;
            break;
        }
        if (result_value) {
            BAG_COUNTER_SET(result_value, subtree->leaf[key_bits]);
        }
        return SKBAG_OK;
    }

    /* visit the nodes and allocate */
    for (lvl = 0; lvl < bt->levels - 1; ++lvl) {
        if (!subtree->child) {
            subtree->child = (bagtree_node_t*)skMemPoolElementNew(bt->nodes);
            if (NULL == subtree->child) {
                return SKBAG_ERR_MEMORY;
            }
        }
        key_bits = BAGTREE_GET_KEY_BITS(key, bt, lvl);
        subtree = &(subtree->child[key_bits]);
    }
    if (!subtree->leaf) {
        subtree->leaf = (uint64_t*)skMemPoolElementNew(bt->counters);
        if (NULL == subtree->leaf) {
            return SKBAG_ERR_MEMORY;
        }
    }
    key_bits = BAGTREE_GET_KEY_BITS(key, bt, lvl);
    switch (op) {
      case BAG_OP_SET:
        subtree->leaf[key_bits] = change_value;
        break;
      case BAG_OP_ADD:
        if (subtree->leaf[key_bits] > (SKBAG_COUNTER_MAX - change_value)) {
            /* would overflow, return error */
            return SKBAG_ERR_OP_BOUNDS;
        }
        subtree->leaf[key_bits] += change_value;
        if (result_value) {
            BAG_COUNTER_SET(result_value, subtree->leaf[key_bits]);
        }
        break;
      case BAG_OP_GET:
      case BAG_OP_SUBTRACT:
        skAbortBadCase(op);
    }

    return SKBAG_OK;
}


/*
 *  status = bagProcessStreamEntryAdd(fake_bag, key, counter, bag);
 *
 *    The skBagStreamEntryFunc_t callback used by skBagAddFromStream().
 *
 *    Add 'counter' to the existing counter for 'key' in 'bag'.
 */
static skBagErr_t
bagProcessStreamEntryAdd(
    const skBag_t               UNUSED(*fake_bag),
    const skBagTypedKey_t              *key,
    const skBagTypedCounter_t          *counter,
    void                               *v_bag)
{
    return skBagCounterAdd((skBag_t*)v_bag, key, counter, NULL);
}


/*
 *  status = bagProcessStreamEntryRead(fake_bag, key, counter, bag);
 *
 *    The skBagStreamEntryFunc_t callback used by skBagRead().
 *
 *    Callback function used by skBagRead().  Set the 'key' to
 *    'counter' in 'bag'.
 */
static skBagErr_t
bagProcessStreamEntryRead(
    const skBag_t               UNUSED(*fake_bag),
    const skBagTypedKey_t              *key,
    const skBagTypedCounter_t          *counter,
    void                               *v_bag)
{
    return skBagCounterSet(*(skBag_t**)v_bag, key, counter);
}


/*
 *  status = bagProcessStreamInitAdd(fake_bag, v_dest_bag);
 *
 *    The skBagStreamInitFunc_t callback used by skBagAddFromStream().
 *
 *    Modifies the destination bag depending on the keys length/type
 *    in the bag that is being read---the 'fake_bag'.
 */
static skBagErr_t
bagProcessStreamInitAdd(
    const skBag_t      *src,
    void               *v_dest_bag)
{
    skBag_t *dest = (skBag_t*)v_dest_bag;

    if (dest->no_autoconvert && (dest->key_octets < src->key_octets)) {
        return SKBAG_ERR_KEY_RANGE;
    }

    dest->key_type = skBagFieldTypeMerge(dest->key_type, src->key_type);
    dest->counter_type = skBagFieldTypeMerge(dest->counter_type,
                                             src->counter_type);
    return SKBAG_OK;
}


/*
 *  status = bagProcessStreamInitRead(fake_bag, bag);
 *
 *    The skBagStreamInitFunc_t callback used by skBagRead().
 *
 *    Creates a new bag in the location specified by 'bag' based on
 *    the parameters in 'fake_bag'.
 */
static skBagErr_t
bagProcessStreamInitRead(
    const skBag_t      *src,
    void               *v_bag)
{
    return skBagCreateTyped((skBag_t**)v_bag, src->key_type, src->counter_type,
                            src->key_octets, sizeof(uint64_t));
}


/*
 *  found = bagtreeIterNext(iter, &key, &counter);
 *
 *    Fill 'key' and 'counter' with the next entry for the iterator
 *    over the bagtree.  Return 1 if found; 0 when no more entries.
 */
static int
bagtreeIterNext(
    skBagIterator_t    *iter,
    uint32_t           *key,
    uint64_t           *counter)
{
    bagtree_t *bt;
    bagtree_node_t *subtree[BAG_KEY_MAX_OCTETS];
    uint32_t key_bits;
    uint32_t lvl;

    bt = iter->bag->d.b_tree;
    subtree[0] = &bt->root;
    lvl = 0;

    if (iter->d.i_tree.no_more_entries) {
        return 0;
    }
    if ((0 == iter->d.i_tree.key)
        && (NULL == subtree[0]->child))
    {
        /* empty tree */
        iter->d.i_tree.no_more_entries = 1;
        return 0;
    }

    for (;;) {
        key_bits = BAGTREE_GET_KEY_BITS(iter->d.i_tree.key, bt, lvl);
        if (lvl < bt->levels - 1) {
            if (subtree[lvl]->child[key_bits].child) {
                subtree[lvl+1] = &subtree[lvl]->child[key_bits];
                ++lvl;
                continue;
            }
            do {
                ++key_bits;
            } while (key_bits < BAGTREE_GET_LEVEL_BLOCKS(bt, lvl)
                     && NULL == subtree[lvl]->child[key_bits].child);
            if (key_bits < BAGTREE_GET_LEVEL_BLOCKS(bt, lvl)) {
                SET_MASKED_BITS(iter->d.i_tree.key,
                                key_bits << BAGTREE_GET_LEVEL_OFFSET(bt, lvl),
                                0, BAGTREE_GET_LEVEL_OFFSET(bt, lvl-1));
                subtree[lvl+1] = &subtree[lvl]->child[key_bits];
                ++lvl;
                continue;
            }
        } else {
            if (!BAG_COUNTER_IS_ZERO(subtree[lvl]->leaf[key_bits])) {
                *key = iter->d.i_tree.key;
                *counter = subtree[lvl]->leaf[key_bits];
                if (iter->d.i_tree.max_key == iter->d.i_tree.key) {
                    iter->d.i_tree.no_more_entries = 1;
                } else {
                    ++iter->d.i_tree.key;
                }
                return 1;
            }
            do {
                ++key_bits;
            } while (key_bits < BAGTREE_GET_LEVEL_BLOCKS(bt, lvl)
                     && BAG_COUNTER_IS_ZERO(subtree[lvl]->leaf[key_bits]));
            if (key_bits != BAGTREE_GET_LEVEL_BLOCKS(bt, lvl)) {
                SET_MASKED_BITS(iter->d.i_tree.key,
                                key_bits << BAGTREE_GET_LEVEL_OFFSET(bt, lvl),
                                0, BAGTREE_GET_LEVEL_OFFSET(bt, lvl-1));
                *key = iter->d.i_tree.key;
                *counter = subtree[lvl]->leaf[key_bits];
                if (iter->d.i_tree.max_key == iter->d.i_tree.key) {
                    iter->d.i_tree.no_more_entries = 1;
                } else {
                    ++iter->d.i_tree.key;
                }
                return 1;
            }
        }

        do {
            if (0 == lvl) {
                iter->d.i_tree.no_more_entries = 1;
                return 0;
            }
            --lvl;
        } while ((BAGTREE_GET_KEY_BITS(iter->d.i_tree.key, bt, lvl)
                  == BAGTREE_GET_LEVEL_BLOCKS(bt, lvl)-1));
        iter->d.i_tree.key = (((iter->d.i_tree.key
                                >> BAGTREE_GET_LEVEL_OFFSET(bt, lvl)) + 1)
                              << BAGTREE_GET_LEVEL_OFFSET(bt, lvl));
    }

    return 0;                   /* NOTREACHED */
}



/*    ********************************************************    */
/*    EXPORTED/PUBLIC FUNCTIONS START HERE    */
/*    ********************************************************    */


/* add an in-core bag to an in-core bag */
skBagErr_t
skBagAddBag(
    skBag_t                *dest,
    const skBag_t          *src,
    skBagBoundsCallback_t   bounds_cb,
    void                   *cb_data)
{
    skBagIterator_t *iter = NULL;
    skBagTypedKey_t key;
    skBagTypedCounter_t counter;
    skBagTypedCounter_t counter2;
    skBagErr_t rv;
    skBagErr_t rv2;

    if (NULL == dest || NULL == src) {
        return SKBAG_ERR_INPUT;
    }
    if (dest->no_autoconvert && (dest->key_octets < src->key_octets)) {
        return SKBAG_ERR_KEY_RANGE;
    }

    dest->key_type = skBagFieldTypeMerge(dest->key_type, src->key_type);
    dest->counter_type = skBagFieldTypeMerge(dest->counter_type,
                                             src->counter_type);

    /* Set type of key and counter to 'ANY' */
    key.type = SKBAG_KEY_ANY;
    counter.type = SKBAG_COUNTER_ANY;
    rv = skBagIteratorCreateUnsorted(src, &iter);
    if (rv) {
        goto END;
    }
    while (skBagIteratorNextTyped(iter, &key, &counter) == SKBAG_OK) {
        rv = skBagCounterAdd(dest, &key, &counter, NULL);
        if (rv) {
            if (SKBAG_ERR_OP_BOUNDS != rv || NULL == bounds_cb) {
                goto END;
            }
            counter2.type = SKBAG_COUNTER_ANY;
            skBagCounterGet(dest, &key, &counter2);
            rv2 = bounds_cb(&key, &counter2, &counter, cb_data);
            if (rv2) {
                rv = rv2;
                goto END;
            }
            rv2 = skBagCounterSet(dest, &key, &counter2);
            if (rv2) {
                rv = rv2;
                goto END;
            }
        }
    }

  END:
    if (iter) {
        skBagIteratorDestroy(iter);
    }
    return rv;
}


/* add contents of file to existing bag. increment counters for
 * overlapping keys. */
skBagErr_t
skBagAddFromStream(
    skBag_t            *bag,
    skstream_t         *stream_in)
{
    if (NULL == bag) {
        return SKBAG_ERR_INPUT;
    }

    return skBagProcessStreamTyped(stream_in, bag, &bagProcessStreamInitAdd,
                                   &bagProcessStreamEntryAdd);
}


void
skBagAutoConvertDisable(
    skBag_t            *bag)
{
    bag->no_autoconvert = 1;
}

void
skBagAutoConvertEnable(
    skBag_t            *bag)
{
    bag->no_autoconvert = 0;
}


int
skBagAutoConvertIsEnabled(
    const skBag_t      *bag)
{
    return !bag->no_autoconvert;
}


/*
 *  status = skBagCopy(&dest, src);
 *
 *    Make a new bag that is a deep copy of src, and set '*dest' to
 *    it.
 */
skBagErr_t
skBagCopy(
    skBag_t           **dest,
    const skBag_t      *src)
{
    skBag_t *bag;
    skBagErr_t rv;

    if (NULL == dest || NULL == src) {
        return SKBAG_ERR_INPUT;
    }

    rv = skBagCreateTyped(&bag, src->key_type, src->counter_type,
                          src->key_octets, sizeof(uint64_t));
    if (rv) {
        return rv;
    }

    switch (src->key_octets) {
      case 1:
      case 2:
      case 4:
        {
            skBagIterator_t *iter = NULL;
            uint32_t key;
            uint64_t counter;

            rv = skBagIteratorCreate(src, &iter);
            if (rv) {
                goto END;
            }
            while (bagtreeIterNext(iter, &key, &counter)) {
                if (bagOperationTree(bag, key, counter, 0, BAG_OP_SET)) {
                    rv = SKBAG_ERR_MEMORY;
                    skBagIteratorDestroy(iter);
                    goto END;
                }
            }
            skBagIteratorDestroy(iter);
        }
        break;

#if SK_ENABLE_IPV6
      case 16:
#if SKBAG_USE_HASHLIB
        {
            HASH_ITER h_iter;
            uint8_t *key_ptr;
            uint8_t *src_val_ptr;
            uint8_t *dest_val_ptr;
            int h_err;

            h_iter = hashlib_create_iterator(src->d.b_hash);
            while (hashlib_iterate(src->d.b_hash, &h_iter,
                                   &key_ptr, &src_val_ptr) == OK)
            {
                if (BAG_COUNTER_IS_ZERO(src_val_ptr)) {
                    continue;
                }
                h_err = hashlib_insert(bag->d.b_hash, key_ptr, &dest_val_ptr);
                switch (h_err) {
                  case ERR_OUTOFMEMORY:
                  case ERR_NOMOREBLOCKS:
                    rv = SKBAG_ERR_MEMORY;
                    goto END;
                  case OK:
                    BAG_MEMCPY_COUNTER(dest_val_ptr, src_val_ptr);
                    break;
                  case OK_DUPLICATE:
                  default:
                    skAbortBadCase(h_err);
                }
            }
        }
#else  /* SKBAG_USE_HASHLIB */
        {
            RBLIST *rb_iter;
            const bag_keycount128_t *srcnode;
            bag_keycount128_t *dstnode;
            bag_redblack_t *brb = bag->d.b_rbt;

            rb_iter = rbopenlist(src->d.b_rbt->tree);
            if (NULL == rb_iter) {
                rv = SKBAG_ERR_MEMORY;
                goto END;
            }
            while ((srcnode = (const bag_keycount128_t*)rbreadlist(rb_iter))
                   != NULL)
            {
                dstnode = (bag_keycount128_t*)skMemPoolElementNew(brb->datum);
                if (NULL == dstnode) {
                    rbcloselist(rb_iter);
                    rv = SKBAG_ERR_MEMORY;
                    goto END;
                }
                memcpy(dstnode, srcnode, sizeof(bag_keycount128_t));
                if (NULL == rbsearch(dstnode, brb->tree)) {
                    rbcloselist(rb_iter);
                    rv = SKBAG_ERR_MEMORY;
                    goto END;
                }
            }
            rbcloselist(rb_iter);
        }
#endif  /* SKBAG_USE_HASHLIB */
        break;
#endif  /* SK_ENABLE_IPV6 */

      case 8:
      default:
        skAbortBadCase(src->key_octets);
    }

    *dest = bag;
    rv = SKBAG_OK;

  END:
    if (SKBAG_OK != rv) {
        skBagDestroy(&bag);
    }
    return rv;
}


/* return number of unique keys in bag */
uint64_t
skBagCountKeys(
    const skBag_t      *bag)
{
    bagstats_t stats;

    bagComputeStats(bag, &stats);
    return stats.unique_keys;
}


/* add value 'counter_add' to counter for 'key'; create key if needed */
skBagErr_t
skBagCounterAdd(
    skBag_t                    *bag,
    const skBagTypedKey_t      *key,
    const skBagTypedCounter_t  *counter_add,
    skBagTypedCounter_t        *out_counter)
{
    uint32_t u32;
    skBagErr_t rv;

    BAG_CHECK_INPUT(bag, key, counter_add);

#if !SK_ENABLE_IPV6

    BAG_KEY_TO_U32_V4(key, u32);

#else
    {
        uint8_t ipv6[16];
        int is_v6;
        skBagFieldType_t key_type;

        if (16 == bag->key_octets) {
            /* bag is ipv6, so convert key to ipv6 */
            BAG_KEY_TO_IPV6(key, ipv6);
#if SKBAG_USE_HASHLIB
            return bagOperationHash(bag, ipv6, counter_add->val.u64,
                                    out_counter, BAG_OP_ADD);
#else
            return bagOperationRedblack(bag, ipv6, counter_add->val.u64,
                                        out_counter, BAG_OP_ADD);
#endif  /* SKBAG_USE_HASHLIB */
        }

        BAG_KEY_TO_U32_V6(key, u32, is_v6);

        if (is_v6) {
            /* key is IPv6; convert bag unless 'counter_add' is 0 */
            if (BAG_COUNTER_IS_ZERO(counter_add->val.u64)) {
                if (out_counter) {
                    BAG_COUNTER_SET_ZERO(out_counter);
                }
                return SKBAG_OK;
            }
            if (bag->no_autoconvert) {
                return SKBAG_ERR_KEY_RANGE;
            }
            switch (bag->key_type) {
              case SKBAG_FIELD_SIPv4:
                key_type = SKBAG_FIELD_SIPv6;
                break;
              case SKBAG_FIELD_DIPv4:
                key_type = SKBAG_FIELD_DIPv6;
                break;
              case SKBAG_FIELD_NHIPv4:
                key_type = SKBAG_FIELD_NHIPv6;
                break;
              case SKBAG_FIELD_ANY_IPv4:
                key_type = SKBAG_FIELD_ANY_IPv6;
                break;
              default:
                key_type = bag->key_type;
                break;
            }
            rv = skBagModify(bag, key_type, bag->counter_type,
                             sizeof(ipv6), sizeof(uint64_t));
            if (rv) {
                return rv;
            }
            BAG_KEY_TO_IPV6(key, ipv6);
#if SKBAG_USE_HASHLIB
            return bagOperationHash(bag, ipv6, counter_add->val.u64,
                                    out_counter, BAG_OP_ADD);
#else
            return bagOperationRedblack(bag, ipv6, counter_add->val.u64,
                                        out_counter, BAG_OP_ADD);
#endif  /* SKBAG_USE_HASHLIB */
        }
    }
#endif  /* #else of #if !SK_ENABLE_IPV6 */

    if ((bag->key_octets < 4)
        && (u32 >= (1u << (bag->key_octets * CHAR_BIT))))
    {
        /* key is out of range */
        if (BAG_COUNTER_IS_ZERO(counter_add->val.u64)) {
            if (out_counter) {
                BAG_COUNTER_SET_ZERO(out_counter);
            }
            return SKBAG_OK;
        }
        if (bag->no_autoconvert) {
            return SKBAG_ERR_KEY_RANGE;
        }
        rv = skBagModify(bag, bag->key_type, bag->counter_type,
                         sizeof(uint32_t), sizeof(uint64_t));
        if (rv) {
            return rv;
        }
    }

    return bagOperationTree(bag, u32, counter_add->val.u64,
                            out_counter, BAG_OP_ADD);
}


size_t
skBagCounterFieldLength(
    const skBag_t   UNUSED(*bag))
{
    return sizeof(uint64_t);
}


skBagFieldType_t
skBagCounterFieldName(
    const skBag_t      *bag,
    char               *buf,
    size_t              buflen)
{
    const bag_field_info_t *bf;

    bf = BAG_GET_FIELD_INFO(bag->counter_type);
    if (NULL == bf) {
        bf = &bag_field_info_custom;
    }
    if (buf && buflen) {
        strncpy(buf, bf->name, buflen);
        buf[buflen-1] = '\0';
    }

    return bag->counter_type;
}


skBagFieldType_t
skBagCounterFieldType(
    const skBag_t      *bag)
{
    return bag->counter_type;
}


/* get counter at 'key' */
skBagErr_t
skBagCounterGet(
    const skBag_t          *bag,
    const skBagTypedKey_t  *key,
    skBagTypedCounter_t    *out_counter)
{
    uint32_t u32;

    if (NULL == bag || NULL == key || NULL == out_counter) {
        return SKBAG_ERR_INPUT;
    }

#if !SK_ENABLE_IPV6

    BAG_KEY_TO_U32_V4(key, u32);

#else
    {
        uint8_t ipv6[16];
        int is_v6;

        if (16 == bag->key_octets) {
            /* bag is ipv6, so convert key to ipv6 */
            BAG_KEY_TO_IPV6(key, ipv6);
#if SKBAG_USE_HASHLIB
            return bagOperationHash((skBag_t*)bag, ipv6, 0,
                                    out_counter, BAG_OP_GET);
#else
            return bagOperationRedblack((skBag_t*)bag, ipv6, 0,
                                        out_counter, BAG_OP_GET);
#endif  /* SKBAG_USE_HASHLIB */
        }

        BAG_KEY_TO_U32_V6(key, u32, is_v6);

        if (is_v6) {
            /* key is IPv6; so it is not in this bag */
            BAG_COUNTER_SET_ZERO(out_counter);
            return SKBAG_OK;
        }
    }
#endif  /* #else of #if !SK_ENABLE_IPV6 */

    if ((bag->key_octets < 4)
        && (u32 >= (1u << (bag->key_octets * CHAR_BIT))))
    {
        /* key is out of range */
        BAG_COUNTER_SET_ZERO(out_counter);
        return SKBAG_OK;
    }

    return bagOperationTree((skBag_t*)bag, u32, 0, out_counter, BAG_OP_GET);
}


/* set counter for 'key' to 'counter'.  create key if needed */
skBagErr_t
skBagCounterSet(
    skBag_t                    *bag,
    const skBagTypedKey_t      *key,
    const skBagTypedCounter_t  *counter)
{
    uint32_t u32;
    skBagErr_t rv = SKBAG_ERR_INPUT;

    BAG_CHECK_INPUT(bag, key, counter);

#if !SK_ENABLE_IPV6

    BAG_KEY_TO_U32_V4(key, u32);

#else
    {
        uint8_t ipv6[16];
        int is_v6;
        skBagFieldType_t key_type;

        if (16 == bag->key_octets) {
            /* bag is ipv6, so convert key to ipv6 */
            BAG_KEY_TO_IPV6(key, ipv6);
#if SKBAG_USE_HASHLIB
            return bagOperationHash(bag, ipv6, counter->val.u64,
                                    NULL, BAG_OP_SET);
#else
            return bagOperationRedblack(bag, ipv6, counter->val.u64,
                                        NULL, BAG_OP_SET);
#endif  /* SKBAG_USE_HASHLIB */
        }

        BAG_KEY_TO_U32_V6(key, u32, is_v6);

        if (is_v6) {
            /* key is IPv6; convert bag unless 'counter' is 0 */
            if (BAG_COUNTER_IS_ZERO(counter->val.u64)) {
                return SKBAG_OK;
            }
            if (bag->no_autoconvert) {
                return SKBAG_ERR_KEY_RANGE;
            }
            switch (bag->key_type) {
              case SKBAG_FIELD_SIPv4:
                key_type = SKBAG_FIELD_SIPv6;
                break;
              case SKBAG_FIELD_DIPv4:
                key_type = SKBAG_FIELD_DIPv6;
                break;
              case SKBAG_FIELD_NHIPv4:
                key_type = SKBAG_FIELD_NHIPv6;
                break;
              case SKBAG_FIELD_ANY_IPv4:
                key_type = SKBAG_FIELD_ANY_IPv6;
                break;
              default:
                key_type = bag->key_type;
                break;
            }
            rv = skBagModify(bag, key_type, bag->counter_type,
                             sizeof(ipv6), sizeof(uint64_t));
            if (rv) {
                return rv;
            }
            BAG_KEY_TO_IPV6(key, ipv6);
#if SKBAG_USE_HASHLIB
            return bagOperationHash(bag, ipv6, counter->val.u64,
                                    NULL, BAG_OP_SET);
#else
            return bagOperationRedblack(bag, ipv6, counter->val.u64,
                                        NULL, BAG_OP_SET);
#endif  /* SKBAG_USE_HASHLIB */
        }
    }
#endif  /* #else of #if !SK_ENABLE_IPV6 */

    if ((bag->key_octets < 4)
        && (u32 >= (1u << (bag->key_octets * CHAR_BIT))))
    {
        /* key is out of range */
        if (BAG_COUNTER_IS_ZERO(counter->val.u64)) {
            return SKBAG_OK;
        }
        if (bag->no_autoconvert) {
            return SKBAG_ERR_KEY_RANGE;
        }

        rv = skBagModify(bag, bag->key_type, bag->counter_type,
                         sizeof(uint32_t), sizeof(uint64_t));
        if (rv) {
            return rv;
        }
    }

    return bagOperationTree(bag, u32, counter->val.u64, NULL, BAG_OP_SET);
}


/* subtract 'counter_sub' from counter at 'key' */
skBagErr_t
skBagCounterSubtract(
    skBag_t                    *bag,
    const skBagTypedKey_t      *key,
    const skBagTypedCounter_t  *counter_sub,
    skBagTypedCounter_t        *out_counter)
{
    uint32_t u32;

    BAG_CHECK_INPUT(bag, key, counter_sub);

#if !SK_ENABLE_IPV6

    BAG_KEY_TO_U32_V4(key, u32);

#else
    {
        uint8_t ipv6[16];
        int is_v6;

        if (16 == bag->key_octets) {
            /* bag is ipv6, so convert key to ipv6 */
            BAG_KEY_TO_IPV6(key, ipv6);
#if SKBAG_USE_HASHLIB
            return bagOperationHash(bag, ipv6, counter_sub->val.u64,
                                    out_counter, BAG_OP_SUBTRACT);
#else
            return bagOperationRedblack(bag, ipv6, counter_sub->val.u64,
                                        out_counter, BAG_OP_SUBTRACT);
#endif  /* SKBAG_USE_HASHLIB */
        }

        BAG_KEY_TO_U32_V6(key, u32, is_v6);

        if (is_v6) {
            /* key is IPv6, so it is not in this bag.  subtraction would
             * underflow unless 'counter_sub' is 0 */
            if (BAG_COUNTER_IS_ZERO(counter_sub->val.u64)) {
                if (out_counter) {
                    BAG_COUNTER_SET_ZERO(out_counter);
                }
                return SKBAG_OK;
            }
            return SKBAG_ERR_OP_BOUNDS;
        }
    }
#endif  /* #else of #if !SK_ENABLE_IPV6 */

    if ((bag->key_octets < 4)
        && (u32 >= (1u << (bag->key_octets * CHAR_BIT))))
    {
        /* key is out of range */
        if (!BAG_COUNTER_IS_ZERO(counter_sub->val.u64)) {
            return SKBAG_ERR_OP_BOUNDS;
        }
        if (out_counter) {
            BAG_COUNTER_SET_ZERO(out_counter);
        }
        return SKBAG_OK;
    }

    return bagOperationTree(bag, u32, counter_sub->val.u64,
                            out_counter, BAG_OP_SUBTRACT);
}


skBagErr_t
skBagCreate(
    skBag_t           **bag)
{
    return skBagCreateTyped(bag, SKBAG_FIELD_CUSTOM, SKBAG_FIELD_CUSTOM,
                            sizeof(uint32_t), sizeof(uint64_t));
}


skBagErr_t
skBagCreateTyped(
    skBag_t           **bag,
    skBagFieldType_t    key_type,
    skBagFieldType_t    counter_type,
    size_t              key_octets,
    size_t              counter_octets)
{
    skBag_t *new_bag = NULL;
    skBagErr_t rv;

    rv = bagCheckTypesAndSizes(key_type, counter_type,
                               &key_octets, &counter_octets);
    if (rv) {
        return rv;
    }

    /* allocate the bag */
    new_bag = (skBag_t*)calloc(1, sizeof(skBag_t));
    if (NULL == new_bag) {
        goto ERROR;
    }

    new_bag->key_octets = (uint16_t)key_octets;
    new_bag->key_type = key_type;
    new_bag->counter_type = counter_type;

    switch (new_bag->key_octets) {
      case 1:
      case 2:
      case 4:
        {
            bagtree_t *bt;
            bt = (bagtree_t*)calloc(1, sizeof(bagtree_t));
            if (NULL == bt) {
                goto ERROR;
            }
            bt->levels = new_bag->key_octets;
            if (skMemoryPoolCreate(&bt->nodes,
                                   (BAGTREE_GET_LEVEL_BLOCKS(bt, 0)
                                    * sizeof(bagtree_node_t)),
                                   BAGTREE_MEMPOOL_SIZE))
            {
                free(bt);
                goto ERROR;
            }
            if (skMemoryPoolCreate(&bt->counters,
                                   (BAGTREE_GET_LEVEL_BLOCKS(bt, bt->levels-1)
                                    * sizeof(uint64_t)),
                                   BAGTREE_MEMPOOL_SIZE))
            {
                skMemoryPoolDestroy(&bt->nodes);
                free(bt);
                goto ERROR;
            }
            new_bag->d.b_tree = bt;
        }
        break;

#if SK_ENABLE_IPV6
      case 16:
#if SKBAG_USE_HASHLIB
        new_bag->d.b_hash
            = hashlib_create_table(new_bag->key_octets,
                                   sizeof(uint64_t),
                                   HTT_INPLACE,
                                   (uint8_t*)&bag_counter_invalid,
                                   NULL,
                                   0,
                                   HASH_INITIAL_SIZE,
                                   DEFAULT_LOAD_FACTOR);
        if (NULL == new_bag->d.b_hash) {
            goto ERROR;
        }
#else  /* SKBAG_USE_HASHLIB */
        {
            bag_redblack_t *brb;
            brb = (bag_redblack_t*)calloc(1, sizeof(bag_redblack_t));
            if (NULL == brb) {
                goto ERROR;
            }
            if (skMemoryPoolCreate(&brb->datum,
                                   sizeof(bag_keycount128_t),
                                   BAG_REDBLACK_MEMPOOL_SIZE))
            {
                free(brb);
                goto ERROR;
            }
            brb->tree = rbinit(&bagCompareKeys128, NULL);
            if (NULL == brb->tree) {
                skMemoryPoolDestroy(&brb->datum);
                free(brb);
                goto ERROR;
            }
            new_bag->d.b_rbt = brb;
        }
#endif  /* SKBAG_USE_HASHLIB */
        break;
#endif  /* SK_ENABLE_IPV6 */

      case 8:
      default:
        skAbortBadCase(new_bag->key_octets);
    }

    /* set pointer and return OK */
    *bag = new_bag;
    return SKBAG_OK;

  ERROR:
    if (new_bag) {
        free(new_bag);
    }
    return SKBAG_ERR_MEMORY;
}


void
skBagDestroy(
    skBag_t           **bag_ptr)
{
    if (bag_ptr && *bag_ptr) {
        skBag_t *bag = *bag_ptr;

        switch (bag->key_octets) {
          case 1:
          case 2:
          case 4:
            if (bag->d.b_tree) {
                bagtree_t *bt = bag->d.b_tree;
                if (bt->nodes) {
                    skMemoryPoolDestroy(&bt->nodes);
                }
                if (bt->counters) {
                    skMemoryPoolDestroy(&bt->counters);
                }
                free(bt);
            }
            break;
#if SK_ENABLE_IPV6
          case 16:
#if SKBAG_USE_HASHLIB
            if (bag->d.b_hash) {
                hashlib_free_table(bag->d.b_hash);
            }
#else
            if (bag->d.b_rbt) {
                bag_redblack_t *brb = bag->d.b_rbt;
                if (brb->datum) {
                    skMemoryPoolDestroy(&brb->datum);
                }
                if (brb->tree) {
                    rbdestroy(brb->tree);
                }
                free(brb);
            }
#endif  /* SKBAG_USE_HASHLIB */
            break;
#endif  /* SK_ENABLE_IPV6 */
          case 8:
          default:
            skAbortBadCase(bag->key_octets);
        }
        memset(bag, 0, sizeof(skBag_t));
        free(bag);
        *bag_ptr = NULL;
    }
}


char *
skBagFieldTypeAsString(
    skBagFieldType_t    field,
    char               *buf,
    size_t              buflen)
{
    const bag_field_info_t *bf;

    bf = BAG_GET_FIELD_INFO(field);
    if (NULL == bf) {
        return NULL;
    }
    if (strlen(bf->name) >= buflen) {
        return NULL;
    }
    strncpy(buf, bf->name, buflen);
    return buf;
}


size_t
skBagFieldTypeGetLength(
    skBagFieldType_t    field)
{
    const bag_field_info_t *bf;

    bf = BAG_GET_FIELD_INFO(field);
    if (NULL == bf) {
        return SKBAG_OCTETS_UNKNOWN;
    }
    return bf->octets;
}


skBagErr_t
skBagFieldTypeIteratorBind(
    skBagFieldTypeIterator_t   *ft_iter)
{
    return skBagFieldTypeIteratorReset(ft_iter);
}


skBagErr_t
skBagFieldTypeIteratorNext(
    skBagFieldTypeIterator_t   *ft_iter,
    skBagFieldType_t           *field_type,
    size_t                     *field_octets,
    char                       *type_name,
    size_t                      type_name_len)
{
    if (NULL == ft_iter) {
        return SKBAG_ERR_INPUT;
    }
    if (ft_iter->no_more_entries) {
        return SKBAG_ERR_KEY_NOT_FOUND;
    }
    if (field_type) {
        *field_type = ft_iter->val;
    }
    if (field_octets) {
        *field_octets = skBagFieldTypeGetLength(ft_iter->val);
    }
    if (type_name && type_name_len) {
        skBagFieldTypeAsString(ft_iter->val, type_name, type_name_len);
    }
    while (ft_iter->val < (skBagFieldType_t)(BAG_NUM_FIELDS - 1u)) {
        ft_iter->val = (skBagFieldType_t)(1 + (int)ft_iter->val);
        if (bag_field_info[ft_iter->val].octets > 0) {
            return SKBAG_OK;
        }
        /* else field is not currently supported; try next field */
    }
    if (SKBAG_FIELD_CUSTOM == ft_iter->val) {
        ft_iter->no_more_entries = 1;
    } else {
        ft_iter->val = SKBAG_FIELD_CUSTOM;
    }
    return SKBAG_OK;
}


skBagErr_t
skBagFieldTypeIteratorReset(
    skBagFieldTypeIterator_t   *ft_iter)
{
    if (NULL == ft_iter) {
        return SKBAG_ERR_INPUT;
    }
    ft_iter->no_more_entries = 0;
    ft_iter->val = (skBagFieldType_t)0;
    do {
        if (bag_field_info[ft_iter->val].octets > 0) {
            return SKBAG_OK;
        }
        /* else field is not currently supported; try next field */
        ft_iter->val = (skBagFieldType_t)(1 + (int)ft_iter->val);

    } while (ft_iter->val < BAG_NUM_FIELDS);
    ft_iter->val = SKBAG_FIELD_CUSTOM;
    return SKBAG_OK;
}


skBagErr_t
skBagFieldTypeLookup(
    const char         *type_name,
    skBagFieldType_t   *field_type,
    size_t             *field_octets)
{
    const bag_field_info_t *bf;
    size_t i;
    skBagErr_t rv = SKBAG_ERR_INPUT;

    if (0 == strcasecmp(bag_field_info_custom.name, type_name)) {
        i = SKBAG_FIELD_CUSTOM;
        bf = &bag_field_info_custom;
        rv = SKBAG_OK;
    } else {
        for (i = 0, bf = bag_field_info; i < BAG_NUM_FIELDS; ++i, ++bf) {
            if ((bf->octets > 0) && (0 == strcasecmp(bf->name, type_name))) {
                rv = SKBAG_OK;
                break;
            }
        }
    }

    if (SKBAG_OK == rv) {
        if (field_type) {
            *field_type = (skBagFieldType_t)i;
        }
        if (field_octets) {
            *field_octets = bf->octets;
        }
    }
    return rv;
}


skBagFieldType_t
skBagFieldTypeMerge(
    skBagFieldType_t    ftype1,
    skBagFieldType_t    ftype2)
{
    if (ftype1 == ftype2) {
        return ftype1;
    }

    switch (ftype1) {
      case SKBAG_FIELD_SIPv4:
      case SKBAG_FIELD_DIPv4:
      case SKBAG_FIELD_NHIPv4:
      case SKBAG_FIELD_ANY_IPv4:
        switch (ftype2) {
          case SKBAG_FIELD_SIPv4:
          case SKBAG_FIELD_DIPv4:
          case SKBAG_FIELD_NHIPv4:
          case SKBAG_FIELD_ANY_IPv4:
            return SKBAG_FIELD_ANY_IPv4;

          case SKBAG_FIELD_SIPv6:
          case SKBAG_FIELD_DIPv6:
          case SKBAG_FIELD_NHIPv6:
          case SKBAG_FIELD_ANY_IPv6:
            return SKBAG_FIELD_ANY_IPv6;

          default:
            break;
        }
        break;

      case SKBAG_FIELD_SIPv6:
      case SKBAG_FIELD_DIPv6:
      case SKBAG_FIELD_NHIPv6:
      case SKBAG_FIELD_ANY_IPv6:
        switch (ftype2) {
          case SKBAG_FIELD_SIPv4:
          case SKBAG_FIELD_DIPv4:
          case SKBAG_FIELD_NHIPv4:
          case SKBAG_FIELD_ANY_IPv4:
          case SKBAG_FIELD_SIPv6:
          case SKBAG_FIELD_DIPv6:
          case SKBAG_FIELD_NHIPv6:
          case SKBAG_FIELD_ANY_IPv6:
            return SKBAG_FIELD_ANY_IPv6;
          default:
            break;
        }
        break;

      case SKBAG_FIELD_SPORT:
      case SKBAG_FIELD_DPORT:
      case SKBAG_FIELD_ANY_PORT:
        switch (ftype2) {
          case SKBAG_FIELD_SPORT:
          case SKBAG_FIELD_DPORT:
          case SKBAG_FIELD_ANY_PORT:
            return SKBAG_FIELD_ANY_PORT;
          default:
            break;
        }
        break;

      case SKBAG_FIELD_INPUT:
      case SKBAG_FIELD_OUTPUT:
      case SKBAG_FIELD_ANY_SNMP:
        switch (ftype2) {
          case SKBAG_FIELD_INPUT:
          case SKBAG_FIELD_OUTPUT:
          case SKBAG_FIELD_ANY_SNMP:
            return SKBAG_FIELD_ANY_SNMP;
          default:
            break;
        }
        break;

      case SKBAG_FIELD_PACKETS:
      case SKBAG_FIELD_SUM_PACKETS:
        switch (ftype2) {
          case SKBAG_FIELD_PACKETS:
          case SKBAG_FIELD_SUM_PACKETS:
            return SKBAG_FIELD_SUM_PACKETS;
          default:
            break;
        }
        break;

      case SKBAG_FIELD_BYTES:
      case SKBAG_FIELD_SUM_BYTES:
        switch (ftype2) {
          case SKBAG_FIELD_BYTES:
          case SKBAG_FIELD_SUM_BYTES:
            return SKBAG_FIELD_SUM_BYTES;
          default:
            break;
        }
        break;

      case SKBAG_FIELD_FLAGS:
      case SKBAG_FIELD_INIT_FLAGS:
      case SKBAG_FIELD_REST_FLAGS:
        switch (ftype2) {
          case SKBAG_FIELD_FLAGS:
          case SKBAG_FIELD_INIT_FLAGS:
          case SKBAG_FIELD_REST_FLAGS:
            return SKBAG_FIELD_FLAGS;
          default:
            break;
        }
        break;

      case SKBAG_FIELD_STARTTIME:
      case SKBAG_FIELD_ENDTIME:
      case SKBAG_FIELD_ANY_TIME:
        switch (ftype2) {
          case SKBAG_FIELD_STARTTIME:
          case SKBAG_FIELD_ENDTIME:
          case SKBAG_FIELD_ANY_TIME:
          case SKBAG_FIELD_ELAPSED:
          case SKBAG_FIELD_SUM_ELAPSED:
            return SKBAG_FIELD_ANY_TIME;
          default:
            break;
        }
        break;

      case SKBAG_FIELD_ELAPSED:
      case SKBAG_FIELD_SUM_ELAPSED:
        switch (ftype2) {
          case SKBAG_FIELD_ELAPSED:
          case SKBAG_FIELD_SUM_ELAPSED:
            return SKBAG_FIELD_SUM_ELAPSED;

          case SKBAG_FIELD_STARTTIME:
          case SKBAG_FIELD_ENDTIME:
          case SKBAG_FIELD_ANY_TIME:
            return SKBAG_FIELD_ANY_TIME;

          default:
            break;
        }
        break;

      case SKBAG_FIELD_SIP_COUNTRY:
      case SKBAG_FIELD_DIP_COUNTRY:
      case SKBAG_FIELD_ANY_COUNTRY:
        switch (ftype2) {
          case SKBAG_FIELD_SIP_COUNTRY:
          case SKBAG_FIELD_DIP_COUNTRY:
          case SKBAG_FIELD_ANY_COUNTRY:
            return SKBAG_FIELD_ANY_COUNTRY;
          default:
            break;
        }
        break;

      case SKBAG_FIELD_SIP_PMAP:
      case SKBAG_FIELD_DIP_PMAP:
      case SKBAG_FIELD_ANY_IP_PMAP:
        switch (ftype2) {
          case SKBAG_FIELD_SIP_PMAP:
          case SKBAG_FIELD_DIP_PMAP:
          case SKBAG_FIELD_ANY_IP_PMAP:
            return SKBAG_FIELD_ANY_IP_PMAP;
          default:
            break;
        }
        break;

      case SKBAG_FIELD_SPORT_PMAP:
      case SKBAG_FIELD_DPORT_PMAP:
      case SKBAG_FIELD_ANY_PORT_PMAP:
        switch (ftype2) {
          case SKBAG_FIELD_SPORT_PMAP:
          case SKBAG_FIELD_DPORT_PMAP:
          case SKBAG_FIELD_ANY_PORT_PMAP:
            return SKBAG_FIELD_ANY_PORT_PMAP;
          default:
            break;
        }
        break;

      default:
        break;
    }

    return SKBAG_FIELD_CUSTOM;
}


/* create iterator */
skBagErr_t
skBagIteratorCreate(
    const skBag_t      *bag,
    skBagIterator_t   **iter)
{
    return bagIterCreate(bag, iter, 1);
}


/* create iterator */
skBagErr_t
skBagIteratorCreateUnsorted(
    const skBag_t      *bag,
    skBagIterator_t   **iter)
{
    return bagIterCreate(bag, iter, 0);
}


/* destroy the iterator */
skBagErr_t
skBagIteratorDestroy(
    skBagIterator_t    *iter)
{
    if (NULL == iter) {
        return SKBAG_ERR_INPUT;
    }
    switch (iter->key_octets) {
      case 1:
      case 2:
        break;
      case 4:
        break;
      case 8:
      case 16:
#if SK_ENABLE_IPV6
#if SKBAG_USE_HASHLIB
        if (iter->d.i_hash.keys) {
            free(iter->d.i_hash.keys);
        }
#else
        if (iter->d.i_rbt.rb_iter) {
            rbcloselist(iter->d.i_rbt.rb_iter);
        }
#endif  /* SKBAG_USE_HASHLIB */
#endif  /* SK_ENABLE_IPV6 */
        break;
    }
    memset(iter, 0, sizeof(*iter));
    free(iter);
    return SKBAG_OK;
}


skBagErr_t
skBagIteratorNextTyped(
    skBagIterator_t        *iter,
    skBagTypedKey_t        *key,
    skBagTypedCounter_t    *counter)
{
    /* check input */
    if (NULL == iter) {
        return SKBAG_ERR_INPUT;
    }
    assert(iter->bag);

    if (iter->key_octets != iter->bag->key_octets) {
        return SKBAG_ERR_MODIFIED;
    }

    if (NULL == iter->bag->d.b_tree) {
        return SKBAG_ERR_KEY_NOT_FOUND;
    }
    if (counter->type != SKBAG_COUNTER_ANY
        && counter->type != SKBAG_COUNTER_U64)
    {
        return SKBAG_ERR_INPUT;
    }
    switch (iter->bag->key_octets) {
      case 1:
      case 2:
      case 4:
        return bagIterNextTree(iter, key, counter);
#if SK_ENABLE_IPV6
      case 16:
#if SKBAG_USE_HASHLIB
        return bagIterNextHash(iter, key, counter);
#else
        return bagIterNextRedblack(iter, key, counter);
#endif  /* SKBAG_USE_HASHLIB */
#endif  /* SK_ENABLE_IPV6 */
      case 8:
      default:
        skAbortBadCase(iter->bag->key_octets);
    }

    return SKBAG_ERR_INPUT;
}


/* reset the iterator */
skBagErr_t
skBagIteratorReset(
    skBagIterator_t    *iter)
{
    if (NULL == iter) {
        return SKBAG_ERR_INPUT;
    }
    assert(iter->bag);

    if (iter->key_octets != iter->bag->key_octets) {
        /* destroy the old data structure */
        switch (iter->key_octets) {
          case 1:
          case 2:
          case 4:
            break;
#if SK_ENABLE_IPV6
          case 16:
#if SKBAG_USE_HASHLIB
            if (iter->d.i_hash.keys) {
                free(iter->d.i_hash.keys);
            }
#else
            iter->d.i_rbt.next = NULL;
            if (iter->d.i_rbt.rb_iter) {
                rbcloselist(iter->d.i_rbt.rb_iter);
                iter->d.i_rbt.rb_iter = NULL;
            }
#endif  /* SKBAG_USE_HASHLIB */
            break;
#endif  /* SK_ENABLE_IPV6 */
          case 8:
          default:
            skAbortBadCase(iter->bag->key_octets);
        }
        iter->key_octets = iter->bag->key_octets;
    }

    iter->pos = 0;

    if (NULL == iter->bag->d.b_tree) {
        return SKBAG_OK;
    }
    switch (iter->bag->key_octets) {
      case 1:
      case 2:
      case 4:
        return bagIterResetTree(iter);
#if SK_ENABLE_IPV6
      case 16:
#if SKBAG_USE_HASHLIB
        return bagIterResetHash(iter);
#else
        return bagIterResetRedblack(iter);
#endif  /* SKBAG_USE_HASHLIB */
#endif  /* SK_ENABLE_IPV6 */
      case 8:
      default:
        skAbortBadCase(iter->bag->key_octets);
    }
}


size_t
skBagKeyFieldLength(
    const skBag_t      *bag)
{
    return bag->key_octets;
}


skBagFieldType_t
skBagKeyFieldName(
    const skBag_t      *bag,
    char               *buf,
    size_t              buflen)
{
    const bag_field_info_t *bf;

    bf = BAG_GET_FIELD_INFO(bag->key_type);
    if (NULL == bf) {
        bf = &bag_field_info_custom;
    }
    if (buf && buflen) {
        strncpy(buf, bf->name, buflen);
        buf[buflen-1] = '\0';
    }

    return bag->key_type;
}


skBagFieldType_t
skBagKeyFieldType(
    const skBag_t      *bag)
{
    return bag->key_type;
}


/* Read Bag from filename---a wrapper around skBagRead(). */
skBagErr_t
skBagLoad(
    skBag_t           **bag,
    const char         *filename)
{
    skstream_t *stream = NULL;
    skBagErr_t err = SKBAG_OK;
    ssize_t rv;

    if (NULL == filename || NULL == bag) {
        return SKBAG_ERR_INPUT;
    }

    if ((rv = skStreamCreate(&stream, SK_IO_READ, SK_CONTENT_SILK))
        || (rv = skStreamBind(stream, filename))
        || (rv = skStreamOpen(stream)))
    {
        skStreamPrintLastErr(stream, rv, &skAppPrintErr);
        err = SKBAG_ERR_READ;
        goto END;
    }

    err = skBagRead(bag, stream);

  END:
    skStreamDestroy(&stream);
    return err;
}


skBagErr_t
skBagModify(
    skBag_t            *bag,
    skBagFieldType_t    key_type,
    skBagFieldType_t    counter_type,
    size_t              key_octets,
    size_t              counter_octets)
{
    skBag_t *cpy = NULL;
    skBagIterator_t *iter = NULL;
    skBagTypedKey_t key;
    skBagTypedCounter_t counter;
    skBagErr_t rv;

    /* a value of SKBAG_OCTETS_NO_CHANGE means keep the current size */
    if (SKBAG_OCTETS_NO_CHANGE == key_octets) {
        key_octets = bag->key_octets;
    } else if (SKBAG_OCTETS_FIELD_DEFAULT == key_octets) {
        key_octets = skBagFieldTypeGetLength(key_type);
    }
    if (SKBAG_OCTETS_NO_CHANGE == counter_octets) {
        counter_octets = sizeof(uint64_t);
    } else if (SKBAG_OCTETS_FIELD_DEFAULT == counter_octets) {
        counter_octets = skBagFieldTypeGetLength(counter_type);
    }

    if (bag->key_octets == key_octets) {
        /* only need to change the types */
        bag->key_type = key_type;
        bag->counter_type = counter_type;
        return SKBAG_OK;
    }

    rv = skBagCreateTyped(&cpy, key_type, counter_type,
                          key_octets, counter_octets);
    if (rv) {
        goto END;
    }

    /* Use the new octet lengths to set the type of the key and the
     * counter used by the iterator  */
    switch (key_octets) {
      case 1:
        key.type = SKBAG_KEY_U8;
        break;
      case 2:
        key.type = SKBAG_KEY_U16;
        break;
      case 4:
        key.type = SKBAG_KEY_U32;
        break;
      case 16:
        key.type = SKBAG_KEY_IPADDR;
        break;
      default:
        skAbortBadCase(key_octets);
    }
    switch (counter_octets) {
      case 8:
        counter.type = SKBAG_COUNTER_U64;
        break;
      default:
        skAbortBadCase(key_octets);
    }
    rv = skBagIteratorCreateUnsorted(bag, &iter);
    if (rv) {
        goto END;
    }
    while (skBagIteratorNextTyped(iter, &key, &counter) == SKBAG_OK) {
        rv = skBagCounterSet(cpy, &key, &counter);
        if (rv) {
            goto END;
        }
    }

    /* copy misc data from 'bag' to 'cpy' */
    cpy->no_autoconvert = bag->no_autoconvert;

  END:
    if (iter) {
        skBagIteratorDestroy(iter);
    }
    if (SKBAG_OK == rv) {
        /* swap the innards of 'bag' and 'cpy' */
        skBag_t tmp;
        memcpy(&tmp, bag, sizeof(skBag_t));
        memcpy(bag, cpy, sizeof(skBag_t));
        memcpy(cpy, &tmp, sizeof(skBag_t));
    }
    skBagDestroy(&cpy);
    return rv;
}


/* print statistics for the bag */
skBagErr_t
skBagPrintTreeStats(
    const skBag_t      *bag,
    skstream_t         *stream_out)
{
    bagstats_t stats;

    if (NULL == bag || NULL == stream_out) {
        return SKBAG_ERR_INPUT;
    }

    bagComputeStats(bag, &stats);

    skStreamPrint(stream_out, ("%18s:  %" PRIu64 " (%" PRIu64 " bytes)\n"),
                  "nodes allocated",
                  stats.nodes, stats.nodes_size);

    skStreamPrint(stream_out, "%18s:  %.02f%%\n",
                  "counter density",
                  (100.0 * (double)stats.unique_keys
                   / (double)stats.nodes));

#if BAG_STATS_FIND_MIN_MAX
    skStreamPrint(stream_out, ("%18s:  %" PRIu64 " -> %" PRIu64 "\n"),
                  "key range",
                  stats.min_key, stats.max_key);

    skStreamPrint(stream_out, ("%18s:  %" PRIu64 " -> %" PRIu64 "\n"),
                  "counter range",
                  stats.min_counter, stats.max_counter);
#endif  /* BAG_STATS_FIND_MIN_MAX */

    return SKBAG_OK;
}


skBagErr_t
skBagProcessStreamTyped(
    skstream_t             *stream_in,
    void                   *cb_data,
    skBagStreamInitFunc_t   cb_init_func,
    skBagStreamEntryFunc_t  cb_entry_func)
{
    skBag_t *bag = NULL;
    sk_file_header_t *hdr;
    sk_header_entry_t *hentry;
    int swap_flag;
    sk_file_version_t bag_version;
    size_t key_read_len;
    size_t counter_read_len;
    size_t entry_read_len;
    const bag_field_info_t *bf;
    skBagErr_t err = SKBAG_OK;
    skBagTypedKey_t key;
    skBagTypedCounter_t counter;
    uint32_t bits;
    ssize_t b;
    uint8_t entrybuf[128];
    union val_un {
        uint64_t    u64;
        uint32_t    u32;
        uint16_t    u16;
        uint8_t     u8;
    } val;
    ssize_t rv;

    if (NULL == stream_in) {
        return SKBAG_ERR_INPUT;
    }

    /* read header */
    rv = skStreamReadSilkHeader(stream_in, &hdr);
    if (rv) {
        skStreamPrintLastErr(stream_in, rv, &skAppPrintErr);
        return SKBAG_ERR_READ;
    }

    rv = skStreamCheckSilkHeader(stream_in, FT_RWBAG, 1,
                                 RWBAG_FILE_VERS_KEY_VARIES, &skAppPrintErr);
    if (rv) {
        return SKBAG_ERR_HEADER;
    }

    bag_version = skHeaderGetRecordVersion(hdr);
    if ((bag_version <= 2) &&
        (SK_COMPMETHOD_NONE != skHeaderGetCompressionMethod(hdr)))
    {
        /*skAppPrintErr("Bag files prior to v2 do not support compression");*/
        return SKBAG_ERR_HEADER;
    }

    swap_flag = !skHeaderIsNativeByteOrder(hdr);

    /* allocate a bag so that the key and counter types and lengths
     * can be queried by the callback */
    bag = (skBag_t*)calloc(1, sizeof(skBag_t));
    if (NULL == bag) {
        return SKBAG_ERR_MEMORY;
    }

    /* size of key and counter on disk.  initialize assuming file
     * version v2 or v3 */
    key_read_len = sizeof(uint32_t);
    counter_read_len = sizeof(uint64_t);

    if (1 == bag_version) {
        /* file version v1 used 32bit counters */
        counter_read_len = sizeof(uint32_t);
    }

    hentry = skHeaderGetFirstMatch(hdr, SK_HENTRY_BAG_ID);
    if (NULL == hentry) {
        /* file has no header entry, must be a pre-SiLK-3.0 file */
        if (RWBAG_FILE_VERS_KEY_VARIES <= bag_version) {
            /* this file version didn't exist prior to SiLK-3.0;
             * something is wrong */
            err = SKBAG_ERR_HEADER;
            goto END;
        }
        bag->key_type = SKBAG_FIELD_CUSTOM;
        bag->key_octets = (uint16_t)key_read_len;
        bag->counter_type = SKBAG_FIELD_CUSTOM;
    } else {
        /* SiLK-3.0 or later file; may have fixed or variable key and
         * counter sizes */
        if (RWBAG_FILE_VERS_KEY_VARIES == bag_version) {
            /* binary lengths of keys/counters vary */
            key_read_len = skHentryBagGetKeyLength(hentry);
            counter_read_len = skHentryBagGetCounterLength(hentry);
        }

        bag->key_type = (skBagFieldType_t)skHentryBagGetKeyType(hentry);
        bf = BAG_GET_FIELD_INFO(bag->key_type);
        if (NULL == bf) {
            /* don't recognize the field type; treat as custom */
            bag->key_type = SKBAG_FIELD_CUSTOM;
            bag->key_octets = skHentryBagGetKeyLength(hentry);
        } else if (bf->octets == SKBAG_OCTETS_CUSTOM) {
            /* type was explicitly custom, get length from header */
            bag->key_octets = skHentryBagGetKeyLength(hentry);
        } else {
            /* type is known; use the type's standard length unless it
             * is larger than the on-disk key size */
            bag->key_octets = (uint16_t)bf->octets;
            if (bag->key_octets > key_read_len) {
                bag->key_octets = (uint16_t)key_read_len;
            }
        }

        bag->counter_type = (skBagFieldType_t)skHentryBagGetCounterType(hentry);
        if (NULL == BAG_GET_FIELD_INFO(bag->counter_type)) {
            /* don't recognize the field type; treat as custom */
            bag->counter_type = SKBAG_FIELD_CUSTOM;
        }
        /* counter octets is always 8 */
    }

    /* check that the lengths are not 0 and not too long, and verify
     * that everything is a power of 2 */
    BITS_IN_WORD32(&bits, bag->key_octets);
    if ((bag->key_octets > 16) || (bag->key_octets == 8) || (bits != 1)) {
        err = SKBAG_ERR_HEADER;
        goto END;
    }
    BITS_IN_WORD32(&bits, (uint32_t)key_read_len);
    if ((key_read_len > 16) || (key_read_len == 8) || (bits != 1)) {
        err = SKBAG_ERR_HEADER;
        goto END;
    }
    BITS_IN_WORD32(&bits, (uint32_t)counter_read_len);
    if ((counter_read_len > 8) || (bits != 1)) {
        err = SKBAG_ERR_HEADER;
        goto END;
    }

    /* cannot read IPv6 bags in an IPv4-only compile of SiLK */
#if !SK_ENABLE_IPV6
    if (key_read_len == 16 || bag->key_octets == 16) {
        err = SKBAG_ERR_HEADER;
        goto END;
    }
#endif

    /* compute size of a complete entry, and double check that sizes
     * are reasonable */
    entry_read_len = key_read_len + counter_read_len;
    if (entry_read_len > sizeof(entrybuf)) {
        skAbort();
    }

    /* call the skBagStreamInitFunc_t */
    if (cb_init_func) {
        err = cb_init_func(bag, cb_data);
        if (SKBAG_OK != err) {
            goto END;
        }
    }

    /* set up is complete; read key/counter pairs */
    while ((b = skStreamRead(stream_in, &entrybuf, entry_read_len))
           == (ssize_t)entry_read_len)
    {
        /* get the counter first */
        switch (counter_read_len) {
          case 1:
            BAG_COUNTER_SET(&counter, entrybuf[key_read_len]);
            break;
          case 2:
#if SKBAG_USE_MEMCPY
            memcpy(&val.u16, (entrybuf + key_read_len), sizeof(val.u16));
#else
            val.u16 = *(uint16_t*)(entrybuf + key_read_len);
#endif
            if (swap_flag) {
                BAG_COUNTER_SET(&counter, BSWAP16(val.u16));
            } else {
                BAG_COUNTER_SET(&counter, val.u16);
            }
            break;
          case 4:
#if SKBAG_USE_MEMCPY
            memcpy(&val.u32, (entrybuf + key_read_len), sizeof(val.u32));
#else
            val.u32 = *(uint32_t*)(entrybuf + key_read_len);
#endif
            if (swap_flag) {
                BAG_COUNTER_SET(&counter, BSWAP32(val.u32));
            } else {
                BAG_COUNTER_SET(&counter, val.u32);
            }
            break;
          case 8:
#if SKBAG_USE_MEMCPY
            memcpy(&val.u64, (entrybuf + key_read_len), sizeof(val.u64));
#else
            val.u64 = *(uint64_t*)(entrybuf + key_read_len);
#endif
            if (swap_flag) {
                BAG_COUNTER_SET(&counter, BSWAP64(val.u64));
            } else {
                BAG_COUNTER_SET(&counter, val.u64);
            }
            break;
          default:
            skAbortBadCase(key_read_len);
        }

        /* get the key and invoke the callback */
        switch (key_read_len) {
          case 1:
            key.type = SKBAG_KEY_U32;
            key.val.u32 = (uint32_t)entrybuf[0];
            err = cb_entry_func(bag, &key, &counter, cb_data);
            break;

          case 2:
#if SKBAG_USE_MEMCPY
            memcpy(&val.u16, entrybuf, sizeof(val.u16));
#else
            val.u16 = *(uint16_t*)entrybuf;
#endif
            key.type = SKBAG_KEY_U32;
            if (swap_flag) {
                key.val.u32 = (uint32_t)BSWAP16(val.u16);
            } else {
                key.val.u32 = (uint32_t)val.u16;
            }
            err = cb_entry_func(bag, &key, &counter, cb_data);
            break;

          case 4:
#if SKBAG_USE_MEMCPY
            memcpy(&val.u32, entrybuf, sizeof(val.u32));
#else
            val.u32 = *(uint32_t*)entrybuf;
#endif
            key.type = SKBAG_KEY_U32;
            if (swap_flag) {
                key.val.u32 = (uint32_t)BSWAP32(val.u32);
            } else {
                key.val.u32 = (uint32_t)val.u32;
            }
            err = cb_entry_func(bag, &key, &counter, cb_data);
            break;

#if SK_ENABLE_IPV6
          case 16:
            key.type = SKBAG_KEY_IPADDR;
            skipaddrSetV6(&key.val.addr, entrybuf);
            err = cb_entry_func(bag, &key, &counter, cb_data);
            break;
#endif

          case 8:
          default:
            skAbortBadCase(key_read_len);
        }

        if (err != SKBAG_OK) {
            goto END;
        }
    }

    /* check for a read error or a partially read entry */
    if (b != 0) {
        if (b == -1) {
            skStreamPrintLastErr(stream_in, b, &skAppPrintErr);
        } else {
            skAppPrintErr("Short read");
        }
        err = SKBAG_ERR_READ;
        goto END;
    }

    err = SKBAG_OK;

  END:
    if (bag) {
        skBagDestroy(&bag);
    }
    return err;
}


/* create bag and fill it with contents from file */
skBagErr_t
skBagRead(
    skBag_t           **bag,
    skstream_t         *stream_in)
{
    if (NULL == bag) {
        return SKBAG_ERR_INPUT;
    }

    return skBagProcessStreamTyped(stream_in, bag, &bagProcessStreamInitRead,
                                   &bagProcessStreamEntryRead);
}


/* Write 'bag' to 'filename'--a wrapper around skBagWrite(). */
skBagErr_t
skBagSave(
    const skBag_t      *bag,
    const char         *filename)
{
    skstream_t *stream = NULL;
    skBagErr_t err = SKBAG_OK;
    ssize_t rv;

    if (NULL == filename || NULL == bag) {
        return SKBAG_ERR_INPUT;
    }

    if ((rv = skStreamCreate(&stream, SK_IO_WRITE, SK_CONTENT_SILK))
        || (rv = skStreamBind(stream, filename))
        || (rv = skStreamOpen(stream)))
    {
        skStreamPrintLastErr(stream, rv, &skAppPrintErr);
        err = SKBAG_ERR_OUTPUT;
        goto END;
    }

    err = skBagWrite(bag, stream);

    rv = skStreamClose(stream);
    if (rv) {
        skStreamPrintLastErr(stream, rv, &skAppPrintErr);
        err = SKBAG_ERR_OUTPUT;
    }

  END:
    skStreamDestroy(&stream);
    return err;
}


const char *
skBagStrerror(
    skBagErr_t          err_code)
{
    static char err_buf[32];

    switch (err_code) {
      case SKBAG_OK:
        return "Success";
      case SKBAG_ERR_MEMORY:
        return "Memory allocation error";
      case SKBAG_ERR_KEY_NOT_FOUND:
        return "No more entries in bag";
      case SKBAG_ERR_INPUT:
        return "Invalid argument to function";
      case SKBAG_ERR_OP_BOUNDS:
        return "Overflow/Underflow in counter";
      case SKBAG_ERR_OUTPUT:
        return "Error writing to stream";
      case SKBAG_ERR_READ:
        return "Error reading from stream";
      case SKBAG_ERR_HEADER:
        return "File header values incompatible with this compile of SiLK";
      case SKBAG_ERR_KEY_RANGE:
        return "Key out of range for bag";
      case SKBAG_ERR_MODIFIED:
        return "Bag modified during iteration";
    }

    snprintf(err_buf, sizeof(err_buf), "Unknown Error #%d", (int)err_code);
    return err_buf;
}


/* write bag to file */
skBagErr_t
skBagWrite(
    const skBag_t      *bag,
    skstream_t         *stream_out)
{
    sk_file_header_t *hdr;
    uint32_t key;
    uint64_t counter;
    skBagIterator_t *iter = NULL;
    ssize_t rv;

    if (NULL == bag || NULL == stream_out) {
        return SKBAG_ERR_INPUT;
    }

    hdr = skStreamGetSilkHeader(stream_out);
    skHeaderSetFileFormat(hdr, FT_RWBAG);

    if (bag->key_octets <= 4) {
        /* write a SiLK-2 compatible bag */
        skHeaderSetRecordVersion(hdr, RWBAG_FILE_VERS_KEY_FIXED);
        skHeaderSetRecordLength(hdr,
                                sizeof(uint32_t) + sizeof(uint64_t));
        rv = skHeaderAddBag(hdr, bag->key_type, (uint16_t)sizeof(uint32_t),
                            bag->counter_type,
                            (uint16_t)sizeof(uint64_t));
    } else {
        /* write a SiLK-3.x+ style bag */
        skHeaderSetRecordVersion(hdr, RWBAG_FILE_VERS_KEY_VARIES);
        skHeaderSetRecordLength(hdr,
                                bag->key_octets + sizeof(uint64_t));
        rv = skHeaderAddBag(hdr, bag->key_type, bag->key_octets,
                            bag->counter_type,
                            (uint16_t)sizeof(uint64_t));
    }
    if (rv) {
        return SKBAG_ERR_MEMORY;
    }

    /* output header */
    rv = skStreamWriteSilkHeader(stream_out);
    if (rv) {
        return SKBAG_ERR_OUTPUT;
    }

    /* write key/counter pairs */
    switch (bag->key_octets) {
      case 1:
      case 2:
      case 4:
        rv = skBagIteratorCreate(bag, &iter);
        if (rv) {
            return SKBAG_ERR_MEMORY;
        }
        while (bagtreeIterNext(iter, &key, &counter)) {
            rv = skStreamWrite(stream_out, &key, sizeof(uint32_t));
            rv += skStreamWrite(stream_out, &counter, sizeof(uint64_t));
            if (rv != sizeof(uint32_t)+sizeof(uint64_t)) {
                skBagIteratorDestroy(iter);
                return SKBAG_ERR_OUTPUT;
            }
        }
        skBagIteratorDestroy(iter);
        break;

#if SK_ENABLE_IPV6
      case 16:
#if SKBAG_USE_HASHLIB
        {
            HASH_ITER h_iter;
            uint8_t *key_ptr;
            uint8_t *val_ptr;

            h_iter = hashlib_create_iterator(bag->d.b_hash);
            while (hashlib_iterate(bag->d.b_hash, &h_iter, &key_ptr, &val_ptr)
                   == OK)
            {
#if SKBAG_USE_MEMCPY
                BAG_MEMCPY_COUNTER(&counter, val_ptr);
                if (BAG_COUNTER_IS_ZERO(counter)) {
                    continue;
                }
#else
                if (BAG_COUNTER_IS_ZERO(val_ptr)) {
                    continue;
                }
#endif  /* SKBAG_USE_MEMCPY */

                rv = skStreamWrite(stream_out, key_ptr, bag->key_octets);
                rv += skStreamWrite(stream_out, val_ptr, sizeof(uint64_t));
                if (rv != (int)(bag->key_octets + sizeof(uint64_t))) {
                    return SKBAG_ERR_OUTPUT;
                }
            }
        }
#else  /* SKBAG_USE_HASHLIB */
        {
            RBLIST *rb_iter;
            const bag_keycount128_t *node;

            rb_iter = rbopenlist(bag->d.b_rbt->tree);
            if (NULL == rb_iter) {
                return SKBAG_ERR_MEMORY;
            }
            assert(sizeof(*node) == bag->key_octets + sizeof(uint64_t));
            while ((node = (const bag_keycount128_t*)rbreadlist(rb_iter))
                   != NULL)
            {
                rv = skStreamWrite(stream_out, node, sizeof(*node));
                if (rv != (int)sizeof(*node)) {
                    rbcloselist(rb_iter);
                    return SKBAG_ERR_OUTPUT;
                }
            }
            rbcloselist(rb_iter);
        }
#endif  /* SKBAG_USE_HASHLIB */
        break;
#endif  /* SK_ENABLE_IPV6 */

      case 8:
      default:
        skAbortBadCase(bag->key_octets);
    }

    rv = skStreamFlush(stream_out);
    if (rv) {
        return SKBAG_ERR_OUTPUT;
    }

    return SKBAG_OK;
}


/*    ********************************************************    */
/*    LEGACY FUNCTIONS    */
/*    ********************************************************    */


#include <silk/bagtree.h>

#define MIN_LEVELS 1
#define MAX_LEVELS 32
#define MIN_KEY_SIZE 8
#define MAX_KEY_SIZE 128
#define MIN_LEVEL_BITS 1
#define MAX_LEVEL_BITS 128


typedef struct bag_legacy_proc_stream_st {
    skBagStreamFunc_t   leg_func;
    void               *leg_data;
} bag_legacy_proc_stream_t;


/*
 *    Callback invoked by skBagProcessStreamTyped() to implement the
 *    callback used by the legacy skBagProcessStream(), which expects a
 *    signature of
 *
 *      static skBagErr_t bagLegacyProcessStream(
 *          const skBagKey_t       *key,
 *          const skBagCounter_t   *counter,
 *          void                   *cb_data);
 *
 */
static skBagErr_t
bagLegacyProcessStream(
    const skBag_t               UNUSED(*fake_bag),
    const skBagTypedKey_t              *key,
    const skBagTypedCounter_t          *counter,
    void                               *cb_data)
{
    bag_legacy_proc_stream_t *leg = (bag_legacy_proc_stream_t*)cb_data;

    return leg->leg_func(&key->val.u32, &counter->val.u64, leg->leg_data);
}


skBagErr_t
skBagAddToCounter(
    skBag_t                *bag,
    const skBagKey_t       *key,
    const skBagCounter_t   *counter_add)
{
    skBagTypedKey_t k;
    skBagTypedCounter_t c;

    k.type = SKBAG_KEY_U32;
    k.val.u32 = *key;
    c.type = SKBAG_COUNTER_U64;
    c.val.u64 = *counter_add;

    return skBagCounterAdd(bag, &k, &c, NULL);
}


/* create a bag */
skBagErr_t
skBagAlloc(
    skBag_t                   **bag,
    skBagLevel_t                levels,
    const skBagLevelsize_t     *level_sizes)
{
    uint32_t key_bits = 0;
    uint32_t high_bits;
    skBagLevel_t lvl;

    /* check the level array */
    if (levels < MIN_LEVELS || levels > MAX_LEVELS || NULL == level_sizes) {
        return SKBAG_ERR_INPUT;
    }

    /* count total number of bits */
    for (lvl = 0; lvl < levels; ++lvl) {
        if (level_sizes[lvl] < MIN_LEVEL_BITS ||
            level_sizes[lvl] > MAX_LEVEL_BITS)
        {
            return SKBAG_ERR_INPUT;
        }
        key_bits += level_sizes[lvl];
    }
    if (key_bits < MIN_KEY_SIZE || key_bits > MAX_KEY_SIZE) {
        return SKBAG_ERR_INPUT;
    }

    /* must be a power of 2 */
    BITS_IN_WORD32(&high_bits, key_bits);
    if (high_bits != 1) {
        return SKBAG_ERR_INPUT;
    }

    return skBagCreateTyped(bag, SKBAG_FIELD_CUSTOM, SKBAG_FIELD_CUSTOM,
                            key_bits / CHAR_BIT, sizeof(skBagCounter_t));
}


skBagErr_t
skBagDecrementCounter(
    skBag_t            *bag,
    const skBagKey_t   *key)
{
    skBagTypedKey_t k;

    k.type = SKBAG_KEY_U32;
    k.val.u32 = *key;

    return skBagCounterSubtract(bag, &k, skbag_counter_incr, NULL);
}


/* destroy a bag */
skBagErr_t
skBagFree(
    skBag_t            *bag)
{
    if (NULL == bag) {
        return SKBAG_ERR_INPUT;
    }
    skBagDestroy(&bag);
    return SKBAG_OK;
}


skBagErr_t
skBagGetCounter(
    skBag_t            *bag,
    const skBagKey_t   *key,
    skBagCounter_t     *counter)
{
    skBagTypedKey_t k;
    skBagTypedCounter_t c;
    skBagErr_t rv;

    k.type = SKBAG_KEY_U32;
    k.val.u32 = *key;

    rv = skBagCounterGet(bag, &k, &c);
    if (SKBAG_OK == rv) {
        *counter = c.val.u64;
    }
    return rv;
}


skBagErr_t
skBagIncrCounter(
    skBag_t            *bag,
    const skBagKey_t   *key)
{
    skBagTypedKey_t k;

    k.type = SKBAG_KEY_U32;
    k.val.u32 = *key;

    return skBagCounterAdd(bag, &k, skbag_counter_incr, NULL);
}


/* return next key/counter pair */
skBagErr_t
skBagIteratorNext(
    skBagIterator_t    *iter,
    skBagKey_t         *key,
    skBagCounter_t     *counter)
{
    skBagTypedKey_t k;
    skBagTypedCounter_t c;
    skBagErr_t rv;

    k.type = SKBAG_KEY_U32;
    c.type = SKBAG_COUNTER_U64;
    rv = skBagIteratorNextTyped(iter, &k, &c);
    if (SKBAG_OK == rv) {
        *key = k.val.u32;
        *counter = c.val.u64;
    }
    return rv;
}


skBagErr_t
skBagProcessStream(
    skstream_t         *stream,
    void               *cb_data,
    skBagStreamFunc_t   cb_func)
{
    bag_legacy_proc_stream_t leg;

    leg.leg_func = cb_func;
    leg.leg_data = cb_data;

    return skBagProcessStreamTyped(stream, &leg, NULL, bagLegacyProcessStream);
}


skBagErr_t
skBagRemoveKey(
    skBag_t            *bag,
    const skBagKey_t   *key)
{
    skBagTypedKey_t k;

    k.type = SKBAG_KEY_U32;
    k.val.u32 = *key;

    return skBagCounterSet(bag, &k, skbag_counter_zero);
}


skBagErr_t
skBagSetCounter(
    skBag_t                *bag,
    const skBagKey_t       *key,
    const skBagCounter_t   *counter)
{
    skBagTypedKey_t k;
    skBagTypedCounter_t c;

    k.type = SKBAG_KEY_U32;
    k.val.u32 = *key;
    c.type = SKBAG_COUNTER_U64;
    c.val.u64 = *counter;

    return skBagCounterSet(bag, &k, &c);
}


skBagErr_t
skBagSubtractFromCounter(
    skBag_t                *bag,
    const skBagKey_t       *key,
    const skBagCounter_t   *counter_sub)
{
    skBagTypedKey_t k;
    skBagTypedCounter_t c;

    k.type = SKBAG_KEY_U32;
    k.val.u32 = *key;
    c.type = SKBAG_COUNTER_U64;
    c.val.u64 = *counter_sub;

    return skBagCounterSubtract(bag, &k, &c, NULL);
}


/*
** Local Variables:
** mode:c
** indent-tabs-mode:nil
** c-basic-offset:4
** End:
*/
