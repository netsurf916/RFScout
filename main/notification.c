/**
    notification.c : RFScout semantic notification catalog
    Description: Owns notification vocabulary and application ownership without depending on speaker policy.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "notification.h"

#include "event.h"

#include <stddef.h>

#define NOTIFICATION_DEFAULT_CHARACTER 'S'

static const rfscout_notification_descriptor_t descriptors[] = {
    { RFSCOUT_NOTIFICATION_ALPR, RFSCOUT_APP_WIFI_BT, "alpr", "ALPR", 'L' },
    { RFSCOUT_NOTIFICATION_BODY_CAMERA, RFSCOUT_APP_WIFI_BT, "body_camera", "Body camera", 'B' },
    { RFSCOUT_NOTIFICATION_DRONE, RFSCOUT_APP_WIFI_BT, "drone", "Drone", 'D' },
    { RFSCOUT_NOTIFICATION_CAMERA, RFSCOUT_APP_WIFI_BT, "camera", "Camera", 'C' },
    { RFSCOUT_NOTIFICATION_SMART_GLASSES,
      RFSCOUT_APP_WIFI_BT,
      "smart_glasses",
      "Smart glasses",
      'G' },
    { RFSCOUT_NOTIFICATION_ACOUSTIC_SENSOR,
      RFSCOUT_APP_WIFI_BT,
      "acoustic_sensor",
      "Acoustic sensor",
      'A' },
    { RFSCOUT_NOTIFICATION_SURVEILLANCE_OTHER,
      RFSCOUT_APP_WIFI_BT,
      "surveillance_other",
      "Other surveillance",
      'S' },

    { RFSCOUT_NOTIFICATION_CC1101_NARROW_ISM,
      RFSCOUT_APP_CC1101,
      "cc1101_narrow_ism",
      "CC1101 narrowband",
      'N' },
    { RFSCOUT_NOTIFICATION_CC1101_REMOTE_SENSOR,
      RFSCOUT_APP_CC1101,
      "cc1101_remote_sensor",
      "CC1101 remote/sensor",
      'R' },
    { RFSCOUT_NOTIFICATION_CC1101_CARRIER,
      RFSCOUT_APP_CC1101,
      "cc1101_carrier",
      "CC1101 carrier",
      'K' },
    { RFSCOUT_NOTIFICATION_CC1101_BURSTY,
      RFSCOUT_APP_CC1101,
      "cc1101_bursty",
      "CC1101 bursty RF",
      'U' },
    { RFSCOUT_NOTIFICATION_CC1101_WIDEBAND,
      RFSCOUT_APP_CC1101,
      "cc1101_wideband",
      "CC1101 wide RF",
      'W' },

    { RFSCOUT_NOTIFICATION_NRF24_ESB, RFSCOUT_APP_NRF24, "nrf24_esb", "nRF24 ESB device", 'E' },
    { RFSCOUT_NOTIFICATION_NRF24_INPUT, RFSCOUT_APP_NRF24, "nrf24_input", "nRF24 input", 'I' },
    { RFSCOUT_NOTIFICATION_NRF24_SENSOR, RFSCOUT_APP_NRF24, "nrf24_sensor", "nRF24 sensor", 'T' },
    { RFSCOUT_NOTIFICATION_NRF24_WIFI_LIKE,
      RFSCOUT_APP_NRF24,
      "nrf24_wifi_like",
      "nRF24 WiFi-like",
      'V' },
    { RFSCOUT_NOTIFICATION_NRF24_BLE_LIKE,
      RFSCOUT_APP_NRF24,
      "nrf24_ble_like",
      "nRF24 hopping RF",
      'L' },
    { RFSCOUT_NOTIFICATION_NRF24_LOGITECH_LIKE,
      RFSCOUT_APP_NRF24,
      "nrf24_logitech_like",
      "nRF24 Logitech",
      'X' },
};

_Static_assert( sizeof( descriptors ) / sizeof( descriptors[ 0 ] ) ==
                    RFSCOUT_NOTIFICATION_TYPE_COUNT,
                "notification descriptor table must match rfscout_notification_type_t" );

const rfscout_notification_descriptor_t *
notification_descriptor( rfscout_notification_type_t a_type )
{
    if( a_type >= RFSCOUT_NOTIFICATION_TYPE_COUNT )
    {
        return NULL;
    }
    const rfscout_notification_descriptor_t *descriptor = &descriptors[ a_type ];
    return ( descriptor->type == a_type ) ? descriptor : NULL;
}

const char *notification_name( rfscout_notification_type_t a_type )
{
    const rfscout_notification_descriptor_t *descriptor = notification_descriptor( a_type );
    return ( NULL != descriptor ) ? descriptor->label : "Unknown";
}

const char *notification_config_key( rfscout_notification_type_t a_type )
{
    const rfscout_notification_descriptor_t *descriptor = notification_descriptor( a_type );
    return ( NULL != descriptor ) ? descriptor->config_key : NULL;
}

rfscout_app_id_t notification_app( rfscout_notification_type_t a_type )
{
    const rfscout_notification_descriptor_t *descriptor = notification_descriptor( a_type );
    return ( NULL != descriptor ) ? descriptor->app : RFSCOUT_APP_DEVICE;
}

char notification_default_character( rfscout_notification_type_t a_type )
{
    const rfscout_notification_descriptor_t *descriptor = notification_descriptor( a_type );
    return ( NULL != descriptor ) ? descriptor->default_character : NOTIFICATION_DEFAULT_CHARACTER;
}

size_t notification_count_for_app( rfscout_app_id_t a_app )
{
    size_t count = 0U;
    for( size_t index = 0U; index < RFSCOUT_NOTIFICATION_TYPE_COUNT; ++index )
    {
        if( descriptors[ index ].app == a_app )
        {
            ++count;
        }
    }
    return count;
}

const rfscout_notification_descriptor_t *notification_descriptor_for_app( rfscout_app_id_t a_app,
                                                                          size_t a_index )
{
    size_t match = 0U;
    for( size_t index = 0U; index < RFSCOUT_NOTIFICATION_TYPE_COUNT; ++index )
    {
        if( descriptors[ index ].app != a_app )
        {
            continue;
        }
        if( match == a_index )
        {
            return &descriptors[ index ];
        }
        ++match;
    }
    return NULL;
}

bool notification_emit( rfscout_notification_type_t a_type )
{
    const rfscout_notification_descriptor_t *descriptor = notification_descriptor( a_type );
    if( NULL == descriptor )
    {
        return false;
    }

    rfscout_event_emit( RFSCOUT_EVENT_NOTIFICATION, (uint8_t)descriptor->app, (int32_t)a_type );
    return true;
}
