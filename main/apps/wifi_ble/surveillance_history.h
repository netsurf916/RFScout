/**
    surveillance_history.h : Consolidated receiver observation history
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/
#pragma once

#include "rf_observation.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct
{
    uint8_t address[ 6 ];
    uint8_t address_type;
    char vendor[ 32 ];
    surveillance_class_t classification;
    double min_latitude;
    double max_latitude;
    double min_longitude;
    double max_longitude;
    uint32_t last_seen_ms;
    bool valid;
    bool dirty;
} surveillance_history_entry_t;

void surveillance_history_open( void );
void surveillance_history_sample( void );
/** @brief Persist records for identities removed by timeout. */
void surveillance_history_process_expired( void );
void surveillance_history_flush( void );
void surveillance_history_compact( void );
bool surveillance_history_get( size_t a_index, surveillance_history_entry_t *a_entry );
size_t surveillance_history_list( surveillance_history_entry_t *a_rows, size_t a_capacity );
