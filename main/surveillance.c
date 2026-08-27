/**
    surveillance.c : RFScout surveillance classification
    Description: Correlates curated prefix metadata, BLE names, manufacturer IDs, services, and Remote ID.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "surveillance.h"

#include "oui_database.h"

#include <ctype.h>
#include <string.h>

#define SCORE_NAME 45U
#define SCORE_COMPANY 80U
#define SCORE_RAVEN 70U
#define SCORE_REMOTE_ID 100U
#define SCORE_ALERT_THRESHOLD 60U
#define FLOCK_COMPANY_ID 0x09C8U

typedef struct
{
    const char *fragment;
    surveillance_class_t classification;
    surveillance_signature_id_t signature;
    uint8_t score;
} name_signature_t;

typedef struct
{
    surveillance_class_t classification;
    uint8_t score;
    uint8_t evidence;
} candidate_t;

static const name_signature_t name_signatures[] = {
    { "AXON", SURVEILLANCE_BODY_CAMERA, SURVEILLANCE_SIGNATURE_AXON, SCORE_NAME },
    { "FLOCKCAM", SURVEILLANCE_FLOCK, SURVEILLANCE_SIGNATURE_FLOCK, SCORE_NAME },
    { "FS EXT BATTERY", SURVEILLANCE_FLOCK, SURVEILLANCE_SIGNATURE_FLOCK, SCORE_NAME },
    { "PENGUIN", SURVEILLANCE_FLOCK, SURVEILLANCE_SIGNATURE_FLOCK, SCORE_NAME },
    { "PIGVISION", SURVEILLANCE_FLOCK, SURVEILLANCE_SIGNATURE_FLOCK, SCORE_NAME },
    { "FS-", SURVEILLANCE_FLOCK, SURVEILLANCE_SIGNATURE_FLOCK, SCORE_NAME },
    { "RING", SURVEILLANCE_CAMERA, SURVEILLANCE_SIGNATURE_RING, SCORE_NAME },
    { "RAY-BAN", SURVEILLANCE_SMART_GLASSES, SURVEILLANCE_SIGNATURE_META, SCORE_NAME },
    { "RAYBAN", SURVEILLANCE_SMART_GLASSES, SURVEILLANCE_SIGNATURE_META, SCORE_NAME },
};

/** @brief Return true when a string contains a fragment without case sensitivity. */
static bool contains_ci( const char *a_value, const char *a_fragment )
{
    if( ( NULL == a_value ) || ( NULL == a_fragment ) )
    {
        return false;
    }

    const size_t fragment_length = strlen( a_fragment );
    for( ; '\0' != *a_value; ++a_value )
    {
        size_t i = 0;
        while( ( i < fragment_length ) && ( '\0' != a_value[ i ] ) &&
               ( toupper( (unsigned char)a_value[ i ] ) ==
                 toupper( (unsigned char)a_fragment[ i ] ) ) )
        {
            ++i;
        }

        if( i == fragment_length )
        {
            return true;
        }
    }

    return false;
}

/** @brief Return true when a normalized BLE observation advertises a service. */
static bool has_service( const rf_device_t *a_observation, uint16_t a_uuid )
{
    for( size_t i = 0; i < a_observation->service_uuid_count; ++i )
    {
        if( a_observation->service_uuids[ i ] == a_uuid )
        {
            return true;
        }
    }

    return false;
}

/**
 * @brief Add one independent evidence source to a candidate signature.
 * @details Each evidence type contributes at most once to a given candidate so
 *          repeated matching strings cannot inflate confidence artificially.
 */
static void add_evidence( candidate_t *a_candidates,
                          surveillance_class_t a_classification,
                          surveillance_signature_id_t a_signature,
                          uint8_t a_score,
                          uint8_t a_evidence )
{
    if( ( NULL == a_candidates ) || ( SURVEILLANCE_SIGNATURE_NONE == a_signature ) ||
        ( a_signature >= SURVEILLANCE_SIGNATURE_COUNT ) || ( 0U == a_score ) )
    {
        return;
    }

    candidate_t *candidate = &a_candidates[ a_signature ];
    candidate->classification = a_classification;

    if( 0U != ( candidate->evidence & a_evidence ) )
    {
        return;
    }

    candidate->evidence |= a_evidence;
    const unsigned combined_score = (unsigned)candidate->score + a_score;
    candidate->score = ( combined_score > 100U ) ? 100U : (uint8_t)combined_score;
}

/** @brief Select the highest-confidence candidate that crosses the alert threshold. */
static surveillance_match_t select_best_candidate( const candidate_t *a_candidates )
{
    surveillance_match_t best = { 0 };

    for( size_t signature = (size_t)SURVEILLANCE_SIGNATURE_FLOCK;
         signature < (size_t)SURVEILLANCE_SIGNATURE_COUNT;
         ++signature )
    {
        const candidate_t *candidate = &a_candidates[ signature ];
        if( ( candidate->score < SCORE_ALERT_THRESHOLD ) || ( candidate->score <= best.score ) )
        {
            continue;
        }

        best.classification = candidate->classification;
        best.signature = (surveillance_signature_id_t)signature;
        best.score = candidate->score;
        best.evidence = candidate->evidence;
    }

    return best;
}

surveillance_match_t surveillance_match( const rf_device_t *a_observation )
{
    candidate_t candidates[ SURVEILLANCE_SIGNATURE_COUNT ] = { 0 };

    if( NULL == a_observation )
    {
        surveillance_match_t empty = { 0 };
        return empty;
    }

    /* Prefix-derived surveillance metadata is owned by oui_database. That lookup intentionally
       evaluates curated prefixes even for locally administered addresses because the curated
       dataset contains field-verified LAA evidence that is not an IEEE vendor assignment. */
    oui_database_match_t identity;
    if( oui_database_lookup_info( a_observation->address, &identity ) &&
        ( SURVEILLANCE_SIGNATURE_NONE != identity.surveillance_signature ) )
    {
        add_evidence( candidates,
                      identity.surveillance_classification,
                      identity.surveillance_signature,
                      identity.surveillance_score,
                      SURVEILLANCE_EVIDENCE_OUI );
    }

    if( a_observation->has_name )
    {
        for( size_t i = 0; i < sizeof( name_signatures ) / sizeof( name_signatures[ 0 ] ); ++i )
        {
            if( !contains_ci( a_observation->name, name_signatures[ i ].fragment ) )
            {
                continue;
            }

            add_evidence( candidates,
                          name_signatures[ i ].classification,
                          name_signatures[ i ].signature,
                          name_signatures[ i ].score,
                          SURVEILLANCE_EVIDENCE_NAME );
        }
    }

    if( FLOCK_COMPANY_ID == a_observation->company_id )
    {
        add_evidence( candidates,
                      SURVEILLANCE_FLOCK,
                      SURVEILLANCE_SIGNATURE_FLOCK,
                      SCORE_COMPANY,
                      SURVEILLANCE_EVIDENCE_COMPANY_ID );
    }

    /* Raven Acoustic Monitoring advertises a distinctive family of adjacent
       BLE services. Requiring two services avoids treating one generic UUID as
       sufficient evidence. */
    unsigned raven_services = 0;
    for( uint16_t uuid = 0x3100U; uuid <= 0x3500U; uuid += 0x0100U )
    {
        if( has_service( a_observation, uuid ) )
        {
            ++raven_services;
        }
    }

    if( raven_services >= 2U )
    {
        add_evidence( candidates,
                      SURVEILLANCE_AUDIO,
                      SURVEILLANCE_SIGNATURE_RAVEN,
                      SCORE_RAVEN,
                      SURVEILLANCE_EVIDENCE_SERVICE_UUID );
    }

    if( a_observation->has_remote_id )
    {
        add_evidence( candidates,
                      SURVEILLANCE_DRONE,
                      SURVEILLANCE_SIGNATURE_REMOTE_ID,
                      SCORE_REMOTE_ID,
                      SURVEILLANCE_EVIDENCE_REMOTE_ID );
    }

    return select_best_candidate( candidates );
}

surveillance_class_t surveillance_classify( const rf_device_t *a_observation )
{
    return surveillance_match( a_observation ).classification;
}

const char *surveillance_signature_label( surveillance_signature_id_t a_signature )
{
    switch( a_signature )
    {
        case SURVEILLANCE_SIGNATURE_FLOCK:
            return "ALPR/Flock";
        case SURVEILLANCE_SIGNATURE_VIGILANT:
            return "ALPR/Vigilant";
        case SURVEILLANCE_SIGNATURE_RAVEN:
            return "AUDIO/Raven";
        case SURVEILLANCE_SIGNATURE_AXON:
            return "BODY/Axon";
        case SURVEILLANCE_SIGNATURE_RING:
            return "CAM/Ring";
        case SURVEILLANCE_SIGNATURE_VERKADA:
            return "CAM/Verkada";
        case SURVEILLANCE_SIGNATURE_AXIS:
            return "CAM/Axis";
        case SURVEILLANCE_SIGNATURE_HIKVISION:
            return "CAM/Hikvision";
        case SURVEILLANCE_SIGNATURE_DAHUA:
            return "CAM/Dahua";
        case SURVEILLANCE_SIGNATURE_REMOTE_ID:
            return "DRONE/RemoteID";
        case SURVEILLANCE_SIGNATURE_META:
            return "GLASS/Meta";
        default:
            return "OTHER";
    }
}

const char *surveillance_name( surveillance_class_t a_classification )
{
    switch( a_classification )
    {
        case SURVEILLANCE_FLOCK:
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
        case SURVEILLANCE_FLOCK:
            return 'F';
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
