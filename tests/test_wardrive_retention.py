"""Host regression fixture for Wardriving segment retention: rotation prunes the oldest segments
from tracked counts, without listing the collection."""
import importlib.util
import pathlib
import unittest

SPEC = importlib.util.spec_from_file_location(
    "coverity_regressions", pathlib.Path(__file__).with_name("test_coverity_regressions.py"))
HELPERS = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(HELPERS)


class WardriveRetentionTests(unittest.TestCase):
    def test_prune_removes_oldest_and_skips_gaps(self):
        function = HELPERS.production_function("main/apps/wardrive/capture.c", "prune_files")
        HELPERS.compile_and_run(
            r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NOT_FOUND 0x105
typedef struct { int unused; } rfscout_media_collection_t;
static rfscout_media_collection_t collection;
static uint32_t retained_oldest;
static uint32_t retained_count;
static bool present[ 1024 ];
static bool fail_remove;

static void segment_name( uint32_t a_sequence, char *a_name, size_t a_capacity )
{
    (void)snprintf( a_name, a_capacity, "%lu", (unsigned long)a_sequence );
}

static esp_err_t media_remove( const rfscout_media_collection_t *a_collection, const char *a_name )
{
    (void)a_collection;
    const size_t index = (size_t)strtoul( a_name, NULL, 10 );
    if( fail_remove )
    {
        return ESP_FAIL;
    }
    if( !present[ index ] )
    {
        return ESP_ERR_NOT_FOUND;
    }
    present[ index ] = false;
    return ESP_OK;
}
"""
            + function
            + r"""
int main( void )
{
    /* Files 10, 11, 14 and 15: 12 and 13 are gaps. */
    present[ 10 ] = present[ 11 ] = present[ 14 ] = present[ 15 ] = true;
    retained_oldest = 10U;
    retained_count = 4U;

    /* At or under the limit nothing is touched. */
    assert( prune_files( 4U ) );
    assert( present[ 10 ] && 10U == retained_oldest );

    /* The oldest goes first, and rotation continues from the next sequence. */
    assert( prune_files( 3U ) );
    assert( !present[ 10 ] && present[ 11 ] && 11U == retained_oldest && 3U == retained_count );

    /* Gaps do not count as removals; newer files survive. */
    assert( prune_files( 1U ) );
    assert( !present[ 11 ] && !present[ 14 ] && present[ 15 ] );
    assert( 15U == retained_oldest && 1U == retained_count );

    /* A failed removal keeps the count, so a later rotation tries again. */
    fail_remove = true;
    assert( !prune_files( 0U ) );
    assert( 15U == retained_oldest && 1U == retained_count );
    fail_remove = false;
    assert( prune_files( 0U ) );
    assert( !present[ 15 ] && 0U == retained_count );

    /* A long run of gaps is bounded and reports incomplete, then continues later. */
    retained_oldest = 20U;
    retained_count = 1U;
    present[ 600 ] = true;
    assert( !prune_files( 0U ) );
    assert( prune_files( 0U ) );
    assert( !present[ 600 ] && 0U == retained_count );
    return 0;
}
"""
        )


if __name__ == "__main__":
    unittest.main()
