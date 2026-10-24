/**
    observation_journal.h : normalized JSONL observation envelope
    Description: Shared serializer only; each application owns its journal, sampling and events.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#pragma once
#ifndef _RFSCOUT_OBSERVATION_JOURNAL_H_
#define _RFSCOUT_OBSERVATION_JOURNAL_H_

#include "rf_observation.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define RFSCOUT_OBSERVATION_JOURNAL_SCHEMA 1U

typedef struct
{
    rf_source_t source;
    uint8_t address[ 6 ];
    uint8_t address_type;
    int64_t utc_epoch; /**< Zero when time has not been trusted. */
    uint32_t seen_ms;
    int8_t rssi;
    uint8_t channel;
    bool location_valid;
    double latitude;
    double longitude;
} observation_journal_record_t;

/**
 * @brief Write a common JSON observation object without its closing brace.
 * @details Callers may append app-specific comma-prefixed fields and a closing brace
 *          using their existing journal formatter. A negative return means truncation.
 */
int observation_journal_format_prefix( const observation_journal_record_t *a_record,
                                       char *a_line,
                                       size_t a_capacity );

/** @brief Ready-to-use media_journal_format_fn for arrays of common-only records. */
int observation_journal_format_record( void *a_context,
                                       size_t a_index,
                                       char *a_line,
                                       size_t a_capacity );

#endif // _RFSCOUT_OBSERVATION_JOURNAL_H_
