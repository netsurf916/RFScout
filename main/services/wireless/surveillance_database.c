/**
    surveillance_database.c : RFScout generated surveillance fact lookup implementation
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "surveillance_database.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

typedef struct
{
    surveillance_class_t classification;
    const char *brand;
} surveillance_signature_info_t;

#include "surveillance_signatures.inc"

static bool starts_with_ci( const char *a_value, const char *a_fragment )
{
    if( ( NULL == a_value ) || ( NULL == a_fragment ) )
    {
        return false;
    }

    while( '\0' != *a_fragment )
    {
        if( ( '\0' == *a_value ) ||
            ( toupper( (unsigned char)*a_value ) != toupper( (unsigned char)*a_fragment ) ) )
        {
            return false;
        }
        ++a_value;
        ++a_fragment;
    }
    return true;
}

static bool contains_ci( const char *a_value, const char *a_fragment )
{
    if( ( NULL == a_value ) || ( NULL == a_fragment ) )
    {
        return false;
    }

    for( ; '\0' != *a_value; ++a_value )
    {
        if( starts_with_ci( a_value, a_fragment ) )
        {
            return true;
        }
    }
    return false;
}

static bool has_service_uuid16( const rf_device_t *a_observation, uint16_t a_uuid )
{
    for( size_t i = 0U; i < a_observation->service_uuid_count; ++i )
    {
        if( a_uuid == a_observation->service_uuids[ i ] )
        {
            return true;
        }
    }
    return false;
}

static void add_evidence( rf_device_t *a_observation, uint16_t a_fact_id )
{
    if( ( 0U == a_fact_id ) || ( a_fact_id > SURVEILLANCE_FACT_COUNT ) )
    {
        return;
    }

    const size_t bit = (size_t)a_fact_id - 1U;
    a_observation->surveillance_evidence_bits[ bit / 32U ] |= UINT32_C( 1 ) << ( bit % 32U );
}

void surveillance_database_match_observation( rf_device_t *a_observation )
{
    if( NULL == a_observation )
    {
        return;
    }

    const uint32_t prefix = ( (uint32_t)a_observation->address[ 0 ] << 16U ) |
                            ( (uint32_t)a_observation->address[ 1 ] << 8U ) |
                            (uint32_t)a_observation->address[ 2 ];

    for( size_t i = 0U; i < SURVEILLANCE_FACT_COUNT; ++i )
    {
        const surveillance_fact_t *fact = &surveillance_facts[ i ];
        bool matched = false;

        switch( fact->signal )
        {
            case SURVEILLANCE_SIGNAL_OUI:
                matched = prefix == (uint32_t)fact->numeric_value;
                break;
            case SURVEILLANCE_SIGNAL_NAME_CONTAINS:
                matched =
                    a_observation->has_name && contains_ci( a_observation->name, fact->text_value );
                break;
            case SURVEILLANCE_SIGNAL_NAME_PREFIX:
                matched = a_observation->has_name &&
                          starts_with_ci( a_observation->name, fact->text_value );
                break;
            case SURVEILLANCE_SIGNAL_COMPANY_ID:
                matched = a_observation->company_id == (uint16_t)fact->numeric_value;
                break;
            case SURVEILLANCE_SIGNAL_SERVICE_UUID16:
                matched = has_service_uuid16( a_observation, (uint16_t)fact->numeric_value );
                break;
            case SURVEILLANCE_SIGNAL_REMOTE_ID:
                matched = a_observation->has_remote_id == ( 0U != fact->numeric_value );
                break;
            case SURVEILLANCE_SIGNAL_SERVICE_UUID128:
            default:
                break;
        }

        if( matched )
        {
            add_evidence( a_observation, fact->id );
        }
    }
}

void surveillance_database_match_service_uuid128( rf_device_t *a_observation,
                                                  const uint8_t a_uuid[ 16 ] )
{
    if( ( NULL == a_observation ) || ( NULL == a_uuid ) )
    {
        return;
    }

    char canonical[ 33 ];
    for( size_t i = 0U; i < 16U; ++i )
    {
        (void)snprintf( &canonical[ i * 2U ], 3U, "%02X", a_uuid[ 15U - i ] );
    }
    canonical[ 32 ] = '\0';

    for( size_t i = 0U; i < SURVEILLANCE_FACT_COUNT; ++i )
    {
        const surveillance_fact_t *fact = &surveillance_facts[ i ];
        if( ( SURVEILLANCE_SIGNAL_SERVICE_UUID128 == fact->signal ) &&
            ( 0 == strcmp( canonical, fact->text_value ) ) )
        {
            add_evidence( a_observation, fact->id );
        }
    }
}

const surveillance_fact_t *surveillance_database_fact( uint16_t a_fact_id )
{
    if( ( 0U == a_fact_id ) || ( a_fact_id > SURVEILLANCE_FACT_COUNT ) )
    {
        return NULL;
    }
    return &surveillance_facts[ a_fact_id - 1U ];
}

size_t surveillance_database_fact_count( void )
{
    return SURVEILLANCE_FACT_COUNT;
}

const char *surveillance_database_signature_label( surveillance_signature_id_t a_signature )
{
    if( ( 0U == a_signature ) || ( (size_t)a_signature >= SURVEILLANCE_SIGNATURE_COUNT ) )
    {
        return "OTHER";
    }
    return surveillance_signature_info[ a_signature ].brand;
}

size_t surveillance_database_signature_count( void )
{
    return SURVEILLANCE_SIGNATURE_COUNT;
}
