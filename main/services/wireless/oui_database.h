/**
    oui_database.h : RFScout IEEE prefix identity lookup interface
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#pragma once

#ifndef _RFSCOUT_OUI_DATABASE_H_
#define _RFSCOUT_OUI_DATABASE_H_

#include "observation_store.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct
{
    const char *vendor;
    uint8_t vendor_prefix_bits;
    surveillance_class_t surveillance_classification;
    surveillance_signature_id_t surveillance_signature;
    uint8_t surveillance_score;
} oui_database_match_t;

/**
 * @brief Resolve IEEE identity and optional curated surveillance metadata.
 * @details IEEE attribution uses longest-prefix matching across MA-S/IAB, MA-M, then MA-L.
 *          Curated surveillance prefixes are evaluated independently so field-verified locally
 *          administered prefixes can still contribute surveillance evidence.
 * @return True when either IEEE vendor identity or curated surveillance metadata matched.
 */
bool oui_database_lookup_info( const uint8_t a_address[ 6 ], oui_database_match_t *a_match );

/**
 * @brief Resolve a globally administered MAC address to an IEEE organization.
 * @return Vendor name from the compiled registries, or NULL when no IEEE match exists.
 */
const char *oui_database_lookup( const uint8_t a_address[ 6 ] );

#endif // _RFSCOUT_OUI_DATABASE_H_
