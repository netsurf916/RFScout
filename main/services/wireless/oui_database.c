/**
    oui_database.c : RFScout IEEE prefix identity lookup implementation
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "oui_database.h"

#include <stddef.h>
#include <string.h>

typedef struct
{
    uint32_t prefix;
    const char *brand;
} oui_record_t;

typedef struct
{
    uint32_t prefix;
    const char *brand;
} oui28_record_t;

typedef struct
{
    uint64_t prefix;
    const char *brand;
} oui36_record_t;

typedef struct
{
    uint32_t prefix;
    surveillance_class_t classification;
    surveillance_signature_id_t signature;
} surveillance_oui_signature_t;

#include "oui_database.inc"
#include "oui_database_extended.inc"
#include "surveillance_signatures.inc"

static const char *lookup_24( uint32_t a_prefix )
{
    size_t low = 0U;
    size_t high = OUI_RECORD_COUNT;

    while( low < high )
    {
        const size_t middle = low + ( ( high - low ) / 2U );
        if( oui_records[ middle ].prefix < a_prefix )
        {
            low = middle + 1U;
        }
        else
        {
            high = middle;
        }
    }

    return ( ( low < OUI_RECORD_COUNT ) && ( oui_records[ low ].prefix == a_prefix ) )
               ? oui_records[ low ].brand
               : NULL;
}

static const char *lookup_28( uint32_t a_prefix )
{
    size_t low = 0U;
    size_t high = OUI28_RECORD_COUNT;

    while( low < high )
    {
        const size_t middle = low + ( ( high - low ) / 2U );
        if( oui28_records[ middle ].prefix < a_prefix )
        {
            low = middle + 1U;
        }
        else
        {
            high = middle;
        }
    }

    return ( ( low < OUI28_RECORD_COUNT ) && ( oui28_records[ low ].prefix == a_prefix ) )
               ? oui28_records[ low ].brand
               : NULL;
}

static const char *lookup_36( uint64_t a_prefix )
{
    size_t low = 0U;
    size_t high = OUI36_RECORD_COUNT;

    while( low < high )
    {
        const size_t middle = low + ( ( high - low ) / 2U );
        if( oui36_records[ middle ].prefix < a_prefix )
        {
            low = middle + 1U;
        }
        else
        {
            high = middle;
        }
    }

    return ( ( low < OUI36_RECORD_COUNT ) && ( oui36_records[ low ].prefix == a_prefix ) )
               ? oui36_records[ low ].brand
               : NULL;
}

static const surveillance_oui_signature_t *lookup_surveillance( uint32_t a_prefix )
{
    size_t low = 0U;
    size_t high = SURVEILLANCE_OUI_SIGNATURE_COUNT;

    while( low < high )
    {
        const size_t middle = low + ( ( high - low ) / 2U );
        if( surveillance_oui_signatures[ middle ].prefix < a_prefix )
        {
            low = middle + 1U;
        }
        else
        {
            high = middle;
        }
    }

    return ( ( low < SURVEILLANCE_OUI_SIGNATURE_COUNT ) &&
             ( surveillance_oui_signatures[ low ].prefix == a_prefix ) )
               ? &surveillance_oui_signatures[ low ]
               : NULL;
}

bool oui_database_lookup_info( const uint8_t a_address[ 6 ], oui_database_match_t *a_match )
{
    if( ( NULL == a_address ) || ( NULL == a_match ) )
    {
        return false;
    }

    memset( a_match, 0, sizeof( *a_match ) );

    const uint32_t prefix24 = ( (uint32_t)a_address[ 0 ] << 16U ) |
                              ( (uint32_t)a_address[ 1 ] << 8U ) | (uint32_t)a_address[ 2 ];
    const uint32_t prefix28 =
        ( (uint32_t)a_address[ 0 ] << 20U ) | ( (uint32_t)a_address[ 1 ] << 12U ) |
        ( (uint32_t)a_address[ 2 ] << 4U ) | ( (uint32_t)a_address[ 3 ] >> 4U );
    const uint64_t prefix36 =
        ( (uint64_t)a_address[ 0 ] << 28U ) | ( (uint64_t)a_address[ 1 ] << 20U ) |
        ( (uint64_t)a_address[ 2 ] << 12U ) | ( (uint64_t)a_address[ 3 ] << 4U ) |
        ( (uint64_t)a_address[ 4 ] >> 4U );

    if( 0U == ( a_address[ 0 ] & 0x03U ) )
    {
        a_match->vendor = lookup_36( prefix36 );
        if( NULL != a_match->vendor )
        {
            a_match->vendor_prefix_bits = 36U;
        }
        else
        {
            a_match->vendor = lookup_28( prefix28 );
            if( NULL != a_match->vendor )
            {
                a_match->vendor_prefix_bits = 28U;
            }
            else
            {
                a_match->vendor = lookup_24( prefix24 );
                if( NULL != a_match->vendor )
                {
                    a_match->vendor_prefix_bits = 24U;
                }
            }
        }
    }

    const surveillance_oui_signature_t *surveillance = lookup_surveillance( prefix24 );
    if( NULL != surveillance )
    {
        a_match->surveillance_classification = surveillance->classification;
        a_match->surveillance_signature = surveillance->signature;
    }

    return ( NULL != a_match->vendor ) ||
           ( SURVEILLANCE_SIGNATURE_NONE != a_match->surveillance_signature );
}

const char *oui_database_lookup( const uint8_t a_address[ 6 ] )
{
    oui_database_match_t match;
    return oui_database_lookup_info( a_address, &match ) ? match.vendor : NULL;
}
