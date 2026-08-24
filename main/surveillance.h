/**
    surveillance.h : RFScout surveillance classification interface
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#pragma once

#ifndef _RFSCOUT_SURVEILLANCE_H_
#define _RFSCOUT_SURVEILLANCE_H_

#include "observation_store.h"

typedef struct
{
    surveillance_class_t classification;
    surveillance_signature_id_t signature;
    uint8_t score;
    uint8_t evidence;
} surveillance_match_t;

/** @brief Classify an observation using all available passive evidence. */
surveillance_match_t surveillance_match( const rf_device_t *a_observation );

/** @brief Compatibility helper returning only the broad classification. */
surveillance_class_t surveillance_classify( const rf_device_t *a_observation );

/** @brief Return the static LCD type/brand label for a specific signature. */
const char *surveillance_signature_label( surveillance_signature_id_t a_signature );

/** @brief Return a display name for a broad surveillance classification. */
const char *surveillance_name( surveillance_class_t a_classification );

/** @brief Return the Morse-code alert letter assigned to a classification. */
char surveillance_morse_letter( surveillance_class_t a_classification );

#endif // _RFSCOUT_SURVEILLANCE_H_
