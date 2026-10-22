/**
    media_journal.h : Append-only JSONL journals on removable media
    Description: One crash-tolerant line journal for every SD-backed event log. A journal is
                 a numbered series of segments in one media collection:
                     <prefix>-<8 hex>.jsonl   raw appended records
                     <prefix>-<8 hex>c.jsonl  compacted base; older segments are superseded
                 Each record is one JSON object on one line. Callers own buffering, record
                 formatting and scheduling; journals are not thread-safe.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#pragma once
#ifndef _RFSCOUT_MEDIA_JOURNAL_H_
#define _RFSCOUT_MEDIA_JOURNAL_H_

#include "media_store.h"

#include "esp_err.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define RFSCOUT_MEDIA_JOURNAL_PREFIX_MAX 24U
#define RFSCOUT_MEDIA_JOURNAL_LINE_MAX 768U

/**
 * @brief Format record a_index as one JSON object without a trailing newline.
 * @return Characters written (excluding NUL), or a negative value to skip the record.
 */
typedef int ( *media_journal_format_fn )( void *a_context,
                                          size_t a_index,
                                          char *a_line,
                                          size_t a_capacity );

typedef struct
{
    rfscout_media_collection_t collection;
    char prefix[ RFSCOUT_MEDIA_JOURNAL_PREFIX_MAX + 1U ];
    uint32_t segment_bytes; /**< start a new segment once the current one reaches this size */
    uint16_t max_segments;  /**< delete the oldest segments beyond this count; 0 = unbounded */
    uint32_t media_generation;
    bool resolved;
    bool advance;   /**< next append must start a fresh segment */
    uint32_t first; /**< oldest live segment, 0 when none */
    uint32_t last;  /**< newest segment, 0 when none */
    uint16_t count;
    bool compacting;
    uint32_t compact_sequence;
    rfscout_media_file_t compact_file;
    char line[ RFSCOUT_MEDIA_JOURNAL_LINE_MAX ];
} media_journal_t;

typedef struct
{
    media_journal_t *journal;
    uint32_t sequence;
    rfscout_media_file_t file;
    bool open;
    bool discarding; /**< skipping the rest of an overlong line */
    size_t fill;
    size_t position;
    char buffer[ RFSCOUT_MEDIA_JOURNAL_LINE_MAX + 256U ];
} media_journal_reader_t;

/**
 * @brief Bind a journal to a resolved collection. No media I/O happens until first use.
 * @details App journals must be bound again for every app session, because app collections
 *          carry the session generation.
 */
esp_err_t media_journal_init( media_journal_t *a_journal,
                              const rfscout_media_collection_t *a_collection,
                              const char *a_prefix,
                              uint32_t a_segment_bytes,
                              uint16_t a_max_segments );

/** @brief Change the retention bound; the next new segment applies it. */
void media_journal_set_max_segments( media_journal_t *a_journal, uint16_t a_max_segments );

/** @brief Delete the oldest segments beyond the retention bound now. */
esp_err_t media_journal_trim( media_journal_t *a_journal );

/**
 * @brief Append a_count formatted records as one durable batch.
 * @details On a failed write the segment is truncated back to its previous size. If that
 *          outcome is uncertain the segment is abandoned and later appends start a fresh
 *          segment, so a torn line is never followed by more records.
 *          ESP_OK means every non-skipped record reached the card.
 */
esp_err_t media_journal_append( media_journal_t *a_journal,
                                media_journal_format_fn a_format,
                                void *a_context,
                                size_t a_count );

/**
 * @brief After a failed append, whether some records of that batch may have persisted.
 * @details When false the batch was rolled back and may be retried without duplicates.
 */
static inline bool media_journal_outcome_uncertain( const media_journal_t *a_journal )
{
    return a_journal->advance;
}

/** @brief Total bytes of live segments; resolves the journal when needed. */
esp_err_t media_journal_size( media_journal_t *a_journal, uint64_t *a_bytes );

/**
 * @brief Begin replacing the journal with a compacted base.
 * @details Write the replacement with media_journal_compact_write(), then commit or abort.
 *          Appends are refused while compaction is open.
 */
esp_err_t media_journal_compact_begin( media_journal_t *a_journal );
esp_err_t media_journal_compact_write( media_journal_t *a_journal,
                                       media_journal_format_fn a_format,
                                       void *a_context,
                                       size_t a_count );
/** @brief Make the base durable, then delete every segment it supersedes. */
esp_err_t media_journal_compact_commit( media_journal_t *a_journal );
void media_journal_compact_abort( media_journal_t *a_journal );

/** @brief Start reading every live record, oldest first. */
esp_err_t media_journal_read_begin( media_journal_t *a_journal, media_journal_reader_t *a_reader );

/**
 * @brief Read the next complete line without its newline.
 * @return ESP_OK, ESP_ERR_NOT_FOUND at the end, ESP_ERR_INVALID_SIZE for a skipped overlong
 *         line, or a media error. An unterminated final line of a segment is a torn write
 *         and is skipped.
 */
esp_err_t media_journal_read_line( media_journal_reader_t *a_reader,
                                   char *a_line,
                                   size_t a_capacity,
                                   size_t *a_length );
void media_journal_read_end( media_journal_reader_t *a_reader );

/**
 * @brief Append formatted text at a_length within a record line.
 * @details Lets formatters chain optional fields: a negative a_length is passed through.
 * @return The new length, or -1 when the text does not fit.
 */
int media_journal_printf( char *a_line, size_t a_capacity, int a_length, const char *a_format, ... )
    __attribute__( ( format( printf, 4, 5 ) ) );

/**
 * @brief Write a_length bytes as a quoted JSON string.
 * @details Escapes quotes, backslashes and control characters; other bytes pass through so
 *          replay returns the original bytes.
 * @return Characters written (excluding NUL), or -1 when the output does not fit.
 */
int media_journal_json_string( char *a_output,
                               size_t a_capacity,
                               const char *a_text,
                               size_t a_length );

#endif // _RFSCOUT_MEDIA_JOURNAL_H_
