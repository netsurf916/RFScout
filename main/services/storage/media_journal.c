/**
    media_journal.c : Append-only JSONL journals on removable media
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "media_journal.h"

#include "esp_log.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define JOURNAL_CREATE_ATTEMPTS 16U
#define JOURNAL_DIGITS 8U

typedef enum
{
    JOURNAL_NAME_NONE = 0,
    JOURNAL_NAME_RAW,
    JOURNAL_NAME_BASE,
    JOURNAL_NAME_TEMP,
} journal_name_kind_t;

typedef struct
{
    media_journal_t *journal;
    bool counting;
    uint32_t min;
    uint32_t max;
    uint32_t base;
    uint16_t count;
} journal_scan_t;

static const char *TAG = "media_journal";

static void segment_name( const media_journal_t *a_journal,
                          uint32_t a_sequence,
                          journal_name_kind_t a_kind,
                          char a_name[ RFSCOUT_MEDIA_NAME_MAX + 1U ] )
{
    const char *suffix = ".jsonl";
    if( JOURNAL_NAME_BASE == a_kind )
    {
        suffix = "c.jsonl";
    }
    else if( JOURNAL_NAME_TEMP == a_kind )
    {
        suffix = "c.tmp";
    }
    snprintf( a_name,
              RFSCOUT_MEDIA_NAME_MAX + 1U,
              "%s-%08lx%s",
              a_journal->prefix,
              (unsigned long)a_sequence,
              suffix );
}

static journal_name_kind_t
parse_name( const media_journal_t *a_journal, const char *a_name, uint32_t *a_sequence )
{
    const size_t prefix_length = strlen( a_journal->prefix );
    if( 0 != strncmp( a_name, a_journal->prefix, prefix_length ) || '-' != a_name[ prefix_length ] )
    {
        return JOURNAL_NAME_NONE;
    }
    const char *digits = &a_name[ prefix_length + 1U ];
    uint32_t sequence = 0U;
    for( size_t index = 0U; index < JOURNAL_DIGITS; ++index )
    {
        const char digit = digits[ index ];
        if( digit >= '0' && digit <= '9' )
        {
            sequence = sequence * 16U + (uint32_t)( digit - '0' );
        }
        else if( digit >= 'a' && digit <= 'f' )
        {
            sequence = sequence * 16U + (uint32_t)( digit - 'a' + 10 );
        }
        else
        {
            return JOURNAL_NAME_NONE;
        }
    }
    const char *suffix = &digits[ JOURNAL_DIGITS ];
    journal_name_kind_t kind = JOURNAL_NAME_NONE;
    if( 0 == strcmp( suffix, ".jsonl" ) )
    {
        kind = JOURNAL_NAME_RAW;
    }
    else if( 0 == strcmp( suffix, "c.jsonl" ) )
    {
        kind = JOURNAL_NAME_BASE;
    }
    else if( 0 == strcmp( suffix, "c.tmp" ) )
    {
        kind = JOURNAL_NAME_TEMP;
    }
    if( 0U == sequence )
    {
        return JOURNAL_NAME_NONE;
    }
    *a_sequence = sequence;
    return kind;
}

static bool scan_visit( const rfscout_media_entry_t *a_entry, void *a_context )
{
    journal_scan_t *scan = a_context;
    uint32_t sequence = 0U;
    const journal_name_kind_t kind = parse_name( scan->journal, a_entry->name, &sequence );
    if( JOURNAL_NAME_NONE == kind )
    {
        return true;
    }
    if( JOURNAL_NAME_TEMP == kind )
    {
        /* An uncommitted compaction never became authoritative. */
        if( !scan->counting )
        {
            (void)media_remove( &scan->journal->collection, a_entry->name );
        }
        return true;
    }
    if( scan->counting )
    {
        if( sequence < scan->base )
        {
            /* Superseded by a committed base whose cleanup was interrupted. */
            (void)media_remove( &scan->journal->collection, a_entry->name );
        }
        else
        {
            ++scan->count;
        }
        return true;
    }
    if( 0U == scan->min || sequence < scan->min )
    {
        scan->min = sequence;
    }
    if( sequence > scan->max )
    {
        scan->max = sequence;
    }
    if( JOURNAL_NAME_BASE == kind && sequence > scan->base )
    {
        scan->base = sequence;
    }
    return true;
}

/** A segment may be appended to only when it is raw, below the size bound, and ends cleanly. */
static bool segment_appendable( media_journal_t *a_journal, uint32_t a_sequence )
{
    char name[ RFSCOUT_MEDIA_NAME_MAX + 1U ];
    segment_name( a_journal, a_sequence, JOURNAL_NAME_RAW, name );
    rfscout_media_file_t file = { 0 };
    if( ESP_OK != media_open( &a_journal->collection, name, RFSCOUT_MEDIA_OPEN_READ, &file ) )
    {
        return false;
    }
    uint64_t size = 0U;
    char tail = '\0';
    size_t length = 0U;
    bool appendable = ESP_OK == media_size( &file, &size ) && size < a_journal->segment_bytes;
    if( appendable && size > 0U )
    {
        appendable = ESP_OK == media_seek( &file, -1, RFSCOUT_MEDIA_SEEK_END ) &&
                     ESP_OK == media_read( &file, &tail, 1U, &length ) && 1U == length &&
                     '\n' == tail;
    }
    if( ESP_OK != media_close( &file ) )
    {
        appendable = false;
    }
    return appendable;
}

static esp_err_t resolve( media_journal_t *a_journal )
{
    esp_err_t result = media_store_probe();
    if( ESP_OK != result )
    {
        return result;
    }
    const uint32_t generation = media_store_generation();
    if( a_journal->resolved && generation == a_journal->media_generation )
    {
        return ESP_OK;
    }

    journal_scan_t scan = { .journal = a_journal };
    result = media_list( &a_journal->collection, scan_visit, &scan );
    if( ESP_OK == result )
    {
        scan.counting = true;
        result = media_list( &a_journal->collection, scan_visit, &scan );
    }
    if( ESP_OK != result )
    {
        return result;
    }

    a_journal->first = 0U != scan.base ? scan.base : scan.min;
    a_journal->last = scan.max;
    a_journal->count = scan.count;
    /* Never continue a base or a segment whose tail may be torn. */
    a_journal->advance =
        0U == scan.max || scan.max == scan.base || !segment_appendable( a_journal, scan.max );
    a_journal->media_generation = generation;
    a_journal->resolved = true;
    return ESP_OK;
}

static bool remove_segment( media_journal_t *a_journal, uint32_t a_sequence )
{
    char name[ RFSCOUT_MEDIA_NAME_MAX + 1U ];
    segment_name( a_journal, a_sequence, JOURNAL_NAME_RAW, name );
    if( ESP_OK == media_remove( &a_journal->collection, name ) )
    {
        return true;
    }
    segment_name( a_journal, a_sequence, JOURNAL_NAME_BASE, name );
    return ESP_OK == media_remove( &a_journal->collection, name );
}

static void trim_resolved( media_journal_t *a_journal )
{
    while( 0U != a_journal->max_segments && a_journal->count > a_journal->max_segments &&
           a_journal->first < a_journal->last )
    {
        if( remove_segment( a_journal, a_journal->first ) )
        {
            --a_journal->count;
        }
        ++a_journal->first;
    }
}

static esp_err_t create_segment( media_journal_t *a_journal )
{
    uint32_t candidate = a_journal->last;
    for( size_t attempt = 0U; attempt < JOURNAL_CREATE_ATTEMPTS; ++attempt )
    {
        const uint32_t sequence = ++candidate;
        char name[ RFSCOUT_MEDIA_NAME_MAX + 1U ];
        segment_name( a_journal, sequence, JOURNAL_NAME_RAW, name );
        rfscout_media_file_t file = { 0 };
        esp_err_t result =
            media_open( &a_journal->collection, name, RFSCOUT_MEDIA_OPEN_CREATE_NEW, &file );
        if( ESP_ERR_NOT_ALLOWED == result )
        {
            continue;
        }
        if( ESP_OK != result )
        {
            return result;
        }
        /* Creation succeeded: even a close failure may leave this file on media.
           Keep the real sequence, but never advance it for an unsuccessful open. */
        a_journal->last = sequence;
        if( 0U == a_journal->first )
        {
            a_journal->first = sequence;
        }
        ++a_journal->count;
        result = media_close( &file );
        if( ESP_OK != result )
        {
            return result;
        }
        a_journal->advance = false;
        trim_resolved( a_journal );
        return ESP_OK;
    }
    return ESP_ERR_NOT_ALLOWED;
}

/** Write formatted records; a_written reports whether any byte may have reached the file. */
static esp_err_t write_records( media_journal_t *a_journal,
                                const rfscout_media_file_t *a_file,
                                media_journal_format_fn a_format,
                                void *a_context,
                                size_t a_count,
                                bool *a_written )
{
    for( size_t index = 0U; index < a_count; ++index )
    {
        const int length =
            a_format( a_context, index, a_journal->line, sizeof( a_journal->line ) - 1U );
        if( length < 0 )
        {
            continue;
        }
        if( (size_t)length >= sizeof( a_journal->line ) - 1U )
        {
            ESP_LOGW( TAG, "%s record too long; skipped", a_journal->prefix );
            continue;
        }
        a_journal->line[ length ] = '\n';
        *a_written = true;
        const esp_err_t result = media_write( a_file, a_journal->line, (size_t)length + 1U );
        if( ESP_OK != result )
        {
            return result;
        }
    }
    return ESP_OK;
}

esp_err_t media_journal_init( media_journal_t *a_journal,
                              const rfscout_media_collection_t *a_collection,
                              const char *a_prefix,
                              uint32_t a_segment_bytes,
                              uint16_t a_max_segments )
{
    if( NULL == a_journal || NULL == a_collection || NULL == a_prefix || 0U == a_segment_bytes )
    {
        return ESP_ERR_INVALID_ARG;
    }
    const size_t prefix_length = strnlen( a_prefix, RFSCOUT_MEDIA_JOURNAL_PREFIX_MAX + 1U );
    if( 0U == prefix_length || prefix_length > RFSCOUT_MEDIA_JOURNAL_PREFIX_MAX )
    {
        return ESP_ERR_INVALID_SIZE;
    }
    if( a_journal->compacting )
    {
        media_journal_compact_abort( a_journal );
    }
    memset( a_journal, 0, sizeof( *a_journal ) );
    a_journal->collection = *a_collection;
    memcpy( a_journal->prefix, a_prefix, prefix_length );
    a_journal->segment_bytes = a_segment_bytes;
    a_journal->max_segments = a_max_segments;
    char name[ RFSCOUT_MEDIA_NAME_MAX + 1U ];
    segment_name( a_journal, 1U, JOURNAL_NAME_BASE, name );
    return media_name_valid( name ) ? ESP_OK : ESP_ERR_INVALID_ARG;
}

void media_journal_set_max_segments( media_journal_t *a_journal, uint16_t a_max_segments )
{
    if( NULL != a_journal )
    {
        a_journal->max_segments = a_max_segments;
    }
}

esp_err_t media_journal_trim( media_journal_t *a_journal )
{
    if( NULL == a_journal )
    {
        return ESP_ERR_INVALID_ARG;
    }
    const esp_err_t result = resolve( a_journal );
    if( ESP_OK == result )
    {
        trim_resolved( a_journal );
    }
    return result;
}

esp_err_t media_journal_append( media_journal_t *a_journal,
                                media_journal_format_fn a_format,
                                void *a_context,
                                size_t a_count )
{
    if( NULL == a_journal || NULL == a_format )
    {
        return ESP_ERR_INVALID_ARG;
    }
    if( 0U == a_count )
    {
        return ESP_OK;
    }
    if( a_journal->compacting )
    {
        return ESP_ERR_INVALID_STATE;
    }
    a_journal->append_uncertain = false;
    esp_err_t result = resolve( a_journal );
    if( ESP_OK == result && a_journal->advance )
    {
        result = create_segment( a_journal );
    }
    if( ESP_OK != result )
    {
        return result;
    }

    char name[ RFSCOUT_MEDIA_NAME_MAX + 1U ];
    segment_name( a_journal, a_journal->last, JOURNAL_NAME_RAW, name );
    rfscout_media_file_t file = { 0 };
    result = media_open( &a_journal->collection, name, RFSCOUT_MEDIA_OPEN_APPEND, &file );
    if( ESP_OK != result )
    {
        return result;
    }
    uint64_t size = 0U;
    result = media_size( &file, &size );
    if( ESP_OK == result && size >= a_journal->segment_bytes )
    {
        result = media_close( &file );
        if( ESP_OK == result )
        {
            result = create_segment( a_journal );
        }
        if( ESP_OK != result )
        {
            return result;
        }
        segment_name( a_journal, a_journal->last, JOURNAL_NAME_RAW, name );
        result = media_open( &a_journal->collection, name, RFSCOUT_MEDIA_OPEN_APPEND, &file );
        if( ESP_OK != result )
        {
            return result;
        }
        size = 0U;
    }

    bool written = false;
    if( ESP_OK == result )
    {
        result = write_records( a_journal, &file, a_format, a_context, a_count, &written );
    }
    if( ESP_OK == result && written )
    {
        result = media_sync( &file );
    }
    if( ESP_OK != result && written && ESP_OK != media_truncate( &file, size ) )
    {
        ESP_LOGE( TAG, "%s rollback failed; continuing in a new segment", a_journal->prefix );
        a_journal->advance = true;
        a_journal->append_uncertain = true;
    }
    const esp_err_t close_result = media_close( &file );
    if( ESP_OK != close_result )
    {
        /* Even a successful truncate may not be durable when close fails. */
        a_journal->advance = true;
        a_journal->append_uncertain = true;
        if( ESP_OK == result )
        {
            result = close_result;
        }
    }
    return result;
}

esp_err_t media_journal_size( media_journal_t *a_journal, uint64_t *a_bytes )
{
    if( NULL == a_journal || NULL == a_bytes )
    {
        return ESP_ERR_INVALID_ARG;
    }
    *a_bytes = 0U;
    const esp_err_t result = resolve( a_journal );
    if( ESP_OK != result || 0U == a_journal->first )
    {
        return result;
    }
    for( uint32_t sequence = a_journal->first; sequence <= a_journal->last; ++sequence )
    {
        char name[ RFSCOUT_MEDIA_NAME_MAX + 1U ];
        rfscout_media_entry_t entry;
        segment_name( a_journal, sequence, JOURNAL_NAME_RAW, name );
        if( ESP_OK != media_stat( &a_journal->collection, name, &entry ) )
        {
            segment_name( a_journal, sequence, JOURNAL_NAME_BASE, name );
            if( ESP_OK != media_stat( &a_journal->collection, name, &entry ) )
            {
                continue;
            }
        }
        *a_bytes += entry.size_bytes;
    }
    return ESP_OK;
}

esp_err_t media_journal_compact_begin( media_journal_t *a_journal )
{
    if( NULL == a_journal )
    {
        return ESP_ERR_INVALID_ARG;
    }
    if( a_journal->compacting )
    {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t result = resolve( a_journal );
    if( ESP_OK != result )
    {
        return result;
    }
    const uint32_t sequence = a_journal->last + 1U;
    char name[ RFSCOUT_MEDIA_NAME_MAX + 1U ];
    segment_name( a_journal, sequence, JOURNAL_NAME_TEMP, name );
    result = media_open(
        &a_journal->collection, name, RFSCOUT_MEDIA_OPEN_REPLACE, &a_journal->compact_file );
    if( ESP_OK == result )
    {
        a_journal->compacting = true;
        a_journal->compact_sequence = sequence;
    }
    return result;
}

esp_err_t media_journal_compact_write( media_journal_t *a_journal,
                                       media_journal_format_fn a_format,
                                       void *a_context,
                                       size_t a_count )
{
    if( NULL == a_journal || NULL == a_format )
    {
        return ESP_ERR_INVALID_ARG;
    }
    if( !a_journal->compacting )
    {
        return ESP_ERR_INVALID_STATE;
    }
    bool written = false;
    const esp_err_t result = write_records(
        a_journal, &a_journal->compact_file, a_format, a_context, a_count, &written );
    if( ESP_OK != result )
    {
        media_journal_compact_abort( a_journal );
    }
    return result;
}

esp_err_t media_journal_compact_commit( media_journal_t *a_journal )
{
    if( NULL == a_journal )
    {
        return ESP_ERR_INVALID_ARG;
    }
    if( !a_journal->compacting )
    {
        return ESP_ERR_INVALID_STATE;
    }
    const uint32_t sequence = a_journal->compact_sequence;
    esp_err_t result = media_sync( &a_journal->compact_file );
    const esp_err_t close_result = media_close( &a_journal->compact_file );
    if( ESP_OK == result )
    {
        result = close_result;
    }
    char temporary[ RFSCOUT_MEDIA_NAME_MAX + 1U ];
    char base[ RFSCOUT_MEDIA_NAME_MAX + 1U ];
    segment_name( a_journal, sequence, JOURNAL_NAME_TEMP, temporary );
    segment_name( a_journal, sequence, JOURNAL_NAME_BASE, base );
    if( ESP_OK == result )
    {
        result = media_rename( &a_journal->collection, temporary, base );
    }
    a_journal->compacting = false;
    if( ESP_OK != result )
    {
        (void)media_remove( &a_journal->collection, temporary );
        return result;
    }

    /* The base is authoritative; superseded segments are cleanup (repeated on resolve). */
    for( uint32_t old = a_journal->first; 0U != old && old < sequence; ++old )
    {
        (void)remove_segment( a_journal, old );
    }
    a_journal->first = sequence;
    a_journal->last = sequence;
    a_journal->count = 1U;
    a_journal->advance = true;
    return ESP_OK;
}

void media_journal_compact_abort( media_journal_t *a_journal )
{
    if( NULL == a_journal || !a_journal->compacting )
    {
        return;
    }
    char temporary[ RFSCOUT_MEDIA_NAME_MAX + 1U ];
    segment_name( a_journal, a_journal->compact_sequence, JOURNAL_NAME_TEMP, temporary );
    (void)media_close( &a_journal->compact_file );
    (void)media_remove( &a_journal->collection, temporary );
    a_journal->compacting = false;
}

esp_err_t media_journal_read_begin( media_journal_t *a_journal, media_journal_reader_t *a_reader )
{
    if( NULL == a_journal || NULL == a_reader )
    {
        return ESP_ERR_INVALID_ARG;
    }
    memset( a_reader, 0, sizeof( *a_reader ) );
    const esp_err_t result = resolve( a_journal );
    if( ESP_OK == result )
    {
        a_reader->journal = a_journal;
        a_reader->sequence = a_journal->first;
    }
    return result;
}

static esp_err_t open_next_segment( media_journal_reader_t *a_reader )
{
    media_journal_t *journal = a_reader->journal;
    while( 0U != a_reader->sequence && a_reader->sequence <= journal->last )
    {
        char name[ RFSCOUT_MEDIA_NAME_MAX + 1U ];
        segment_name( journal, a_reader->sequence, JOURNAL_NAME_RAW, name );
        esp_err_t result =
            media_open( &journal->collection, name, RFSCOUT_MEDIA_OPEN_READ, &a_reader->file );
        if( ESP_ERR_NOT_FOUND == result )
        {
            segment_name( journal, a_reader->sequence, JOURNAL_NAME_BASE, name );
            result =
                media_open( &journal->collection, name, RFSCOUT_MEDIA_OPEN_READ, &a_reader->file );
        }
        if( ESP_OK == result )
        {
            a_reader->open = true;
            a_reader->fill = 0U;
            a_reader->position = 0U;
            a_reader->discarding = false;
            return ESP_OK;
        }
        if( ESP_ERR_NOT_FOUND != result )
        {
            return result;
        }
        ++a_reader->sequence;
    }
    return ESP_ERR_NOT_FOUND;
}

esp_err_t media_journal_read_line( media_journal_reader_t *a_reader,
                                   char *a_line,
                                   size_t a_capacity,
                                   size_t *a_length )
{
    if( NULL == a_reader || NULL == a_reader->journal || NULL == a_line || 0U == a_capacity ||
        NULL == a_length )
    {
        return ESP_ERR_INVALID_ARG;
    }
    while( true )
    {
        if( !a_reader->open )
        {
            const esp_err_t result = open_next_segment( a_reader );
            if( ESP_OK != result )
            {
                return result;
            }
        }

        char *start = &a_reader->buffer[ a_reader->position ];
        char *newline = memchr( start, '\n', a_reader->fill - a_reader->position );
        if( NULL != newline )
        {
            const size_t length = (size_t)( newline - start );
            a_reader->position += length + 1U;
            if( a_reader->discarding || length >= a_capacity )
            {
                a_reader->discarding = false;
                return ESP_ERR_INVALID_SIZE;
            }
            memcpy( a_line, start, length );
            a_line[ length ] = '\0';
            *a_length = length;
            return ESP_OK;
        }

        memmove( a_reader->buffer, start, a_reader->fill - a_reader->position );
        a_reader->fill -= a_reader->position;
        a_reader->position = 0U;
        if( a_reader->fill == sizeof( a_reader->buffer ) )
        {
            a_reader->discarding = true;
            a_reader->fill = 0U;
        }

        size_t read = 0U;
        const esp_err_t result = media_read( &a_reader->file,
                                             &a_reader->buffer[ a_reader->fill ],
                                             sizeof( a_reader->buffer ) - a_reader->fill,
                                             &read );
        if( ESP_OK != result || 0U == read )
        {
            /* End of segment: any unterminated remainder is a torn write. */
            (void)media_close( &a_reader->file );
            a_reader->open = false;
            ++a_reader->sequence;
            if( ESP_OK != result )
            {
                return result;
            }
            continue;
        }
        a_reader->fill += read;
    }
}

void media_journal_read_end( media_journal_reader_t *a_reader )
{
    if( NULL != a_reader && a_reader->open )
    {
        (void)media_close( &a_reader->file );
        a_reader->open = false;
    }
}

int media_journal_printf( char *a_line, size_t a_capacity, int a_length, const char *a_format, ... )
{
    if( NULL == a_line || a_length < 0 || (size_t)a_length >= a_capacity )
    {
        return -1;
    }
    va_list arguments;
    va_start( arguments, a_format );
    const int written =
        vsnprintf( &a_line[ a_length ], a_capacity - (size_t)a_length, a_format, arguments );
    va_end( arguments );
    if( written < 0 || (size_t)written >= a_capacity - (size_t)a_length )
    {
        return -1;
    }
    return a_length + written;
}

int media_journal_json_string( char *a_output,
                               size_t a_capacity,
                               const char *a_text,
                               size_t a_length )
{
    if( NULL == a_output || NULL == a_text || a_capacity < 3U )
    {
        return -1;
    }
    size_t out = 0U;
    a_output[ out++ ] = '"';
    for( size_t index = 0U; index < a_length; ++index )
    {
        const unsigned char character = (unsigned char)a_text[ index ];
        if( character < 0x20U || 0x7fU == character )
        {
            if( out + 7U >= a_capacity )
            {
                return -1;
            }
            out += (size_t)snprintf( &a_output[ out ], 7U, "\\u%04x", character );
            continue;
        }
        const bool escape = '"' == character || '\\' == character;
        if( out + ( escape ? 2U : 1U ) + 1U >= a_capacity )
        {
            return -1;
        }
        if( escape )
        {
            a_output[ out++ ] = '\\';
        }
        a_output[ out++ ] = (char)character;
    }
    if( out + 2U > a_capacity )
    {
        return -1;
    }
    a_output[ out++ ] = '"';
    a_output[ out ] = '\0';
    return (int)out;
}
