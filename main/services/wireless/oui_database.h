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
} oui_database_match_t;

/**
 * @brief Resolve IEEE identity and optional curated surveillance metadata.
 * @details IEEE attribution uses longest-prefix matching across MA-S/IAB, MA-M, then MA-L.\n * @return True when IEEE vendor identity matched.
 */
bool oui_database_lookup_info( const uint8_t a_address[ 6 ], oui_database_match_t *a_match );

/**
 * @brief Resolve a globally administered MAC address to an IEEE organization.
 * @return Vendor name from the compiled registries, or NULL when no IEEE match exists.
 */
const char *oui_database_lookup( const uint8_t a_address[ 6 ] );

#endif // _RFSCOUT_OUI_DATABASE_H_
