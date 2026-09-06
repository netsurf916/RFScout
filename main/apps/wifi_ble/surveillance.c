/**
    surveillance.c : RFScout surveillance classification
    Description: Matches curated prefix, BLE name/company/service fingerprints, and Remote ID evidence.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "surveillance.h"

#include "device_config.h"
#include "oui_database.h"

#include <ctype.h>
#include <string.h>

#define FLOCK_COMPANY_ID 0x09C8U
#define EVIDENCE_PRIORITY_NAME 1U
#define EVIDENCE_PRIORITY_OUI 3U
#define EVIDENCE_PRIORITY_DISTINCTIVE 4U
#define EVIDENCE_PRIORITY_REMOTE_ID 5U

typedef struct
{
    const char *fragment;
    surveillance_class_t classification;
    surveillance_signature_id_t signature;
    bool prefix_only;
} name_signature_t;

typedef struct
{
    surveillance_class_t classification;
    uint8_t evidence;
    uint8_t priority;
    bool matched;
} candidate_t;

static const name_signature_t name_signatures[] = {
    { "AXON", SURVEILLANCE_BODY_CAMERA, SURVEILLANCE_SIGNATURE_AXON, false },
    { "FLOCKCAM", SURVEILLANCE_ALPR, SURVEILLANCE_SIGNATURE_FLOCK, false },
    { "FS EXT BATTERY", SURVEILLANCE_ALPR, SURVEILLANCE_SIGNATURE_FLOCK, false },
    { "PENGUIN", SURVEILLANCE_ALPR, SURVEILLANCE_SIGNATURE_FLOCK, false },
    { "PIGVISION", SURVEILLANCE_ALPR, SURVEILLANCE_SIGNATURE_FLOCK, false },
    { "FS-", SURVEILLANCE_ALPR, SURVEILLANCE_SIGNATURE_FLOCK, true },
    { "RING", SURVEILLANCE_CAMERA, SURVEILLANCE_SIGNATURE_RING, false },
    { "RAY-BAN", SURVEILLANCE_SMART_GLASSES, SURVEILLANCE_SIGNATURE_META, false },
    { "RAYBAN", SURVEILLANCE_SMART_GLASSES, SURVEILLANCE_SIGNATURE_META, false },
    { "OAKLEY META", SURVEILLANCE_SMART_GLASSES, SURVEILLANCE_SIGNATURE_META, false },
    { "G1_", SURVEILLANCE_SMART_GLASSES, SURVEILLANCE_SIGNATURE_EVEN_G1, true },
    { "AIRGO", SURVEILLANCE_SMART_GLASSES, SURVEILLANCE_SIGNATURE_SOLOS_AIRGO, false },
};

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

static void add_evidence( candidate_t *a_candidates,
                          surveillance_class_t a_classification,
                          surveillance_signature_id_t a_signature,
                          uint8_t a_evidence,
                          uint8_t a_priority )
{
    if( ( NULL == a_candidates ) || ( SURVEILLANCE_SIGNATURE_NONE == a_signature ) ||
        ( a_signature >= SURVEILLANCE_SIGNATURE_COUNT ) )
    {
        return;
    }

    candidate_t *candidate = &a_candidates[ a_signature ];
    candidate->classification = a_classification;
    candidate->evidence |= a_evidence;
    candidate->matched = true;
    if( a_priority > candidate->priority )
    {
        candidate->priority = a_priority;
    }
}

static uint8_t minimum_evidence_priority( void )
{
    switch( device_config_surveillance_sensitivity() )
    {
        case RFSCOUT_SURVEILLANCE_SENSITIVITY_LOW:
            return EVIDENCE_PRIORITY_DISTINCTIVE;
        case RFSCOUT_SURVEILLANCE_SENSITIVITY_MEDIUM:
            return EVIDENCE_PRIORITY_OUI;
        case RFSCOUT_SURVEILLANCE_SENSITIVITY_HIGH:
        default:
            return EVIDENCE_PRIORITY_NAME;
    }
}

static surveillance_match_t select_best_candidate( const candidate_t *a_candidates )
{
    surveillance_match_t best = { 0 };
    uint8_t best_priority = 0U;
    const uint8_t minimum_priority = minimum_evidence_priority();

    for( size_t signature = (size_t)SURVEILLANCE_SIGNATURE_FLOCK;
         signature < (size_t)SURVEILLANCE_SIGNATURE_COUNT;
         ++signature )
    {
        const candidate_t *candidate = &a_candidates[ signature ];
        if( !candidate->matched || ( candidate->priority < minimum_priority ) ||
            ( candidate->priority <= best_priority ) )
        {
            continue;
        }
        best.classification = candidate->classification;
        best.signature = (surveillance_signature_id_t)signature;
        best.evidence = candidate->evidence;
        best_priority = candidate->priority;
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

    oui_database_match_t identity;
    if( oui_database_lookup_info( a_observation->address, &identity ) &&
        ( SURVEILLANCE_SIGNATURE_NONE != identity.surveillance_signature ) )
    {
        add_evidence( candidates,
                      identity.surveillance_classification,
                      identity.surveillance_signature,
                      SURVEILLANCE_EVIDENCE_OUI,
                      EVIDENCE_PRIORITY_OUI );
    }

    if( a_observation->has_name )
    {
        for( size_t i = 0; i < sizeof( name_signatures ) / sizeof( name_signatures[ 0 ] ); ++i )
        {
            const bool matched =
                name_signatures[ i ].prefix_only
                    ? starts_with_ci( a_observation->name, name_signatures[ i ].fragment )
                    : contains_ci( a_observation->name, name_signatures[ i ].fragment );
            if( matched )
            {
                add_evidence( candidates,
                              name_signatures[ i ].classification,
                              name_signatures[ i ].signature,
                              SURVEILLANCE_EVIDENCE_NAME,
                              EVIDENCE_PRIORITY_NAME );
            }
        }
    }

    if( FLOCK_COMPANY_ID == a_observation->company_id )
    {
        add_evidence( candidates,
                      SURVEILLANCE_ALPR,
                      SURVEILLANCE_SIGNATURE_FLOCK,
                      SURVEILLANCE_EVIDENCE_COMPANY_ID,
                      EVIDENCE_PRIORITY_DISTINCTIVE );
    }

    if( 0U != ( a_observation->ble_service_fingerprints &
                RFSCOUT_BLE_SERVICE_FINGERPRINT_BRILLIANT_FRAME ) )
    {
        add_evidence( candidates,
                      SURVEILLANCE_SMART_GLASSES,
                      SURVEILLANCE_SIGNATURE_BRILLIANT_FRAME,
                      SURVEILLANCE_EVIDENCE_SERVICE_UUID,
                      EVIDENCE_PRIORITY_DISTINCTIVE );
    }
    if( 0U !=
        ( a_observation->ble_service_fingerprints & RFSCOUT_BLE_SERVICE_FINGERPRINT_ROKID_CXR ) )
    {
        add_evidence( candidates,
                      SURVEILLANCE_SMART_GLASSES,
                      SURVEILLANCE_SIGNATURE_ROKID,
                      SURVEILLANCE_EVIDENCE_SERVICE_UUID,
                      EVIDENCE_PRIORITY_DISTINCTIVE );
    }

    unsigned raven_services = 0U;
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
                      SURVEILLANCE_EVIDENCE_SERVICE_UUID,
                      EVIDENCE_PRIORITY_DISTINCTIVE );
    }

    if( a_observation->has_remote_id )
    {
        add_evidence( candidates,
                      SURVEILLANCE_DRONE,
                      SURVEILLANCE_SIGNATURE_REMOTE_ID,
                      SURVEILLANCE_EVIDENCE_REMOTE_ID,
                      EVIDENCE_PRIORITY_REMOTE_ID );
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
        case SURVEILLANCE_SIGNATURE_BRILLIANT_FRAME:
            return "GLASS/Frame";
        case SURVEILLANCE_SIGNATURE_EVEN_G1:
            return "GLASS/Even G1";
        case SURVEILLANCE_SIGNATURE_SOLOS_AIRGO:
            return "GLASS/Solos";
        case SURVEILLANCE_SIGNATURE_ROKID:
            return "GLASS/Rokid";
        default:
            return "OTHER";
    }
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
