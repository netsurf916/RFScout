"""Host regression fixture for Wardriving segment retention: pruning from tracked counts without
listing the collection, and deleting the oldest segments when the card limits retention."""
import importlib.util
import pathlib
import unittest

SPEC = importlib.util.spec_from_file_location(
    "coverity_regressions", pathlib.Path(__file__).with_name("test_coverity_regressions.py"))
HELPERS = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(HELPERS)

HARNESS_HEAD = r"""
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
#define MIB ( 1024ULL * 1024ULL )
#define RFSCOUT_LOG_FREE_RESERVE ( 64ULL * MIB )
typedef struct { int unused; } rfscout_media_collection_t;
typedef struct { uint64_t size_bytes; } rfscout_media_entry_t;
typedef struct { uint32_t file_limit_bytes; } wardrive_config_t;
static rfscout_media_collection_t collection;
static wardrive_config_t config = { (uint32_t)( 16ULL * MIB ) };
static uint32_t retained_oldest;
static uint32_t retained_count;
static uint64_t retained_bytes;
static uint64_t card_free;
static uint64_t sizes[ 1024 ]; /* 0 means the sequence number has no file */
static bool fail_remove;
static bool fail_space;

static void segment_name( uint32_t a_sequence, char *a_name, size_t a_capacity )
{
    (void)snprintf( a_name, a_capacity, "%lu", (unsigned long)a_sequence );
}

static esp_err_t media_stat( const rfscout_media_collection_t *a_collection,
                             const char *a_name,
                             rfscout_media_entry_t *a_entry )
{
    (void)a_collection;
    const uint64_t size = sizes[ strtoul( a_name, NULL, 10 ) ];
    if( 0U == size )
    {
        return ESP_ERR_NOT_FOUND;
    }
    a_entry->size_bytes = size;
    return ESP_OK;
}

static esp_err_t media_remove( const rfscout_media_collection_t *a_collection, const char *a_name )
{
    (void)a_collection;
    if( fail_remove )
    {
        return ESP_FAIL;
    }
    const size_t index = (size_t)strtoul( a_name, NULL, 10 );
    card_free += sizes[ index ];
    sizes[ index ] = 0U;
    return ESP_OK;
}

static esp_err_t media_store_space( uint64_t *a_total_bytes, uint64_t *a_free_bytes )
{
    (void)a_total_bytes;
    *a_free_bytes = card_free;
    return fail_space ? ESP_FAIL : ESP_OK;
}

static void retain( uint32_t a_sequence, uint64_t a_size )
{
    sizes[ a_sequence ] = a_size;
    ++retained_count;
    retained_bytes += a_size;
}

static void reset( void )
{
    memset( sizes, 0, sizeof( sizes ) );
    retained_oldest = 10U;
    retained_count = 0U;
    retained_bytes = 0U;
    card_free = 0U;
    fail_remove = false;
    fail_space = false;
}
"""

HARNESS_TESTS = r"""
static void pruning( void )
{
    /* Files 10, 11, 14 and 15: 12 and 13 are gaps. */
    reset();
    retain( 10U, MIB );
    retain( 11U, MIB );
    retain( 14U, MIB );
    retain( 15U, MIB );

    /* At or under the limit nothing is touched. */
    assert( prune_files( 4U ) );
    assert( 0U != sizes[ 10 ] && 10U == retained_oldest );

    /* The oldest goes first, and rotation continues from the next sequence. */
    assert( prune_files( 3U ) );
    assert( 0U == sizes[ 10 ] && 0U != sizes[ 11 ] );
    assert( 11U == retained_oldest && 3U == retained_count && 3U * MIB == retained_bytes );

    /* Gaps do not count as removals; newer files survive. */
    assert( prune_files( 1U ) );
    assert( 0U == sizes[ 11 ] && 0U == sizes[ 14 ] && 0U != sizes[ 15 ] );
    assert( 15U == retained_oldest && 1U == retained_count && MIB == retained_bytes );

    /* A failed removal keeps the totals, so a later rotation tries again. */
    fail_remove = true;
    assert( !prune_files( 0U ) );
    assert( 15U == retained_oldest && 1U == retained_count && MIB == retained_bytes );
    fail_remove = false;
    assert( prune_files( 0U ) );
    assert( 0U == sizes[ 15 ] && 0U == retained_count && 0U == retained_bytes );

    /* A long run of gaps is bounded and reports incomplete, then continues later. */
    reset();
    retained_oldest = 20U;
    retain( 600U, MIB );
    assert( !prune_files( 0U ) );
    assert( prune_files( 0U ) );
    assert( 0U == sizes[ 600 ] && 0U == retained_count );
}

static void card_limits_retention( void )
{
    const uint64_t needed = RFSCOUT_LOG_FREE_RESERVE + 16U * MIB;

    /* Enough room: nothing is deleted. */
    reset();
    retain( 10U, 16U * MIB );
    card_free = needed;
    assert( make_room() );
    assert( 0U != sizes[ 10 ] && 1U == retained_count );

    /* Short of room: only as many of the oldest as it takes, newest kept. */
    reset();
    retain( 10U, 16U * MIB );
    retain( 11U, 16U * MIB );
    retain( 12U, 16U * MIB );
    card_free = needed - 20U * MIB;
    assert( make_room() );
    assert( 0U == sizes[ 10 ] && 0U == sizes[ 11 ] && 0U != sizes[ 12 ] );
    assert( 1U == retained_count && 16U * MIB == retained_bytes && card_free >= needed );

    /* Smaller older segments: several may go for one new segment. */
    reset();
    retain( 10U, 4U * MIB );
    retain( 11U, 4U * MIB );
    retain( 12U, 16U * MIB );
    card_free = needed - 8U * MIB;
    assert( make_room() );
    assert( 0U == sizes[ 11 ] && 0U != sizes[ 12 ] && needed == card_free );

    /* Deleting every segment still would not be enough: nothing is deleted. */
    reset();
    retain( 10U, 16U * MIB );
    retain( 11U, 16U * MIB );
    card_free = needed - 33U * MIB;
    assert( !make_room() );
    assert( 0U != sizes[ 10 ] && 0U != sizes[ 11 ] && 2U == retained_count );

    /* Deleting every segment is exactly enough: allowed. */
    reset();
    retain( 10U, 16U * MIB );
    card_free = needed - 16U * MIB;
    assert( make_room() );
    assert( 0U == retained_count && needed == card_free );

    /* Nothing retained and no room: stop. */
    reset();
    card_free = needed - 1U;
    assert( !make_room() );

    /* A failed removal or space query stops the capture without further deletes. */
    reset();
    retain( 10U, 16U * MIB );
    retain( 11U, 16U * MIB );
    card_free = needed - 8U * MIB;
    fail_remove = true;
    assert( !make_room() );
    assert( 0U != sizes[ 10 ] && 2U == retained_count );
    fail_remove = false;
    fail_space = true;
    assert( !make_room() );
    assert( 0U != sizes[ 10 ] );
}

int main( void )
{
    pruning();
    card_limits_retention();
    return 0;
}
"""


def harness():
    source = "main/apps/wardrive/capture.c"
    functions = [HELPERS.production_function(source, name)
                 for name in ("remove_oldest", "prune_files", "make_room")]
    return HARNESS_HEAD + "\n".join(functions) + HARNESS_TESTS


class WardriveRetentionTests(unittest.TestCase):
    def test_retention_by_count_and_card_space(self):
        HELPERS.compile_and_run(harness())


if __name__ == "__main__":
    unittest.main()
