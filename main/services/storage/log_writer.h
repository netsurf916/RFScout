/**
    log_writer.h : RFScout asynchronous SD log writer
    Description: Builds JSON-line records and commits bounded bursts to SD.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#pragma once

#ifndef _RFSCOUT_LOG_WRITER_H_
#define _RFSCOUT_LOG_WRITER_H_

#include "esp_err.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct
{
    size_t start;
    bool first_field;
    bool active;
} rfscout_log_record_t;

/** Start asynchronous SD-card logging. */
esp_err_t log_writer_init( void );

/** Begin one JSON-line record. Time/uptime and fresh location metadata are added automatically. */
bool log_begin( rfscout_log_record_t *a_record, const char *a_type );

/** Add one string field. NULL and empty values are omitted. */
bool log_str( rfscout_log_record_t *a_record, const char *a_key, const char *a_value );

/** Add one signed integer field. */
bool log_int( rfscout_log_record_t *a_record, const char *a_key, int64_t a_value );

/** Add one unsigned integer field. */
bool log_uint( rfscout_log_record_t *a_record, const char *a_key, uint64_t a_value );

/** Add one boolean field. */
bool log_bool( rfscout_log_record_t *a_record, const char *a_key, bool a_value );

/** Add one floating-point field. */
bool log_double( rfscout_log_record_t *a_record, const char *a_key, double a_value );

/** Add one byte sequence encoded as an uppercase hexadecimal JSON string. */
bool log_hex( rfscout_log_record_t *a_record,
              const char *a_key,
              const void *a_data,
              size_t a_length );

/** Finish the record and retain it in the current in-memory SD burst. */
bool log_end( rfscout_log_record_t *a_record );

/** Flush buffered records, close the active file, and stop the writer task. */
bool log_writer_shutdown( uint32_t a_timeout_ms );

/** Return sticky SD logging I/O-error state. */
bool log_writer_has_error( void );

#endif // _RFSCOUT_LOG_WRITER_H_
