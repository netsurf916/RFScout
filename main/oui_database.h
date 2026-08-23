/**
    oui_database.h : RFScout IEEE OUI lookup interface
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#pragma once

#ifndef _RFSCOUT_OUI_DATABASE_H_
#define _RFSCOUT_OUI_DATABASE_H_

#include <stdint.h>

/**
 * @brief Resolve a globally administered MAC address to an IEEE organization.
 * @return Vendor name from the compiled registry, or NULL when no match exists.
 */
const char *oui_database_lookup( const uint8_t a_address[ 6 ] );

#endif // _RFSCOUT_OUI_DATABASE_H_
