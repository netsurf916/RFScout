/**
    alert.c : RFScout audible alert policy implementation
    Description: Converts device-wide alert preferences into asynchronous speaker requests.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "alert.h"

#include "audio_output.h"
#include "morse.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include <ctype.h>

#define ALERT_TONE_FREQUENCY_HZ 900U
#define ALERT_TONE_DURATION_MS 250U
#define ALERT_MORSE_WPM 20U
#define ALERT_MORSE_UNIT_MS ( 1200U / ALERT_MORSE_WPM )
#define ALERT_MORSE_DEFAULT_CHARACTER 'S'

/**
 * Canonical alert-item registry.
 *
 * Keep this table in exactly the same order as rfscout_notification_type_t in alert.h. The Device
 * UI and rfscout.json persistence iterate this table, so labels/config keys/default characters do
 * not need parallel updates elsewhere. When classifier categories change, also update the producer
 * mappings in rfscout.c (WiFi/BT surveillance) and app_manager.c (CC1101/nRF24).
 */
static const rfscout_notification_descriptor_t notification_descriptors[] = {
    { RFSCOUT_NOTIFICATION_FLOCK, RFSCOUT_APP_WIFI_BT, "flock", "Flock / ALPR", 'F' },
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

_Static_assert( sizeof( notification_descriptors ) / sizeof( notification_descriptors[ 0 ] ) ==
                    RFSCOUT_NOTIFICATION_TYPE_COUNT,
                "notification descriptor table must match rfscout_notification_type_t" );

static rfscout_alert_mode_t mode = RFSCOUT_ALERT_OFF;
static uint8_t volume_percent = RFSCOUT_ALERT_VOLUME_DEFAULT_PERCENT;
static char morse_character = ALERT_MORSE_DEFAULT_CHARACTER;
static SemaphoreHandle_t alert_mutex;

/** @brief Queue an alert while the alert-policy mutex is already held. */
static bool request_locked( char a_character )
{
    switch( mode )
    {
        case RFSCOUT_ALERT_OFF:
            return true;

        case RFSCOUT_ALERT_TONE:
            return audio_output_play_tone(
                ALERT_TONE_FREQUENCY_HZ, ALERT_TONE_DURATION_MS, volume_percent );

        case RFSCOUT_ALERT_MORSE:
            if( !alert_morse_character_supported( a_character ) )
            {
                return false;
            }
            return audio_output_play_morse( (char)toupper( (unsigned char)a_character ),
                                            ALERT_TONE_FREQUENCY_HZ,
                                            ALERT_MORSE_UNIT_MS,
                                            volume_percent );

        default:
            return false;
    }
}

esp_err_t alert_init( void )
{
    if( NULL == alert_mutex )
    {
        alert_mutex = xSemaphoreCreateMutex();
        if( NULL == alert_mutex )
        {
            return ESP_ERR_NO_MEM;
        }
    }

    if( pdTRUE != xSemaphoreTake( alert_mutex, portMAX_DELAY ) )
    {
        return ESP_FAIL;
    }
    mode = RFSCOUT_ALERT_OFF;
    volume_percent = RFSCOUT_ALERT_VOLUME_DEFAULT_PERCENT;
    morse_character = ALERT_MORSE_DEFAULT_CHARACTER;
    (void)xSemaphoreGive( alert_mutex );
    return audio_output_init();
}

const char *alert_mode_name( rfscout_alert_mode_t a_mode )
{
    switch( a_mode )
    {
        case RFSCOUT_ALERT_TONE:
            return "TONE";
        case RFSCOUT_ALERT_MORSE:
            return "MORSE";
        case RFSCOUT_ALERT_OFF:
        default:
            return "OFF";
    }
}

const rfscout_notification_descriptor_t *
alert_notification_descriptor( rfscout_notification_type_t a_type )
{
    if( a_type >= RFSCOUT_NOTIFICATION_TYPE_COUNT )
    {
        return NULL;
    }
    const rfscout_notification_descriptor_t *descriptor = &notification_descriptors[ a_type ];
    return ( descriptor->type == a_type ) ? descriptor : NULL;
}

const char *alert_notification_name( rfscout_notification_type_t a_type )
{
    const rfscout_notification_descriptor_t *descriptor = alert_notification_descriptor( a_type );
    return ( NULL != descriptor ) ? descriptor->label : "Unknown";
}

const char *alert_notification_config_key( rfscout_notification_type_t a_type )
{
    const rfscout_notification_descriptor_t *descriptor = alert_notification_descriptor( a_type );
    return ( NULL != descriptor ) ? descriptor->config_key : NULL;
}

rfscout_app_id_t alert_notification_app( rfscout_notification_type_t a_type )
{
    const rfscout_notification_descriptor_t *descriptor = alert_notification_descriptor( a_type );
    return ( NULL != descriptor ) ? descriptor->app : RFSCOUT_APP_DEVICE;
}

char alert_notification_default_character( rfscout_notification_type_t a_type )
{
    const rfscout_notification_descriptor_t *descriptor = alert_notification_descriptor( a_type );
    return ( NULL != descriptor ) ? descriptor->default_character : ALERT_MORSE_DEFAULT_CHARACTER;
}

rfscout_alert_mode_t alert_mode( void )
{
    rfscout_alert_mode_t current = mode;
    if( ( NULL != alert_mutex ) && ( pdTRUE == xSemaphoreTake( alert_mutex, portMAX_DELAY ) ) )
    {
        current = mode;
        (void)xSemaphoreGive( alert_mutex );
    }
    return current;
}

void alert_set_mode( rfscout_alert_mode_t a_mode )
{
    if( ( a_mode >= RFSCOUT_ALERT_MODE_COUNT ) || ( NULL == alert_mutex ) )
    {
        return;
    }
    if( pdTRUE != xSemaphoreTake( alert_mutex, portMAX_DELAY ) )
    {
        return;
    }

    mode = a_mode;
    if( RFSCOUT_ALERT_OFF == mode )
    {
        audio_output_cancel_pending();
    }
    (void)xSemaphoreGive( alert_mutex );
}

uint8_t alert_volume_percent( void )
{
    uint8_t current = volume_percent;
    if( ( NULL != alert_mutex ) && ( pdTRUE == xSemaphoreTake( alert_mutex, portMAX_DELAY ) ) )
    {
        current = volume_percent;
        (void)xSemaphoreGive( alert_mutex );
    }
    return current;
}

bool alert_set_volume_percent( uint8_t a_percent )
{
    if( ( a_percent > RFSCOUT_ALERT_VOLUME_MAX_PERCENT ) || ( NULL == alert_mutex ) )
    {
        return false;
    }
    if( pdTRUE != xSemaphoreTake( alert_mutex, portMAX_DELAY ) )
    {
        return false;
    }
    volume_percent = a_percent;
    (void)xSemaphoreGive( alert_mutex );
    return true;
}

char alert_morse_character( void )
{
    char current = morse_character;
    if( ( NULL != alert_mutex ) && ( pdTRUE == xSemaphoreTake( alert_mutex, portMAX_DELAY ) ) )
    {
        current = morse_character;
        (void)xSemaphoreGive( alert_mutex );
    }
    return current;
}

bool alert_morse_character_supported( char a_character )
{
    return NULL != morse_pattern( a_character );
}

bool alert_set_morse_character( char a_character )
{
    if( !alert_morse_character_supported( a_character ) || ( NULL == alert_mutex ) )
    {
        return false;
    }
    if( pdTRUE != xSemaphoreTake( alert_mutex, portMAX_DELAY ) )
    {
        return false;
    }

    morse_character = (char)toupper( (unsigned char)a_character );
    (void)xSemaphoreGive( alert_mutex );
    return true;
}

bool alert_request( void )
{
    return alert_request_character( alert_morse_character() );
}

bool alert_request_character( char a_character )
{
    if( ( NULL == alert_mutex ) || ( pdTRUE != xSemaphoreTake( alert_mutex, 0U ) ) )
    {
        return false;
    }

    const bool queued = request_locked( a_character );
    (void)xSemaphoreGive( alert_mutex );
    return queued;
}
