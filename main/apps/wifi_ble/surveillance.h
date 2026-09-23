/**
    surveillance.h : RFScout surveillance case scoring interface
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#pragma once

#ifndef _RFSCOUT_SURVEILLANCE_H_
#define _RFSCOUT_SURVEILLANCE_H_

#include "observation_store.h"

/** @brief Match database-defined evidence present in one normalized observation. */
void surveillance_prepare_observation( rf_device_t *a_observation );

/**
 * @brief Merge unique observation evidence into a canonical case and rescore it.
 * @details The caller retains ownership of both structures. Evidence already present in the case
 *          is not counted again.
 */
void surveillance_case_observe( rf_device_t *a_case, const rf_device_t *a_observation );

/** @brief Return the generated display label for a surveillance signature. */
const char *surveillance_signature_label( surveillance_signature_id_t a_signature );

/** @brief Return a display name for a broad surveillance classification. */
const char *surveillance_name( surveillance_class_t a_classification );

/** @brief Return the Morse-code alert letter assigned to a classification. */
char surveillance_morse_letter( surveillance_class_t a_classification );

#endif // _RFSCOUT_SURVEILLANCE_H_
