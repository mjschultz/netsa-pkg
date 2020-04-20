/*
** Copyright (C) 2001-2020 by Carnegie Mellon University.
**
** @OPENSOURCE_LICENSE_START@
** See license information in ../../LICENSE.txt
** @OPENSOURCE_LICENSE_END@
*/

/* File: hashlib_tests.c: regression testing application for the hash library
 *
 * There's room to improve this to make the testing more thorough.
 */

#include <silk/silk.h>

RCSIDENT("$SiLK: hashlib_tests.c ef14e54179be 2020-04-14 21:57:45Z mthomas $");

#include <silk/hashlib.h>


static void
hashlib_test1(
    void)
{
    int passed = 1;
    uint32_t iKey;
    uint32_t iValue;
    uint32_t *key_ref, *val_ref;
    const uint32_t max_key = 400000;
    const int initial_table_size = 600000;
    HashTable *test_ptr = NULL;
    uint32_t no_value = 0xFFFFFFFF;
    uint8_t *no_value_ptr;
    uint32_t num_found = 0;
    HASH_ITER iter;

    printf("\n--- Testing value-based hash table\n");

    /* Allocate memory for and initialize special "empty" value */
    no_value_ptr = (uint8_t*)malloc(sizeof(iValue));
    if (!no_value_ptr) {
        printf("Out of memory\n");
        exit(EXIT_FAILURE);
    }
    memcpy(no_value_ptr, &no_value, sizeof(iValue));

    /* Create a table to test with */
    test_ptr = hashlib_create_table(sizeof(iKey),
                                    sizeof(iValue),
                                    HTT_INPLACE,   /* values, not pointers */
                                    no_value_ptr,  /* all FF means empty */
                                    NULL, 0, /* No user data */
                                    initial_table_size, DEFAULT_LOAD_FACTOR);
    assert(test_ptr);

    /* done with the no_value_ptr */
    free(no_value_ptr);

    /* Populate the table with integers and their doubles */
    for (iKey = 1; iKey <= max_key; iKey++) {
        iValue = iKey*2;
        hashlib_insert(test_ptr, (uint8_t*)&iKey, (uint8_t**)&val_ref);
        memcpy(val_ref, &iValue, sizeof(iValue));
    }

    /* Validate num entries */
    if (hashlib_count_entries(test_ptr) != max_key) {
        printf(("Error in hashlib_test1."
                " hashlib_count_entries returned incorrect value\n"));
        exit(EXIT_FAILURE);
    }

    printf("Table information:\n");
    hashlib_dump_table_header(stderr, test_ptr);

    printf("Testing iteration\n");
    num_found = 0;
    iter = hashlib_create_iterator(test_ptr);
    while (hashlib_iterate(test_ptr, &iter, (uint8_t**)&key_ref,
                           (uint8_t**)&val_ref)
           != ERR_NOMOREENTRIES)
    {
        uint32_t inv_val = (uint32_t) (*val_ref)/2;
        num_found++;
        if (inv_val != *key_ref) {
            printf("%u --> %u (%u)", *key_ref, *val_ref, inv_val);
            printf("****Incorrect value: %u != %u\n", inv_val, *key_ref);
            exit(EXIT_FAILURE);
        }
    }

    if (num_found != max_key) {
        printf("Iteration failed.  Expected %d entries, found %d\n", max_key,
               num_found);
        exit(EXIT_FAILURE);
    }

    if (passed) {
        printf("Iteration test PASSED.\n");
    } else {
        printf("****Iteration test FAILED.\n");
    }

    printf("Testing lookup\n");
    for (iKey = 1; iKey <= max_key; iKey++) {
        uint32_t inv_val;
        key_ref = &iKey;
        hashlib_lookup(test_ptr, (uint8_t*) &iKey, (uint8_t**) &val_ref);
        inv_val = (*val_ref)/2;
        if (inv_val != *key_ref) {
            printf("%u --> %u (%u)", *key_ref, *val_ref, inv_val);
            printf("****Incorrect value: %u != %u\n", inv_val, *key_ref);
            exit(EXIT_FAILURE);
        }
    }

    if (passed) {
        printf("Lookup test PASSED.\n");
    } else {
        printf("****Lookup test FAILED.\n");
    }

    hashlib_free_table(test_ptr);

}

/* NOTE: remove is not implemented. We may implement it
 * eventually. Remove is intrinsically expensive since it requires a
 * rehash. */
#if 0
static void
hashlib_test_remove(
    void)
{
    HashTable *table_ptr;
    uint8_t *val_ptr;
    int i, j;
    int rv;
    uint32_t key;
    uint32_t removed_keys[] = {
        152, /* -> 62 */
        27,  /* -> 62 */
        7    /* -> 62 */
    };

    uint32_t num_present = 0;
    uint32_t present_keys[300-sizeof(removed_keys)/4];
    int num_removed_keys = sizeof(removed_keys)/4;

    table_ptr =
        hashlib_create_table(sizeof(uint32_t),
                             sizeof(uint32_t),
                             HTT_INPLACE,   /* values, not pointers */
                             0,  /* 0 value is empty */
                             NULL, 0, /* No user data */
                             300, DEFAULT_LOAD_FACTOR);

    /* Add values except those in removed_keys to hash table and
     * to present_keys array */
    for (i = 0; i < 300; i++) {
        uint8_t use_it_bool = 1;

        /* Add it to present vals only if we're not going to remove it */
        for (j = 0; j < num_removed_keys; j++) {
            if (i == removed_keys[j]) {
                use_it_bool = 0;
                break;
            }
        }
        if (use_it_bool) {
            present_keys[num_present++] = i;
        }

        /* Add it to the table */
        key = i;
        rv = hashlib_insert(table_ptr, (uint8_t*) &key, (uint8_t**) &val_ptr);
        *val_ptr = 1;
        assert(rv==OK);
    }

    /* Remove values in removed_keys */
    for (i = 0; i < num_removed_keys; i++) {
        key = removed_keys[i];
        fprintf(stderr, "Removing: %d\n", key);
        rv = hashlib_remove(table_ptr, (uint8_t*) &key);
        assert(rv==OK);
    }

    /* Make sure the values in present_keys are in the hash table */
    for (i = 0; i < num_present; i++) {
        key = present_keys[i];
        fprintf(stderr, "Looking up %d\n", key);
        rv = hashlib_lookup(table_ptr, (uint8_t*) &key, (uint8_t**) val_ptr);
        if (rv != OK) {
            fprintf(stderr, "Couldn't find key: %d\n", key);
            hashlib_dump_table(stdout, table_ptr);
            assert(rv==OK);
        }
    }

    /* Make sure the removed_keys are not */
    for (i = 0; i < num_removed_keys; i++) {
        key = removed_keys[i];
        fprintf(stderr, "Checking: %d. ", key);
        rv = hashlib_lookup(table_ptr, (uint8_t*) &key, (uint8_t**) val_ptr);
        if (rv == ERR_NOTFOUND) {
            fprintf(stderr, "Removed.\n");
        } else if (rv == OK) {
            fprintf(stderr, "*** Found it. ERROR -- not removed\n");
            assert(rv == ERR_NOTFOUND);
        } else {
            fprintf(stderr, "*** Found it. UNEXPECTED ERROR\n");
            assert(rv == ERR_NOTFOUND);
        }
    }
}
#endif /* 0 */


int main(
    int          UNUSED(argc),
    char       UNUSED(**argv))
{
    fprintf(stdout, "Starting regression testing\n");

    hashlib_test1();

    /* If we reached this point, all tests were successful */
    fprintf(stdout, "\nAll tests completed successfully.\n");

    return 0;
}


/*
** Local Variables:
** mode:c
** indent-tabs-mode:nil
** c-basic-offset:4
** End:
*/
