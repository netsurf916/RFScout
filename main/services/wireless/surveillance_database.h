/**
    surveillance_database.h : RFScout generated surveillance fact lookup interface
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#pragma once

#ifndef _RFSCOUT_SURVEILLANCE_DATABASE_H_
#define _RFSCOUT_SURVEILLANCE_DATABASE_H_

#include "rf_observation.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum
{
    SURVEILLANCE_SIGNAL_OUI = 0,
    SURVEILLANCE_SIGNAL_NAME_CONTAINS,
    SURVEILLANCE_SIGNAL_NAME_PREFIX,
    SURVEILLANCE_SIGNAL_COMPANY_ID,
    SURVEILLANCE_SIGNAL_SERVICE_UUID16,
    SURVEILLANCE_SIGNAL_SERVICE_UUID128,
    SURVEILLANCE_SIGNAL_REMOTE_ID
} surveillance_signal_t;

typedef struct
{
    uint16_t id;
    surveillance_signature_id_t signature;
    surveillance_class_t classification;
    surveillance_signal_t signal;
    uint8_t weight;
    uint64_t numeric_value;
    const char *text_value;
} surveillance_fact_t;

/** @brief Match database facts against normalized fields and add resulting evidence. */
void surveillance_database_match_observation( rf_device_t *a_observation );

/** @brief Match database facts against one advertised 128-bit BLE UUID and add evidence. */
void surveillance_database_match_service_uuid128( rf_device_t *a_observation,
                                                  const uint8_t a_uuid[ 16 ] );

/** @brief Resolve a generated database fact by generated fact ID. */
const surveillance_fact_t *surveillance_database_fact( uint16_t a_fact_id );

/** @brief Return the number of generated database facts. */
size_t surveillance_database_fact_count( void );

/** @brief Return the display label associated with a generated signature ID. */
const char *surveillance_database_signature_label( surveillance_signature_id_t a_signature );

/** @brief Return the number of generated signatures including the NONE entry. */
size_t surveillance_database_signature_count( void );

#endif // _RFSCOUT_SURVEILLANCE_DATABASE_H_
