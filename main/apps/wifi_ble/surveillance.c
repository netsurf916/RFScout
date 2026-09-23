/**
    surveillance.c : RFScout surveillance case scoring
    Description: Scores unique database-defined evidence accumulated by canonical observations.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "surveillance.h"

#include "device_config.h"
#include "surveillance_database.h"

#include <string.h>

static uint8_t evidence_source( surveillance_signal_t a_signal )
{
    switch( a_signal )
    {
        case SURVEILLANCE_SIGNAL_OUI:
            return SURVEILLANCE_EVIDENCE_OUI;
        case SURVEILLANCE_SIGNAL_NAME_CONTAINS:
        case SURVEILLANCE_SIGNAL_NAME_PREFIX:
            return SURVEILLANCE_EVIDENCE_NAME;
        case SURVEILLANCE_SIGNAL_COMPANY_ID:
            return SURVEILLANCE_EVIDENCE_COMPANY_ID;
        case SURVEILLANCE_SIGNAL_SERVICE_UUID16:
        case SURVEILLANCE_SIGNAL_SERVICE_UUID128:
            return SURVEILLANCE_EVIDENCE_SERVICE_UUID;
        case SURVEILLANCE_SIGNAL_REMOTE_ID:
            return SURVEILLANCE_EVIDENCE_REMOTE_ID;
        default:
            return 0U;
    }
}

static uint8_t threshold_score( void )
{
    switch( device_config_evidence_threshold() )
    {
        case RFSCOUT_EVIDENCE_STRENGTH_DISTINCTIVE:
            return 6U;
        case RFSCOUT_EVIDENCE_STRENGTH_PROBABLE:
            return 4U;
        case RFSCOUT_EVIDENCE_STRENGTH_POSSIBLE:
        default:
            return 2U;
    }
}

static void evaluate_case( rf_device_t *a_case )
{
    uint8_t best_score = 0U;
    uint8_t best_sources = 0U;
    surveillance_class_t best_classification = SURVEILLANCE_NONE;
    surveillance_signature_id_t best_signature = SURVEILLANCE_SIGNATURE_NONE;
    const size_t signature_count = surveillance_database_signature_count();

    for( size_t signature = 1U; signature < signature_count; ++signature )
    {
        uint16_t score = 0U;
        uint8_t sources = 0U;
        surveillance_class_t classification = SURVEILLANCE_NONE;

        const size_t evidence_count = surveillance_database_evidence_count();
        for( size_t i = 0U; i < evidence_count; ++i )
        {
            if( 0U == ( a_case->surveillance_evidence_bits[ i / 32U ] &
                        ( UINT32_C( 1 ) << ( i % 32U ) ) ) )
            {
                continue;
            }

            const surveillance_evidence_record_t *record =
                surveillance_database_record( (uint16_t)( i + 1U ) );
            if( ( NULL == record ) || ( record->signature != signature ) )
            {
                continue;
            }

            score += record->weight;
            sources |= evidence_source( record->signal );
            classification = record->classification;
        }

        if( score > UINT8_MAX )
        {
            score = UINT8_MAX;
        }
        if( (uint8_t)score > best_score )
        {
            best_score = (uint8_t)score;
            best_sources = sources;
            best_classification = classification;
            best_signature = (surveillance_signature_id_t)signature;
        }
    }

    a_case->surveillance_score = best_score;
    a_case->surveillance_evidence = best_sources;
    a_case->surveillance_case_signature = best_signature;
    if( best_score >= threshold_score() )
    {
        a_case->surveillance = best_classification;
        a_case->surveillance_signature = best_signature;
    }
    else
    {
        a_case->surveillance = SURVEILLANCE_NONE;
        a_case->surveillance_signature = SURVEILLANCE_SIGNATURE_NONE;
    }
}

void surveillance_prepare_observation( rf_device_t *a_observation )
{
    surveillance_database_match_observation( a_observation );
}

void surveillance_case_observe( rf_device_t *a_case, const rf_device_t *a_observation )
{
    if( ( NULL == a_case ) || ( NULL == a_observation ) )
    {
        return;
    }

    for( size_t i = 0U; i < RFSCOUT_SURVEILLANCE_EVIDENCE_WORD_COUNT; ++i )
    {
        a_case->surveillance_evidence_bits[ i ] |= a_observation->surveillance_evidence_bits[ i ];
    }

    evaluate_case( a_case );
}

size_t surveillance_evidence_count( const rf_device_t *a_case )
{
    if( NULL == a_case )
    {
        return 0U;
    }

    size_t count = 0U;
    for( size_t i = 0U; i < RFSCOUT_SURVEILLANCE_EVIDENCE_WORD_COUNT; ++i )
    {
        count += (size_t)__builtin_popcount( a_case->surveillance_evidence_bits[ i ] );
    }
    return count;
}

size_t surveillance_signature_count( void )
{
    return surveillance_database_signature_count();
}

const char *surveillance_signature_label( surveillance_signature_id_t a_signature )
{
    return surveillance_database_signature_label( a_signature );
}

const char *surveillance_name( surveillance_class_t a_classification )
{
    switch( a_classification )
    {
        case SURVEILLANCE_ALPR:
            return "ALPR";
        case SURVEILLANCE_BODY_CAMERA:
            return "Body camera";
        case SURVEILLANCE_DRONE:
            return "Drone";
        case SURVEILLANCE_CAMERA:
            return "Camera / doorbell";
        case SURVEILLANCE_SMART_GLASSES:
            return "Smart glasses";
        case SURVEILLANCE_AUDIO:
            return "Acoustic sensor";
        case SURVEILLANCE_OTHER:
            return "Surveillance";
        default:
            return "None";
    }
}

char surveillance_morse_letter( surveillance_class_t a_classification )
{
    switch( a_classification )
    {
        case SURVEILLANCE_ALPR:
            return 'L';
        case SURVEILLANCE_BODY_CAMERA:
            return 'B';
        case SURVEILLANCE_DRONE:
            return 'D';
        case SURVEILLANCE_CAMERA:
            return 'C';
        case SURVEILLANCE_SMART_GLASSES:
            return 'G';
        case SURVEILLANCE_AUDIO:
            return 'A';
        case SURVEILLANCE_OTHER:
            return 'S';
        default:
            return '\0';
    }
}
