/**
    oui_database.c : RFScout IEEE OUI lookup implementation
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "oui_database.h"

#include <stddef.h>

typedef struct
{
    uint32_t prefix;
    const char *brand;
} oui_record_t;

#include "oui_database.inc"

const char *oui_database_lookup( const uint8_t a_address[ 6 ] )
{
    if( ( NULL == a_address ) || ( 0U != ( a_address[ 0 ] & 0x03U ) ) )
    {
        return NULL;
    }

    const uint32_t prefix =
        ( (uint32_t)a_address[ 0 ] << 16 ) | ( (uint32_t)a_address[ 1 ] << 8 ) | a_address[ 2 ];
    size_t low = 0;
    size_t high = OUI_RECORD_COUNT;

    while( low < high )
    {
        const size_t middle = low + ( ( high - low ) / 2U );
        if( oui_records[ middle ].prefix < prefix )
        {
            low = middle + 1U;
        }
        else
        {
            high = middle;
        }
    }

    if( ( low < OUI_RECORD_COUNT ) && ( oui_records[ low ].prefix == prefix ) )
    {
        return oui_records[ low ].brand;
    }
    return NULL;
}
