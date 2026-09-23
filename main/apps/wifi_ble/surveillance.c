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
            return 3U;
        case RFSCOUT_EVIDENCE_STRENGTH_PROBABLE:
            return 2U;
        case RFSCOUT_EVIDENCE_STRENGTH_POSSIBLE:
        default:
            return 1U;
    }
}

static bool case_has_evidence( const rf_device_t *a_case, uint16_t a_record_id )
{
    for( size_t i = 0U; i < a_case->surveillance_evidence_count; ++i )
    {
        if( a_record_id == a_case->surveillance_evidence_ids[ i ] )
        {
            return true;
        }
    }
    return false;
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

        for( size_t i = 0U; i < a_case->surveillance_evidence_count; ++i )
        {
            const surveillance_evidence_record_t *record =
                surveillance_database_record( a_case->surveillance_evidence_ids[ i ] );
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

    for( size_t i = 0U; i < a_observation->surveillance_evidence_count; ++i )
    {
        const uint16_t record_id = a_observation->surveillance_evidence_ids[ i ];
        if( case_has_evidence( a_case, record_id ) )
        {
            continue;
        }
        if( a_case->surveillance_evidence_count >= RFSCOUT_SURVEILLANCE_EVIDENCE_MAX )
        {
            break;
        }
        a_case->surveillance_evidence_ids[ a_case->surveillance_evidence_count++ ] = record_id;
    }

    evaluate_case( a_case );
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
