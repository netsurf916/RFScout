/**
    surveillance.h : RFScout surveillance classification interface
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#pragma once

#ifndef _RFSCOUT_SURVEILLANCE_H_
#define _RFSCOUT_SURVEILLANCE_H_

#include "observation_store.h"

/** @brief Classify an observation against high-confidence surveillance signatures. */
surveillance_class_t surveillance_classify( const rf_device_t *a_observation );

/** @brief Return a display name for a surveillance classification. */
const char *surveillance_name( surveillance_class_t a_classification );

/** @brief Return the Morse-code alert letter assigned to a classification. */
char surveillance_morse_letter( surveillance_class_t a_classification );

#endif // _RFSCOUT_SURVEILLANCE_H_
