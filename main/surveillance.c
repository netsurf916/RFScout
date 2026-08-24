/**
    surveillance.c : RFScout surveillance classification
    Description: Matches observations against curated generated OUI and name signatures.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "surveillance.h"

#include <ctype.h>
#include <string.h>

typedef struct
{
    uint32_t prefix;
    surveillance_class_t classification;
    surveillance_signature_id_t signature;
} surveillance_oui_signature_t;

typedef struct
{
    const char *fragment;
    surveillance_class_t classification;
} name_signature_t;

#include "surveillance_signatures.inc"

static const name_signature_t name_signatures[] =
{
    { "AXON", SURVEILLANCE_BODY_CAMERA },
    { "BODY", SURVEILLANCE_BODY_CAMERA },
    { "DJI", SURVEILLANCE_DRONE },
    { "PARROT", SURVEILLANCE_DRONE },
    { "SKYDIO", SURVEILLANCE_DRONE },
    { "RING", SURVEILLANCE_CAMERA },
    { "RAY-BAN", SURVEILLANCE_SMART_GLASSES },
    { "RAYBAN", SURVEILLANCE_SMART_GLASSES },
    { "VUZI", SURVEILLANCE_SMART_GLASSES },
    { "SMART GLASS", SURVEILLANCE_SMART_GLASSES },
};

/** @brief Determine whether one string contains another without case sensitivity. */
static bool contains_ci( const char *a_value, const char *a_fragment )
{
    if( ( NULL == a_value ) || ( NULL == a_fragment ) ) return false;
    const size_t fragment_length = strlen( a_fragment );
    for( ; '\0' != *a_value; ++a_value )
    {
        size_t i = 0;
        while( ( i < fragment_length ) && ( '\0' != a_value[ i ] ) &&
               ( toupper( (unsigned char)a_value[ i ] ) ==
                 toupper( (unsigned char)a_fragment[ i ] ) ) ) ++i;
        if( i == fragment_length ) return true;
    }
    return false;
}

/** @brief Return the 24-bit IEEE prefix from a six-byte device address. */
static uint32_t address_prefix( const uint8_t *a_address )
{
    return ( (uint32_t)a_address[ 0 ] << 16U ) |
           ( (uint32_t)a_address[ 1 ] << 8U ) |
           (uint32_t)a_address[ 2 ];
}

surveillance_class_t surveillance_classify( const rf_device_t *a_observation )
{
    if( NULL == a_observation ) return SURVEILLANCE_NONE;

    /* BLE random/private addresses must never be interpreted as IEEE OUIs. */
    if( ( RF_SOURCE_BLE != a_observation->last_source ) ||
        ( 0U == a_observation->address_type ) )
    {
        const uint32_t prefix = address_prefix( a_observation->address );
        for( size_t i = 0; i < SURVEILLANCE_OUI_SIGNATURE_COUNT; ++i )
        {
            if( prefix == surveillance_oui_signatures[ i ].prefix )
                return surveillance_oui_signatures[ i ].classification;
        }
    }

    if( a_observation->has_name )
    {
        for( size_t i = 0; i < sizeof( name_signatures ) / sizeof( name_signatures[ 0 ] ); ++i )
        {
            if( contains_ci( a_observation->name, name_signatures[ i ].fragment ) )
                return name_signatures[ i ].classification;
        }
    }
    return SURVEILLANCE_NONE;
}

const char *surveillance_name( surveillance_class_t a_classification )
{
    switch( a_classification )
    {
        case SURVEILLANCE_FLOCK: return "Flock / ALPR";
        case SURVEILLANCE_BODY_CAMERA: return "Body camera";
        case SURVEILLANCE_DRONE: return "Drone";
        case SURVEILLANCE_CAMERA: return "Camera / doorbell";
        case SURVEILLANCE_SMART_GLASSES: return "Smart glasses";
        case SURVEILLANCE_OTHER: return "Surveillance";
        default: return "None";
    }
}

char surveillance_morse_letter( surveillance_class_t a_classification )
{
    switch( a_classification )
    {
        case SURVEILLANCE_FLOCK: return 'F';
        case SURVEILLANCE_BODY_CAMERA: return 'B';
        case SURVEILLANCE_DRONE: return 'D';
        case SURVEILLANCE_CAMERA: return 'C';
        case SURVEILLANCE_SMART_GLASSES: return 'G';
        case SURVEILLANCE_OTHER: return 'S';
        default: return '\0';
    }
}
