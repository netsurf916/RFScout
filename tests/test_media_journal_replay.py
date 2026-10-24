"""Host regression fixtures for SD journal replay robustness: torn tails, malformed lines, append
rollback and compacted-base supersession, using the production media_journal.c against an
in-memory media store."""
import pathlib
import shutil
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
STORAGE = ROOT / "main/services/storage"
JOURNAL = STORAGE / "media_journal.c"

# media_store.h is the real header; these stand in for its ESP-IDF and app-registry includes.
STUBS = {
    "esp_err.h": """#pragma once
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NO_MEM 0x101
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_INVALID_SIZE 0x104
#define ESP_ERR_NOT_FOUND 0x105
#define ESP_ERR_NOT_ALLOWED 0x10D
""",
    "esp_log.h": """#pragma once
#define ESP_LOGE( tag, ... ) ( (void)( tag ) )
#define ESP_LOGW( tag, ... ) ( (void)( tag ) )
#define ESP_LOGI( tag, ... ) ( (void)( tag ) )
""",
    "app_descriptor.h": """#pragma once
typedef enum
{
    RFSCOUT_APP_HOTSPOTS = 0,
    RFSCOUT_APP_COUNT
} rfscout_app_id_t;
""",
}

# In-memory media store shared with other journal-backed host fixtures.
MEMORY_STORE = r'''
#undef NDEBUG
#include "media_journal.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ in-memory media store */

#define STORE_FILES 24U
#define STORE_FILE_BYTES 8192U
#define STORE_HANDLES 4U

typedef struct
{
    bool used;
    char name[ RFSCOUT_MEDIA_NAME_MAX + 1U ];
    size_t size;
    char data[ STORE_FILE_BYTES ];
} store_file_t;

typedef struct
{
    bool open;
    uint32_t serial;
    store_file_t *file;
    size_t position;
    bool append;
} store_handle_t;

static store_file_t files[ STORE_FILES ];
static store_handle_t handles[ STORE_HANDLES ];
static uint32_t serial;
static uint32_t generation = 1U;
/* Fault injection: write call fail_write_at fails after landing torn_bytes of its data. */
static unsigned write_calls;
static unsigned fail_write_at;
static size_t torn_bytes;
static bool fail_truncate;
static bool fail_close; /* one shot */
static bool fail_create; /* one shot */

static store_file_t *find_file( const char *a_name )
{
    for( size_t index = 0U; index < STORE_FILES; ++index )
    {
        if( files[ index ].used && 0 == strcmp( files[ index ].name, a_name ) )
        {
            return &files[ index ];
        }
    }
    return NULL;
}

static store_file_t *create_file( const char *a_name )
{
    for( size_t index = 0U; index < STORE_FILES; ++index )
    {
        if( !files[ index ].used )
        {
            memset( &files[ index ], 0, sizeof( files[ index ] ) );
            files[ index ].used = true;
            snprintf( files[ index ].name, sizeof( files[ index ].name ), "%s", a_name );
            return &files[ index ];
        }
    }
    assert( !"store full" );
    return NULL;
}

static store_handle_t *handle( const rfscout_media_file_t *a_file )
{
    const size_t slot = NULL != a_file ? (size_t)a_file->slot : 0U;
    if( 0U == slot || slot > STORE_HANDLES )
    {
        return NULL;
    }
    store_handle_t *entry = &handles[ slot - 1U ];
    return entry->open && entry->serial == a_file->serial && entry->file->used ? entry : NULL;
}

esp_err_t media_store_probe( void )
{
    return ESP_OK;
}

uint32_t media_store_generation( void )
{
    return generation;
}

bool media_name_valid( const char *a_name )
{
    return NULL != a_name && 0U != strlen( a_name ) && strlen( a_name ) <= RFSCOUT_MEDIA_NAME_MAX;
}

esp_err_t media_open( const rfscout_media_collection_t *a_collection,
                      const char *a_name,
                      rfscout_media_open_mode_t a_mode,
                      rfscout_media_file_t *a_file )
{
    (void)a_collection;
    store_file_t *file = find_file( a_name );
    if( RFSCOUT_MEDIA_OPEN_CREATE_NEW == a_mode && fail_create )
    {
        fail_create = false;
        return ESP_ERR_NO_MEM;
    }
    if( RFSCOUT_MEDIA_OPEN_READ == a_mode && NULL == file )
    {
        return ESP_ERR_NOT_FOUND;
    }
    if( RFSCOUT_MEDIA_OPEN_CREATE_NEW == a_mode && NULL != file )
    {
        return ESP_ERR_NOT_ALLOWED;
    }
    size_t slot = 0U;
    while( slot < STORE_HANDLES && handles[ slot ].open )
    {
        ++slot;
    }
    if( STORE_HANDLES == slot )
    {
        return ESP_ERR_NO_MEM;
    }
    if( NULL == file )
    {
        file = create_file( a_name );
    }
    if( RFSCOUT_MEDIA_OPEN_REPLACE == a_mode )
    {
        file->size = 0U;
    }
    handles[ slot ] =
        ( store_handle_t ){ true, ++serial, file, 0U, RFSCOUT_MEDIA_OPEN_APPEND == a_mode };
    *a_file = ( rfscout_media_file_t ){ (uint8_t)( slot + 1U ), serial };
    return ESP_OK;
}

esp_err_t
media_read( const rfscout_media_file_t *a_file, void *a_data, size_t a_capacity, size_t *a_read )
{
    store_handle_t *entry = handle( a_file );
    if( NULL == entry )
    {
        return ESP_ERR_INVALID_STATE;
    }
    const size_t available =
        entry->file->size > entry->position ? entry->file->size - entry->position : 0U;
    *a_read = a_capacity < available ? a_capacity : available;
    memcpy( a_data, &entry->file->data[ entry->position ], *a_read );
    entry->position += *a_read;
    return ESP_OK;
}

esp_err_t media_write( const rfscout_media_file_t *a_file, const void *a_data, size_t a_length )
{
    store_handle_t *entry = handle( a_file );
    if( NULL == entry )
    {
        return ESP_ERR_INVALID_STATE;
    }
    const bool fail = ++write_calls == fail_write_at;
    const size_t length = fail && torn_bytes < a_length ? torn_bytes : a_length;
    if( entry->append )
    {
        entry->position = entry->file->size;
    }
    assert( entry->position + length <= STORE_FILE_BYTES );
    memcpy( &entry->file->data[ entry->position ], a_data, length );
    entry->position += length;
    if( entry->position > entry->file->size )
    {
        entry->file->size = entry->position;
    }
    return fail ? ESP_FAIL : ESP_OK;
}

esp_err_t media_seek( const rfscout_media_file_t *a_file,
                      int64_t a_offset,
                      rfscout_media_seek_origin_t a_origin )
{
    store_handle_t *entry = handle( a_file );
    if( NULL == entry )
    {
        return ESP_ERR_INVALID_STATE;
    }
    const int64_t base = RFSCOUT_MEDIA_SEEK_END == a_origin ? (int64_t)entry->file->size : 0;
    if( base + a_offset < 0 || base + a_offset > (int64_t)entry->file->size )
    {
        return ESP_ERR_INVALID_ARG;
    }
    entry->position = (size_t)( base + a_offset );
    return ESP_OK;
}

esp_err_t media_size( const rfscout_media_file_t *a_file, uint64_t *a_size_bytes )
{
    store_handle_t *entry = handle( a_file );
    if( NULL == entry )
    {
        return ESP_ERR_INVALID_STATE;
    }
    *a_size_bytes = entry->file->size;
    return ESP_OK;
}

esp_err_t media_truncate( const rfscout_media_file_t *a_file, uint64_t a_size_bytes )
{
    store_handle_t *entry = handle( a_file );
    if( NULL == entry )
    {
        return ESP_ERR_INVALID_STATE;
    }
    if( fail_truncate )
    {
        return ESP_FAIL;
    }
    if( a_size_bytes < entry->file->size )
    {
        entry->file->size = (size_t)a_size_bytes;
    }
    return ESP_OK;
}

esp_err_t media_sync( const rfscout_media_file_t *a_file )
{
    return NULL != handle( a_file ) ? ESP_OK : ESP_ERR_INVALID_STATE;
}

esp_err_t media_close( rfscout_media_file_t *a_file )
{
    store_handle_t *entry = handle( a_file );
    if( NULL == entry )
    {
        return ESP_ERR_INVALID_STATE;
    }
    entry->open = false;
    *a_file = ( rfscout_media_file_t ){ 0 };
    if( fail_close )
    {
        fail_close = false;
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t media_stat( const rfscout_media_collection_t *a_collection,
                      const char *a_name,
                      rfscout_media_entry_t *a_entry )
{
    (void)a_collection;
    const store_file_t *file = find_file( a_name );
    if( NULL == file )
    {
        return ESP_ERR_NOT_FOUND;
    }
    memset( a_entry, 0, sizeof( *a_entry ) );
    snprintf( a_entry->name, sizeof( a_entry->name ), "%s", file->name );
    a_entry->size_bytes = file->size;
    return ESP_OK;
}

esp_err_t media_remove( const rfscout_media_collection_t *a_collection, const char *a_name )
{
    (void)a_collection;
    store_file_t *file = find_file( a_name );
    if( NULL == file )
    {
        return ESP_ERR_NOT_FOUND;
    }
    file->used = false;
    return ESP_OK;
}

esp_err_t media_rename( const rfscout_media_collection_t *a_collection,
                        const char *a_source_name,
                        const char *a_destination_name )
{
    (void)a_collection;
    store_file_t *source = find_file( a_source_name );
    if( NULL == source )
    {
        return ESP_ERR_NOT_FOUND;
    }
    store_file_t *destination = find_file( a_destination_name );
    if( NULL != destination && destination != source )
    {
        destination->used = false;
    }
    snprintf( source->name, sizeof( source->name ), "%s", a_destination_name );
    return ESP_OK;
}

/* Visits files in slot order, which is put() order, so tests control the directory order. */
esp_err_t media_list( const rfscout_media_collection_t *a_collection,
                      rfscout_media_list_fn a_visit,
                      void *a_context )
{
    (void)a_collection;
    for( size_t index = 0U; index < STORE_FILES; ++index )
    {
        if( !files[ index ].used )
        {
            continue;
        }
        rfscout_media_entry_t entry;
        memset( &entry, 0, sizeof( entry ) );
        snprintf( entry.name, sizeof( entry.name ), "%s", files[ index ].name );
        entry.size_bytes = files[ index ].size;
        if( !a_visit( &entry, a_context ) )
        {
            break;
        }
    }
    return ESP_OK;
}

static void clear_faults( void )
{
    fail_write_at = 0U;
    torn_bytes = 0U;
    fail_truncate = false;
    fail_close = false;
    fail_create = false;
}

/** Card swap: no files, handles or faults survive. */
static void store_reset( void )
{
    memset( files, 0, sizeof( files ) );
    memset( handles, 0, sizeof( handles ) );
    clear_faults();
    ++generation;
}

/** Power loss: files survive as written, open handles do not. */
static void store_reboot( void )
{
    memset( handles, 0, sizeof( handles ) );
    clear_faults();
    ++generation;
}

static void put( const char *a_name, const char *a_text )
{
    store_file_t *file = find_file( a_name );
    if( NULL == file )
    {
        file = create_file( a_name );
    }
    file->size = strlen( a_text );
    assert( file->size <= STORE_FILE_BYTES );
    memcpy( file->data, a_text, file->size );
}

static bool exists( const char *a_name )
{
    return NULL != find_file( a_name );
}

static bool holds( const char *a_name, const char *a_text )
{
    const store_file_t *file = find_file( a_name );
    return NULL != file && strlen( a_text ) == file->size &&
           0 == memcmp( file->data, a_text, file->size );
}

static size_t open_handles( void )
{
    size_t count = 0U;
    for( size_t index = 0U; index < STORE_HANDLES; ++index )
    {
        count += handles[ index ].open ? 1U : 0U;
    }
    return count;
}

'''

HARNESS = MEMORY_STORE + r'''
/* ------------------------------------------------------------------ journal helpers */

static const rfscout_media_collection_t collection = { .system = true, .collection = "test" };
static media_journal_t journal;
static media_journal_reader_t reader;

/** Bind as after a reboot: no journal RAM state survives. */
static void bind( uint32_t a_segment_bytes )
{
    memset( &journal, 0, sizeof( journal ) );
    assert( ESP_OK == media_journal_init( &journal, &collection, "hist", a_segment_bytes, 0U ) );
}

static int format_text( void *a_context, size_t a_index, char *a_line, size_t a_capacity )
{
    const char *const *texts = a_context;
    return media_journal_printf( a_line, a_capacity, 0, "%s", texts[ a_index ] );
}

static esp_err_t append( const char *const *a_texts, size_t a_count )
{
    return media_journal_append( &journal, format_text, (void *)a_texts, a_count );
}

static esp_err_t compact_write( const char *const *a_texts, size_t a_count )
{
    return media_journal_compact_write( &journal, format_text, (void *)a_texts, a_count );
}

/** Replay everything and require exactly these lines; NULL expects a skipped overlong line. */
static void expect_lines( const char *const *a_lines, size_t a_count )
{
    char line[ RFSCOUT_MEDIA_JOURNAL_LINE_MAX ];
    size_t length = 0U;
    assert( ESP_OK == media_journal_read_begin( &journal, &reader ) );
    for( size_t index = 0U; index < a_count; ++index )
    {
        const esp_err_t result = media_journal_read_line( &reader, line, sizeof( line ), &length );
        if( NULL == a_lines[ index ] )
        {
            assert( ESP_ERR_INVALID_SIZE == result );
            continue;
        }
        if( ESP_OK != result || 0 != strcmp( a_lines[ index ], line ) )
        {
            fprintf( stderr, "line %zu: want %s, got %d\n", index, a_lines[ index ], result );
        }
        assert( ESP_OK == result );
        assert( strlen( a_lines[ index ] ) == length && 0 == strcmp( a_lines[ index ], line ) );
    }
    assert( ESP_ERR_NOT_FOUND == media_journal_read_line( &reader, line, sizeof( line ), &length ) );
    media_journal_read_end( &reader );
    assert( 0U == open_handles() );
}

/* ------------------------------------------------------------------ scenarios */

static void torn_final_line( void )
{
    store_reset();
    put( "hist-00000001.jsonl", "{\"n\":1}\n{\"n\":2}\n{\"n\":" );
    bind( 4096U );
    expect_lines( ( const char *[] ){ "{\"n\":1}", "{\"n\":2}" }, 2U );

    /* A segment with a torn tail is never continued, so the torn line stays last. */
    assert( ESP_OK == append( ( const char *[] ){ "{\"n\":3}" }, 1U ) );
    assert( holds( "hist-00000001.jsonl", "{\"n\":1}\n{\"n\":2}\n{\"n\":" ) );
    assert( holds( "hist-00000002.jsonl", "{\"n\":3}\n" ) );
    expect_lines( ( const char *[] ){ "{\"n\":1}", "{\"n\":2}", "{\"n\":3}" }, 3U );

    /* A torn tail in an older segment never joins the next segment's first line. */
    store_reset();
    put( "hist-00000001.jsonl", "{\"n\":1}\n{\"n" );
    put( "hist-00000002.jsonl", "\":2}\n" );
    bind( 4096U );
    expect_lines( ( const char *[] ){ "{\"n\":1}", "\":2}" }, 2U );
}

static void malformed_lines( void )
{
    /* A malformed line, an overlong line inside the read buffer and one longer than the
       buffer itself: each is reported on its own and reading continues. */
    static char text[ 4096 ];
    char *cursor = text;
    cursor += sprintf( cursor, "{\"n\":1}\nnot json at all\n{\"n\":3}\n" );
    memset( cursor, 'x', 800U );
    cursor += 800U;
    *cursor++ = '\n';
    memset( cursor, 'y', 2000U );
    cursor += 2000U;
    sprintf( cursor, "\n{\"n\":6}\n" );
    store_reset();
    put( "hist-00000001.jsonl", text );
    bind( 8192U );
    expect_lines(
        ( const char *[] ){ "{\"n\":1}", "not json at all", "{\"n\":3}", NULL, NULL, "{\"n\":6}" },
        6U );
}

static void create_failure_does_not_advance( void )
{
    store_reset();
    bind( 4096U );
    fail_create = true;
    assert( ESP_OK != append( ( const char *[] ){ "{\"n\":1}" }, 1U ) );
    assert( !media_journal_outcome_uncertain( &journal ) );
    assert( 0U == journal.last );
    assert( !exists( "hist-00000001.jsonl" ) );
    assert( ESP_OK == append( ( const char *[] ){ "{\"n\":1}" }, 1U ) );
    assert( 1U == journal.last );
    assert( holds( "hist-00000001.jsonl", "{\"n\":1}\n" ) );
}

static void append_rollback( void )
{
    store_reset();
    bind( 4096U );
    assert( ESP_OK == append( ( const char *[] ){ "{\"n\":1}", "{\"n\":2}" }, 2U ) );
    assert( holds( "hist-00000001.jsonl", "{\"n\":1}\n{\"n\":2}\n" ) );

    /* The second record fails after a torn prefix; truncation rolls the whole batch back. */
    fail_write_at = write_calls + 2U;
    torn_bytes = 3U;
    assert( ESP_OK != append( ( const char *[] ){ "{\"n\":3}", "{\"n\":4}" }, 2U ) );
    assert( !media_journal_outcome_uncertain( &journal ) );
    assert( holds( "hist-00000001.jsonl", "{\"n\":1}\n{\"n\":2}\n" ) );
    assert( ESP_OK == append( ( const char *[] ){ "{\"n\":3}", "{\"n\":4}" }, 2U ) );
    assert( holds( "hist-00000001.jsonl", "{\"n\":1}\n{\"n\":2}\n{\"n\":3}\n{\"n\":4}\n" ) );
    assert( !exists( "hist-00000002.jsonl" ) );

    /* When truncation fails too, the outcome is uncertain and the torn segment is abandoned. */
    const char *torn = "{\"n\":1}\n{\"n\":2}\n{\"n\":3}\n{\"n\":4}\n{\"n\":5}\n{\"n";
    fail_write_at = write_calls + 2U;
    fail_truncate = true;
    assert( ESP_OK != append( ( const char *[] ){ "{\"n\":5}", "{\"n\":6}" }, 2U ) );
    assert( media_journal_outcome_uncertain( &journal ) );
    clear_faults();
    assert( holds( "hist-00000001.jsonl", torn ) );
    assert( ESP_OK == append( ( const char *[] ){ "{\"n\":7}" }, 1U ) );
    assert( holds( "hist-00000001.jsonl", torn ) );
    assert( holds( "hist-00000002.jsonl", "{\"n\":7}\n" ) );

    /* A failed close leaves even a written batch uncertain, so the segment is abandoned. */
    fail_close = true;
    assert( ESP_OK != append( ( const char *[] ){ "{\"n\":8}" }, 1U ) );
    assert( media_journal_outcome_uncertain( &journal ) );
    assert( ESP_OK == append( ( const char *[] ){ "{\"n\":9}" }, 1U ) );
    assert( holds( "hist-00000003.jsonl", "{\"n\":9}\n" ) );
    expect_lines( ( const char *[] ){ "{\"n\":1}",
                                      "{\"n\":2}",
                                      "{\"n\":3}",
                                      "{\"n\":4}",
                                      "{\"n\":5}",
                                      "{\"n\":7}",
                                      "{\"n\":8}",
                                      "{\"n\":9}" },
                  8U );

    /* After a reboot, a cleanly terminated raw segment is continued. */
    store_reboot();
    bind( 4096U );
    assert( ESP_OK == append( ( const char *[] ){ "{\"n\":10}" }, 1U ) );
    assert( holds( "hist-00000003.jsonl", "{\"n\":9}\n{\"n\":10}\n" ) );
    assert( !exists( "hist-00000004.jsonl" ) );
    assert( 0U == open_handles() );
}

static void segment_order_and_base( void )
{
    /* Sequence order rather than directory order; gaps are skipped, foreign names ignored. */
    store_reset();
    put( "hist-00000010.jsonl", "{\"n\":16}\n" );
    put( "hist-00000002.jsonl", "{\"n\":2}\n" );
    put( "other-00000001.jsonl", "{\"other\":1}\n" );
    put( "hist-00000001.jsonl", "{\"n\":1}\n" );
    put( "hist-notes.jsonl", "{\"notes\":1}\n" );
    bind( 4096U );
    expect_lines( ( const char *[] ){ "{\"n\":1}", "{\"n\":2}", "{\"n\":16}" }, 3U );
    assert( exists( "other-00000001.jsonl" ) && exists( "hist-notes.jsonl" ) );

    /* A committed base supersedes older segments: cleanup interrupted after the commit
       finishes on resolve, and a leftover temporary base is discarded. */
    store_reset();
    put( "hist-00000004.jsonl", "{\"n\":4}\n" );
    put( "hist-00000001.jsonl", "{\"n\":1}\n" );
    put( "hist-00000003c.jsonl", "{\"base\":3}\n" );
    put( "hist-00000005c.tmp", "{\"partial\":5}\n" );
    put( "hist-00000002.jsonl", "{\"n\":2}\n" );
    bind( 4096U );
    expect_lines( ( const char *[] ){ "{\"base\":3}", "{\"n\":4}" }, 2U );
    assert( !exists( "hist-00000001.jsonl" ) && !exists( "hist-00000002.jsonl" ) );
    assert( !exists( "hist-00000005c.tmp" ) );
    assert( exists( "hist-00000003c.jsonl" ) && exists( "hist-00000004.jsonl" ) );
    assert( ESP_OK == append( ( const char *[] ){ "{\"n\":5}" }, 1U ) );
    assert( holds( "hist-00000004.jsonl", "{\"n\":4}\n{\"n\":5}\n" ) );
    assert( !exists( "hist-00000005.jsonl" ) );
}

static void compaction( void )
{
    store_reset();
    bind( 4096U );
    assert( ESP_OK == append( ( const char *[] ){ "{\"n\":1}", "{\"n\":2}" }, 2U ) );

    /* A crash before commit leaves only a temporary base, which resolve removes. */
    assert( ESP_OK == media_journal_compact_begin( &journal ) );
    assert( ESP_ERR_INVALID_STATE == append( ( const char *[] ){ "{\"n\":3}" }, 1U ) );
    assert( ESP_OK == compact_write( ( const char *[] ){ "{\"summary\":1}" }, 1U ) );
    assert( exists( "hist-00000002c.tmp" ) );
    store_reboot();
    bind( 4096U );
    expect_lines( ( const char *[] ){ "{\"n\":1}", "{\"n\":2}" }, 2U );
    assert( !exists( "hist-00000002c.tmp" ) );

    /* An aborted compaction removes its temporary base and changes nothing else. */
    assert( ESP_OK == media_journal_compact_begin( &journal ) );
    assert( ESP_OK == compact_write( ( const char *[] ){ "{\"summary\":1}" }, 1U ) );
    media_journal_compact_abort( &journal );
    assert( !exists( "hist-00000002c.tmp" ) );
    expect_lines( ( const char *[] ){ "{\"n\":1}", "{\"n\":2}" }, 2U );

    /* A committed base replaces every older segment and is never appended to. */
    assert( ESP_OK == media_journal_compact_begin( &journal ) );
    assert( ESP_OK == compact_write( ( const char *[] ){ "{\"summary\":2}" }, 1U ) );
    assert( ESP_OK == media_journal_compact_commit( &journal ) );
    assert( holds( "hist-00000002c.jsonl", "{\"summary\":2}\n" ) );
    assert( !exists( "hist-00000001.jsonl" ) && !exists( "hist-00000002c.tmp" ) );
    assert( ESP_OK == append( ( const char *[] ){ "{\"n\":3}" }, 1U ) );
    assert( holds( "hist-00000002c.jsonl", "{\"summary\":2}\n" ) );
    assert( holds( "hist-00000003.jsonl", "{\"n\":3}\n" ) );
    expect_lines( ( const char *[] ){ "{\"summary\":2}", "{\"n\":3}" }, 2U );
}

int main( int argc, char **argv )
{
    static const struct
    {
        const char *name;
        void ( *run )( void );
    } scenarios[] = {
        { "torn_final_line", torn_final_line },
        { "malformed_lines", malformed_lines },
        { "append_rollback", append_rollback },
        { "create_failure", create_failure_does_not_advance },
        { "segment_order_and_base", segment_order_and_base },
        { "compaction", compaction },
    };
    if( 2 != argc )
    {
        return 2;
    }
    for( size_t index = 0U; index < sizeof( scenarios ) / sizeof( scenarios[ 0 ] ); ++index )
    {
        if( 0 == strcmp( argv[ 1 ], scenarios[ index ].name ) )
        {
            scenarios[ index ].run();
            return 0;
        }
    }
    return 2;
}
'''


class MediaJournalReplayTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = shutil.which("cc")
        if compiler is None:
            raise unittest.SkipTest("C compiler unavailable")
        cls.folder = tempfile.TemporaryDirectory()
        temp = pathlib.Path(cls.folder.name)
        for name, text in STUBS.items():
            (temp / name).write_text(text)
        source = temp / "journal_harness.c"
        source.write_text(HARNESS)
        cls.binary = temp / "journal_harness"
        try:
            # strnlen() is POSIX, not ISO C11.
            subprocess.run(
                [compiler, "-std=c11", "-D_POSIX_C_SOURCE=200809L", "-Wall", "-Wextra", "-Werror",
                 "-I", str(temp), "-I", str(STORAGE), str(source), str(JOURNAL),
                 "-o", str(cls.binary)], check=True
            )
        except BaseException:
            cls.folder.cleanup()
            raise

    @classmethod
    def tearDownClass(cls):
        cls.folder.cleanup()

    def run_scenario(self, name):
        subprocess.run([str(self.binary), name], check=True)

    def test_torn_final_line_is_skipped(self):
        self.run_scenario("torn_final_line")

    def test_malformed_and_overlong_lines_do_not_stop_replay(self):
        self.run_scenario("malformed_lines")

    def test_append_rollback_and_uncertain_outcome(self):
        self.run_scenario("append_rollback")

    def test_failed_segment_create_keeps_sequence(self):
        self.run_scenario("create_failure")

    def test_segment_order_and_compacted_base(self):
        self.run_scenario("segment_order_and_base")

    def test_compaction_temporary_and_commit(self):
        self.run_scenario("compaction")


if __name__ == "__main__":
    unittest.main()
