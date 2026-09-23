/**
    surveillance_database.c : RFScout generated surveillance evidence lookup implementation
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

static void add_evidence_id( rf_device_t *a_observation, uint16_t a_record_id )
{
    for( size_t i = 0U; i < a_observation->surveillance_evidence_count; ++i )
    {
        if( a_record_id == a_observation->surveillance_evidence_ids[ i ] )
        {
            return;
        }
    }

    if( a_observation->surveillance_evidence_count < RFSCOUT_SURVEILLANCE_EVIDENCE_MAX )
    {
        a_observation->surveillance_evidence_ids[ a_observation->surveillance_evidence_count++ ] =
            a_record_id;
    }
    else
    {
        a_observation->surveillance_evidence_overflow = true;
    }
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

    for( size_t i = 0U; i < SURVEILLANCE_EVIDENCE_RECORD_COUNT; ++i )
    {
        const surveillance_evidence_record_t *record = &surveillance_evidence_records[ i ];
        bool matched = false;

        switch( record->signal )
        {
            case SURVEILLANCE_SIGNAL_OUI:
                matched = prefix == (uint32_t)record->numeric_value;
                break;
            case SURVEILLANCE_SIGNAL_NAME_CONTAINS:
                matched = a_observation->has_name &&
                          contains_ci( a_observation->name, record->text_value );
                break;
            case SURVEILLANCE_SIGNAL_NAME_PREFIX:
                matched = a_observation->has_name &&
                          starts_with_ci( a_observation->name, record->text_value );
                break;
            case SURVEILLANCE_SIGNAL_COMPANY_ID:
                matched = a_observation->company_id == (uint16_t)record->numeric_value;
                break;
            case SURVEILLANCE_SIGNAL_SERVICE_UUID16:
                matched = has_service_uuid16( a_observation, (uint16_t)record->numeric_value );
                break;
            case SURVEILLANCE_SIGNAL_REMOTE_ID:
                matched = a_observation->has_remote_id == ( 0U != record->numeric_value );
                break;
            case SURVEILLANCE_SIGNAL_SERVICE_UUID128:
            default:
                break;
        }

        if( matched )
        {
            add_evidence_id( a_observation, record->id );
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

    for( size_t i = 0U; i < SURVEILLANCE_EVIDENCE_RECORD_COUNT; ++i )
    {
        const surveillance_evidence_record_t *record = &surveillance_evidence_records[ i ];
        if( ( SURVEILLANCE_SIGNAL_SERVICE_UUID128 == record->signal ) &&
            ( 0 == strcmp( canonical, record->text_value ) ) )
        {
            add_evidence_id( a_observation, record->id );
        }
    }
}

const surveillance_evidence_record_t *surveillance_database_record( uint16_t a_record_id )
{
    if( ( 0U == a_record_id ) || ( a_record_id > SURVEILLANCE_EVIDENCE_RECORD_COUNT ) )
    {
        return NULL;
    }
    return &surveillance_evidence_records[ a_record_id - 1U ];
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
