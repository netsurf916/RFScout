/**
    device_config.c : RFScout persistent device configuration implementation
    Description: Loads validated owned records over compiled defaults and persists current preferences.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "device_config.h"

#include "event.h"

#include "app_audio_policy.h"
#include "notification.h"
#include "persistent_store.h"
#include "ui_theme.h"

#include "cJSON.h"
#include "esp_log.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#define APP_CONFIG_RECORD_VERSION 1U
#define CONFIG_BRIGHTNESS_DEFAULT 70U
#define CONFIG_DISPLAY_DIM_TIMEOUT_DEFAULT_SECONDS 120U
#define CONFIG_DISPLAY_OFF_TIMEOUT_DEFAULT_SECONDS 300U
#define CONFIG_DISPLAY_TIMEOUT_MAX_SECONDS 3600U
#define CONFIG_AUDIO_INTERACTION_GAIN_DEFAULT_PERCENT 20U
#define CONFIG_AUDIO_NOTIFICATION_GAIN_DEFAULT_PERCENT 20U
#define CONFIG_RECORDER_VOLUME_DEFAULT 50U

static const char *TAG = "device_config";
static device_config_t config;
static bool config_dirty;

static const char *app_config_name( rfscout_app_id_t a_app )
{
    static const char *names[ RFSCOUT_APP_COUNT ] = {
        [RFSCOUT_APP_SYSTEM_EVENTS] = "system_events",
        [RFSCOUT_APP_DEVICE] = "device",
        [RFSCOUT_APP_WIFI_BT] = "wifi_bt",
        [RFSCOUT_APP_CC1101] = "cc1101",
        [RFSCOUT_APP_NRF24] = "nrf24",
        [RFSCOUT_APP_AUDIO] = "audio",
        [RFSCOUT_APP_THERMAL] = "thermal",
        [RFSCOUT_APP_AUDIO_SPECTRUM] = "audio_spectrum",
    };
    return ( a_app < RFSCOUT_APP_COUNT ) ? names[ a_app ] : "device";
}

static bool app_from_config_name( const char *a_name, rfscout_app_id_t *a_app )
{
    if( NULL == a_name || NULL == a_app )
    {
        return false;
    }
    for( size_t index = 0U; index < RFSCOUT_APP_COUNT; ++index )
    {
        if( 0 == strcasecmp( a_name, app_config_name( (rfscout_app_id_t)index ) ) )
        {
            *a_app = (rfscout_app_id_t)index;
            return true;
        }
    }
    if( 0 == strcasecmp( a_name, "home" ) )
    {
        *a_app = RFSCOUT_APP_DEVICE;
        return true;
    }
    if( 0 == strcasecmp( a_name, "recorder" ) )
    {
        *a_app = RFSCOUT_APP_AUDIO;
        return true;
    }
    return false;
}

static const char *evidence_threshold_config_name( rfscout_evidence_strength_t a_threshold )
{
    switch( a_threshold )
    {
        case RFSCOUT_EVIDENCE_STRENGTH_PROBABLE:
            return "probable";
        case RFSCOUT_EVIDENCE_STRENGTH_DISTINCTIVE:
            return "distinctive";
        case RFSCOUT_EVIDENCE_STRENGTH_POSSIBLE:
        default:
            return "possible";
    }
}

static bool evidence_threshold_from_config_name( const char *a_name,
                                                 rfscout_evidence_strength_t *a_threshold )
{
    if( NULL == a_name || NULL == a_threshold )
    {
        return false;
    }
    if( 0 == strcasecmp( a_name, "possible" ) )
    {
        *a_threshold = RFSCOUT_EVIDENCE_STRENGTH_POSSIBLE;
        return true;
    }
    if( 0 == strcasecmp( a_name, "probable" ) )
    {
        *a_threshold = RFSCOUT_EVIDENCE_STRENGTH_PROBABLE;
        return true;
    }
    if( 0 == strcasecmp( a_name, "distinctive" ) )
    {
        *a_threshold = RFSCOUT_EVIDENCE_STRENGTH_DISTINCTIVE;
        return true;
    }
    return false;
}

static const char *power_action_config_name( power_action_t a_action )
{
    switch( a_action )
    {
        case POWER_ACTION_NONE:
            return "none";
        case POWER_ACTION_SCREEN_OFF:
            return "screen_off";
        case POWER_ACTION_SLEEP:
            return "sleep";
        case POWER_ACTION_POWER_OFF:
            return "power_off";
        case POWER_ACTION_COUNT:
        default:
            return "none";
    }
}

static bool power_action_from_config_name( const char *a_name, power_action_t *a_action )
{
    if( NULL == a_name || NULL == a_action )
    {
        return false;
    }
    for( int action = (int)POWER_ACTION_NONE; action < (int)POWER_ACTION_COUNT; ++action )
    {
        if( 0 == strcasecmp( a_name, power_action_config_name( (power_action_t)action ) ) )
        {
            *a_action = (power_action_t)action;
            return true;
        }
    }
    return false;
}

static bool power_action_valid_for_gesture( power_action_t a_action, bool a_long_press )
{
    if( a_long_press )
    {
        return POWER_ACTION_NONE == a_action || POWER_ACTION_POWER_OFF == a_action;
    }
    return POWER_ACTION_NONE == a_action || POWER_ACTION_SCREEN_OFF == a_action ||
           POWER_ACTION_SLEEP == a_action || POWER_ACTION_POWER_OFF == a_action;
}

static bool power_action_valid_for_policy( power_action_t a_action )
{
    return POWER_ACTION_NONE == a_action || POWER_ACTION_SLEEP == a_action ||
           POWER_ACTION_POWER_OFF == a_action;
}

static void copy_string( char *a_destination, size_t a_capacity, const char *a_source )
{
    if( NULL == a_destination || 0U == a_capacity )
    {
        return;
    }
    snprintf( a_destination, a_capacity, "%s", NULL != a_source ? a_source : "" );
}

static void
timezone_default( size_t a_index, const char *a_name, const char *a_label, const char *a_posix )
{
    device_timezone_t *zone = &config.timezones[ a_index ];
    copy_string( zone->name, sizeof( zone->name ), a_name );
    copy_string( zone->label, sizeof( zone->label ), a_label );
    copy_string( zone->posix, sizeof( zone->posix ), a_posix );
}

static void set_defaults( void )
{
    memset( &config, 0, sizeof( config ) );
    config.brightness_percent = CONFIG_BRIGHTNESS_DEFAULT;
    config.display_dim_timeout_seconds = CONFIG_DISPLAY_DIM_TIMEOUT_DEFAULT_SECONDS;
    config.display_off_timeout_seconds = CONFIG_DISPLAY_OFF_TIMEOUT_DEFAULT_SECONDS;
    config.power_button_short_action = POWER_ACTION_SLEEP;
    config.power_button_long_action = POWER_ACTION_POWER_OFF;
    config.low_battery_threshold_percent = 10U;
    config.low_battery_action = POWER_ACTION_POWER_OFF;
    config.high_temperature_threshold_c = 90U;
    config.high_temperature_action = POWER_ACTION_POWER_OFF;
    config.startup_app = RFSCOUT_APP_DEVICE;
    config.logging_enabled = true;
    config.clock_24h = false;
    config.theme = RFSCOUT_THEME_INDOOR;
    config.audio_policy = RFSCOUT_AUDIO_POLICY_NORMAL;
    config.audio_interaction_gain_percent = CONFIG_AUDIO_INTERACTION_GAIN_DEFAULT_PERCENT;
    config.audio_notification_gain_percent = CONFIG_AUDIO_NOTIFICATION_GAIN_DEFAULT_PERCENT;
    config.alert_mode = RFSCOUT_ALERT_OFF;
    config.alert_volume_percent = RFSCOUT_ALERT_VOLUME_DEFAULT_PERCENT;
    config.recorder_volume_percent = CONFIG_RECORDER_VOLUME_DEFAULT;
    config.wifi_device_feedback_enabled = true;
    config.ble_device_feedback_enabled = true;
    config.wifi_timeout_seconds = RFSCOUT_OBSERVATION_TIMEOUT_DEFAULT_SECONDS;
    config.ble_timeout_seconds = RFSCOUT_OBSERVATION_TIMEOUT_DEFAULT_SECONDS;
    config.evidence_threshold = RFSCOUT_EVIDENCE_STRENGTH_POSSIBLE;

    for( size_t index = 0U; index < RFSCOUT_APP_COUNT; ++index )
    {
        config.apps_enabled[ index ] = true;
        const rfscout_app_audio_policy_t *policy = app_audio_policy_get( (rfscout_app_id_t)index );
        config.notification_app_enabled[ index ] =
            policy->notifications_supported && policy->notifications_enabled_default;
    }
    config.apps_enabled[ RFSCOUT_APP_SYSTEM_EVENTS ] = false;
    config.apps_enabled[ RFSCOUT_APP_DEVICE ] = true;

    for( size_t index = 0U; index < RFSCOUT_NOTIFICATION_TYPE_COUNT; ++index )
    {
        config.notification_characters[ index ] =
            alert_notification_default_character( (rfscout_notification_type_t)index );
    }

    config.timezone_count = 2U;
    timezone_default( 0U, "America/Chicago", "US Central", "CST6CDT,M3.2.0,M11.1.0" );
    timezone_default( 1U, "UTC", "UTC", "UTC0" );
    copy_string( config.timezone_selected, sizeof( config.timezone_selected ), "America/Chicago" );
    config_dirty = false;
}

static bool json_bool( const cJSON *a_parent, const char *a_name, bool a_default )
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive( a_parent, a_name );
    return cJSON_IsBool( item ) ? cJSON_IsTrue( item ) : a_default;
}

static bool timeout_seconds_valid( uint16_t a_seconds )
{
    return a_seconds >= RFSCOUT_OBSERVATION_TIMEOUT_MIN_SECONDS &&
           a_seconds <= RFSCOUT_OBSERVATION_TIMEOUT_MAX_SECONDS &&
           0U == ( a_seconds % RFSCOUT_OBSERVATION_TIMEOUT_STEP_SECONDS );
}

static uint16_t
json_timeout_seconds( const cJSON *a_parent, const char *a_name, uint16_t a_default )
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive( a_parent, a_name );
    if( !cJSON_IsNumber( item ) || item->valueint < (int)RFSCOUT_OBSERVATION_TIMEOUT_MIN_SECONDS ||
        item->valueint > (int)RFSCOUT_OBSERVATION_TIMEOUT_MAX_SECONDS ||
        0 != ( item->valueint % (int)RFSCOUT_OBSERVATION_TIMEOUT_STEP_SECONDS ) )
    {
        return a_default;
    }
    return (uint16_t)item->valueint;
}

static uint16_t
json_display_timeout_seconds( const cJSON *a_parent, const char *a_name, uint16_t a_default )
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive( a_parent, a_name );
    if( !cJSON_IsNumber( item ) || item->valueint < 0 ||
        item->valueint > (int)CONFIG_DISPLAY_TIMEOUT_MAX_SECONDS )
    {
        return a_default;
    }
    return (uint16_t)item->valueint;
}

static void load_display( const cJSON *a_root )
{
    const cJSON *display = cJSON_GetObjectItemCaseSensitive( a_root, "display" );
    if( !cJSON_IsObject( display ) )
    {
        return;
    }

    const cJSON *brightness = cJSON_GetObjectItemCaseSensitive( display, "brightness" );
    if( cJSON_IsNumber( brightness ) && brightness->valueint >= 10 && brightness->valueint <= 100 )
    {
        config.brightness_percent = (uint8_t)brightness->valueint;
    }
    config.display_dim_timeout_seconds = json_display_timeout_seconds(
        display, "dim_timeout_seconds", config.display_dim_timeout_seconds );
    config.display_off_timeout_seconds = json_display_timeout_seconds(
        display, "off_timeout_seconds", config.display_off_timeout_seconds );
    if( 0U != config.display_dim_timeout_seconds && 0U != config.display_off_timeout_seconds &&
        config.display_off_timeout_seconds <= config.display_dim_timeout_seconds )
    {
        config.display_off_timeout_seconds = 0U;
    }
    config.clock_24h = json_bool( display, "clock_24h", config.clock_24h );

    const cJSON *theme = cJSON_GetObjectItemCaseSensitive( display, "theme" );
    if( cJSON_IsString( theme ) && NULL != theme->valuestring )
    {
        rfscout_theme_id_t parsed;
        if( ui_theme_from_config_name( theme->valuestring, &parsed ) )
        {
            config.theme = parsed;
        }
    }
}

static void load_audio_policy( const cJSON *a_root )
{
    const cJSON *audio = cJSON_GetObjectItemCaseSensitive( a_root, "audio" );
    if( !cJSON_IsObject( audio ) )
    {
        return;
    }

    const cJSON *policy = cJSON_GetObjectItemCaseSensitive( audio, "policy" );
    if( cJSON_IsString( policy ) && NULL != policy->valuestring )
    {
        rfscout_audio_policy_id_t parsed;
        if( audio_policy_from_config_name( policy->valuestring, &parsed ) )
        {
            config.audio_policy = parsed;
        }
    }

    const cJSON *interaction =
        cJSON_GetObjectItemCaseSensitive( audio, "interaction_gain_percent" );
    if( cJSON_IsNumber( interaction ) && interaction->valueint >= 0 &&
        interaction->valueint <= 100 )
    {
        config.audio_interaction_gain_percent = (uint8_t)interaction->valueint;
    }
    const cJSON *notification =
        cJSON_GetObjectItemCaseSensitive( audio, "notification_gain_percent" );
    if( cJSON_IsNumber( notification ) && notification->valueint >= 0 &&
        notification->valueint <= 100 )
    {
        config.audio_notification_gain_percent = (uint8_t)notification->valueint;
    }
}

static void load_power( const cJSON *a_root )
{
    const cJSON *power = cJSON_GetObjectItemCaseSensitive( a_root, "power" );
    if( !cJSON_IsObject( power ) )
    {
        return;
    }

    power_action_t parsed;
    const cJSON *short_press = cJSON_GetObjectItemCaseSensitive( power, "short_press" );
    if( cJSON_IsString( short_press ) && NULL != short_press->valuestring &&
        power_action_from_config_name( short_press->valuestring, &parsed ) &&
        power_action_valid_for_gesture( parsed, false ) )
    {
        config.power_button_short_action = parsed;
    }
    const cJSON *long_press = cJSON_GetObjectItemCaseSensitive( power, "long_press" );
    if( cJSON_IsString( long_press ) && NULL != long_press->valuestring &&
        power_action_from_config_name( long_press->valuestring, &parsed ) &&
        power_action_valid_for_gesture( parsed, true ) )
    {
        config.power_button_long_action = parsed;
    }

    const cJSON *low_battery = cJSON_GetObjectItemCaseSensitive( power, "low_battery_percent" );
    if( cJSON_IsNumber( low_battery ) && low_battery->valueint >= 0 &&
        low_battery->valueint <= (int)RFSCOUT_LOW_BATTERY_THRESHOLD_MAX_PERCENT &&
        ( 0 == low_battery->valueint || 0 == ( low_battery->valueint % 5 ) ) )
    {
        config.low_battery_threshold_percent = (uint8_t)low_battery->valueint;
    }
    const cJSON *low_action = cJSON_GetObjectItemCaseSensitive( power, "low_battery_action" );
    if( cJSON_IsString( low_action ) && NULL != low_action->valuestring &&
        power_action_from_config_name( low_action->valuestring, &parsed ) &&
        power_action_valid_for_policy( parsed ) )
    {
        config.low_battery_action = parsed;
    }

    const cJSON *high_temperature = cJSON_GetObjectItemCaseSensitive( power, "high_temperature_c" );
    if( cJSON_IsNumber( high_temperature ) &&
        ( 0 == high_temperature->valueint ||
          ( high_temperature->valueint >= (int)RFSCOUT_HIGH_TEMPERATURE_THRESHOLD_MIN_C &&
            high_temperature->valueint <= (int)RFSCOUT_HIGH_TEMPERATURE_THRESHOLD_MAX_C &&
            0 == ( high_temperature->valueint %
                   (int)RFSCOUT_HIGH_TEMPERATURE_THRESHOLD_STEP_C ) ) ) )
    {
        config.high_temperature_threshold_c = (uint8_t)high_temperature->valueint;
    }
    const cJSON *high_action = cJSON_GetObjectItemCaseSensitive( power, "high_temperature_action" );
    if( cJSON_IsString( high_action ) && NULL != high_action->valuestring &&
        power_action_from_config_name( high_action->valuestring, &parsed ) &&
        power_action_valid_for_policy( parsed ) )
    {
        config.high_temperature_action = parsed;
    }
}

static void load_system_apps( const cJSON *a_root )
{
    const cJSON *apps = cJSON_GetObjectItemCaseSensitive( a_root, "apps" );
    if( !cJSON_IsObject( apps ) )
    {
        return;
    }

    for( size_t index = 0U; index < RFSCOUT_APP_COUNT; ++index )
    {
        const cJSON *item =
            cJSON_GetObjectItemCaseSensitive( apps, app_config_name( (rfscout_app_id_t)index ) );
        if( cJSON_IsBool( item ) )
        {
            config.apps_enabled[ index ] = cJSON_IsTrue( item );
        }
    }
    config.apps_enabled[ RFSCOUT_APP_DEVICE ] = true;

    const cJSON *startup = cJSON_GetObjectItemCaseSensitive( apps, "startup" );
    if( cJSON_IsString( startup ) && NULL != startup->valuestring )
    {
        rfscout_app_id_t parsed;
        if( app_from_config_name( startup->valuestring, &parsed ) )
        {
            config.startup_app = parsed;
        }
    }
}

static void load_logging( const cJSON *a_root )
{
    const cJSON *logging = cJSON_GetObjectItemCaseSensitive( a_root, "logging" );
    if( cJSON_IsObject( logging ) )
    {
        config.logging_enabled = json_bool( logging, "enabled", config.logging_enabled );
    }
}

static void load_global_notifications( const cJSON *a_root )
{
    const cJSON *notifications = cJSON_GetObjectItemCaseSensitive( a_root, "notifications" );
    if( !cJSON_IsObject( notifications ) )
    {
        return;
    }

    const cJSON *mode = cJSON_GetObjectItemCaseSensitive( notifications, "mode" );
    if( cJSON_IsString( mode ) && NULL != mode->valuestring )
    {
        if( 0 == strcasecmp( mode->valuestring, "OFF" ) )
        {
            config.alert_mode = RFSCOUT_ALERT_OFF;
        }
        else if( 0 == strcasecmp( mode->valuestring, "TONE" ) )
        {
            config.alert_mode = RFSCOUT_ALERT_TONE;
        }
        else if( 0 == strcasecmp( mode->valuestring, "MORSE" ) )
        {
            config.alert_mode = RFSCOUT_ALERT_MORSE;
        }
    }
    const cJSON *volume = cJSON_GetObjectItemCaseSensitive( notifications, "volume" );
    if( cJSON_IsNumber( volume ) && volume->valueint >= 0 &&
        volume->valueint <= RFSCOUT_ALERT_VOLUME_MAX_PERCENT )
    {
        config.alert_volume_percent = (uint8_t)volume->valueint;
    }
}

static void load_timezone_selection( const cJSON *a_root )
{
    const cJSON *timezone = cJSON_GetObjectItemCaseSensitive( a_root, "timezone" );
    if( !cJSON_IsObject( timezone ) )
    {
        return;
    }
    const cJSON *selected = cJSON_GetObjectItemCaseSensitive( timezone, "selected" );
    if( cJSON_IsString( selected ) && NULL != selected->valuestring )
    {
        copy_string(
            config.timezone_selected, sizeof( config.timezone_selected ), selected->valuestring );
    }
}

static bool timezone_selected_valid( void )
{
    for( size_t index = 0U; index < config.timezone_count; ++index )
    {
        if( 0 == strcmp( config.timezones[ index ].name, config.timezone_selected ) )
        {
            return true;
        }
    }
    return false;
}

static esp_err_t
read_store_text( bool a_system, rfscout_app_id_t a_app, const char *a_key, char **a_text )
{
    if( NULL == a_text )
    {
        return ESP_ERR_INVALID_ARG;
    }
    *a_text = NULL;
    size_t length = 0U;
    esp_err_t result = a_system ? system_store_read( a_key, NULL, 0U, &length )
                                : persistent_store_app_read( a_app, a_key, NULL, 0U, &length );
    if( ESP_OK != result )
    {
        return result;
    }
    char *text = calloc( length + 1U, 1U );
    if( NULL == text )
    {
        return ESP_ERR_NO_MEM;
    }
    result = a_system ? system_store_read( a_key, text, length, &length )
                      : persistent_store_app_read( a_app, a_key, text, length, &length );
    if( ESP_OK != result )
    {
        free( text );
        return result;
    }
    text[ length ] = '\0';
    *a_text = text;
    return ESP_OK;
}

static bool record_version_valid( const cJSON *a_root, unsigned a_version )
{
    const cJSON *version = cJSON_GetObjectItemCaseSensitive( a_root, "version" );
    return cJSON_IsNumber( version ) && version->valueint == (int)a_version;
}

static void load_app_notifications( rfscout_app_id_t a_app, const cJSON *a_root )
{
    const rfscout_app_audio_policy_t *policy = app_audio_policy_get( a_app );
    const cJSON *notifications = cJSON_GetObjectItemCaseSensitive( a_root, "notifications" );
    if( !policy->notifications_supported || !cJSON_IsObject( notifications ) )
    {
        return;
    }

    config.notification_app_enabled[ a_app ] =
        json_bool( notifications, "enabled", config.notification_app_enabled[ a_app ] );
    if( RFSCOUT_APP_WIFI_BT == a_app )
    {
        const cJSON *events = cJSON_GetObjectItemCaseSensitive( notifications, "events" );
        if( cJSON_IsObject( events ) )
        {
            config.wifi_device_feedback_enabled =
                json_bool( events, "wifi_device", config.wifi_device_feedback_enabled );
            config.ble_device_feedback_enabled =
                json_bool( events, "ble_device", config.ble_device_feedback_enabled );
        }
    }

    const cJSON *characters = cJSON_GetObjectItemCaseSensitive( notifications, "characters" );
    if( !cJSON_IsObject( characters ) )
    {
        return;
    }
    const size_t count = notification_count_for_app( a_app );
    for( size_t item = 0U; item < count; ++item )
    {
        const rfscout_notification_descriptor_t *descriptor =
            notification_descriptor_for_app( a_app, item );
        if( NULL == descriptor || NULL == descriptor->config_key )
        {
            continue;
        }
        const cJSON *value = cJSON_GetObjectItemCaseSensitive( characters, descriptor->config_key );
        if( !cJSON_IsString( value ) || NULL == value->valuestring ||
            '\0' == value->valuestring[ 0 ] || '\0' != value->valuestring[ 1 ] )
        {
            continue;
        }
        const char character = (char)toupper( (unsigned char)value->valuestring[ 0 ] );
        if( alert_morse_character_supported( character ) )
        {
            config.notification_characters[ descriptor->type ] = character;
        }
    }
}

static void load_app_record( rfscout_app_id_t a_app, const cJSON *a_root )
{
    if( RFSCOUT_APP_WIFI_BT == a_app )
    {
        const cJSON *retention = cJSON_GetObjectItemCaseSensitive( a_root, "retention" );
        if( cJSON_IsObject( retention ) )
        {
            config.wifi_timeout_seconds =
                json_timeout_seconds( retention, "wifi_seconds", config.wifi_timeout_seconds );
            config.ble_timeout_seconds =
                json_timeout_seconds( retention, "bluetooth_seconds", config.ble_timeout_seconds );
        }
        const cJSON *threshold = cJSON_GetObjectItemCaseSensitive( a_root, "evidence_threshold" );
        if( cJSON_IsString( threshold ) && NULL != threshold->valuestring )
        {
            rfscout_evidence_strength_t parsed;
            if( evidence_threshold_from_config_name( threshold->valuestring, &parsed ) )
            {
                config.evidence_threshold = parsed;
            }
        }

        /*
         * Pre-1.0 configuration is intentionally not migrated. Mark records using
         * the retired sensitivity key dirty so the normal sparse-save path prunes it.
         */
        if( NULL != cJSON_GetObjectItemCaseSensitive( a_root, "surveillance_sensitivity" ) )
        {
            config_dirty = true;
        }
    }
    else if( RFSCOUT_APP_AUDIO == a_app )
    {
        const cJSON *recorder = cJSON_GetObjectItemCaseSensitive( a_root, "recorder" );
        if( cJSON_IsObject( recorder ) )
        {
            const cJSON *volume = cJSON_GetObjectItemCaseSensitive( recorder, "volume" );
            if( cJSON_IsNumber( volume ) && volume->valueint >= 0 && volume->valueint <= 100 )
            {
                config.recorder_volume_percent = (uint8_t)volume->valueint;
            }
        }
    }
    load_app_notifications( a_app, a_root );
}

static void load_app_records( void )
{
    for( size_t index = 0U; index < RFSCOUT_APP_COUNT; ++index )
    {
        const rfscout_app_id_t app = (rfscout_app_id_t)index;
        char *text = NULL;
        const esp_err_t result = read_store_text( false, app, "config", &text );
        if( ESP_ERR_NOT_FOUND == result )
        {
            continue;
        }
        if( ESP_OK != result )
        {
            ESP_LOGW( TAG,
                      "could not read %s app configuration: %s",
                      app_config_name( app ),
                      esp_err_to_name( result ) );
            continue;
        }
        cJSON *root = cJSON_Parse( text );
        free( text );
        if( !cJSON_IsObject( root ) || !record_version_valid( root, APP_CONFIG_RECORD_VERSION ) )
        {
            ESP_LOGW( TAG, "discarding incompatible %s app configuration", app_config_name( app ) );
            cJSON_Delete( root );
            (void)persistent_store_app_remove( app, "config" );
            continue;
        }
        load_app_record( app, root );
        cJSON_Delete( root );
    }
}

static esp_err_t load_system_record( void )
{
    char *text = NULL;
    const esp_err_t result = read_store_text( true, RFSCOUT_APP_DEVICE, "config", &text );
    if( ESP_OK != result )
    {
        return result;
    }
    cJSON *root = cJSON_Parse( text );
    free( text );
    if( !cJSON_IsObject( root ) || !record_version_valid( root, RFSCOUT_CONFIG_SCHEMA_VERSION ) )
    {
        cJSON_Delete( root );
        (void)system_store_remove( "config" );
        return ESP_ERR_INVALID_VERSION;
    }

    load_display( root );
    load_audio_policy( root );
    load_power( root );
    load_system_apps( root );
    load_logging( root );
    load_global_notifications( root );
    load_timezone_selection( root );
    cJSON_Delete( root );

    if( !timezone_selected_valid() )
    {
        copy_string( config.timezone_selected, sizeof( config.timezone_selected ), "UTC" );
        if( !timezone_selected_valid() )
        {
            copy_string( config.timezone_selected,
                         sizeof( config.timezone_selected ),
                         config.timezones[ 0 ].name );
        }
    }
    return ESP_OK;
}

static bool notification_character_overrides_default( rfscout_app_id_t a_app )
{
    const size_t count = notification_count_for_app( a_app );
    for( size_t item = 0U; item < count; ++item )
    {
        const rfscout_notification_descriptor_t *descriptor =
            notification_descriptor_for_app( a_app, item );
        if( NULL != descriptor && config.notification_characters[ descriptor->type ] !=
                                      alert_notification_default_character( descriptor->type ) )
        {
            return true;
        }
    }
    return false;
}

static void add_notification_overrides( cJSON *a_root, rfscout_app_id_t a_app )
{
    const rfscout_app_audio_policy_t *policy = app_audio_policy_get( a_app );
    if( !policy->notifications_supported )
    {
        return;
    }
    const bool enabled_override =
        config.notification_app_enabled[ a_app ] != policy->notifications_enabled_default;
    const bool event_override =
        RFSCOUT_APP_WIFI_BT == a_app &&
        ( !config.wifi_device_feedback_enabled || !config.ble_device_feedback_enabled );
    const bool character_override = notification_character_overrides_default( a_app );
    if( !enabled_override && !event_override && !character_override )
    {
        return;
    }

    cJSON *notifications = cJSON_AddObjectToObject( a_root, "notifications" );
    if( enabled_override )
    {
        cJSON_AddBoolToObject( notifications, "enabled", config.notification_app_enabled[ a_app ] );
    }
    if( event_override )
    {
        cJSON *events = cJSON_AddObjectToObject( notifications, "events" );
        if( !config.wifi_device_feedback_enabled )
        {
            cJSON_AddBoolToObject( events, "wifi_device", false );
        }
        if( !config.ble_device_feedback_enabled )
        {
            cJSON_AddBoolToObject( events, "ble_device", false );
        }
    }
    if( character_override )
    {
        cJSON *characters = cJSON_AddObjectToObject( notifications, "characters" );
        const size_t count = notification_count_for_app( a_app );
        for( size_t item = 0U; item < count; ++item )
        {
            const rfscout_notification_descriptor_t *descriptor =
                notification_descriptor_for_app( a_app, item );
            if( NULL == descriptor || NULL == descriptor->config_key ||
                config.notification_characters[ descriptor->type ] ==
                    alert_notification_default_character( descriptor->type ) )
            {
                continue;
            }
            char value[ 2 ] = { config.notification_characters[ descriptor->type ], '\0' };
            cJSON_AddStringToObject( characters, descriptor->config_key, value );
        }
    }
}

static cJSON *build_app_record( rfscout_app_id_t a_app )
{
    cJSON *root = cJSON_CreateObject();
    if( NULL == root )
    {
        return NULL;
    }
    cJSON_AddNumberToObject( root, "version", APP_CONFIG_RECORD_VERSION );

    if( RFSCOUT_APP_WIFI_BT == a_app )
    {
        if( config.wifi_timeout_seconds != RFSCOUT_OBSERVATION_TIMEOUT_DEFAULT_SECONDS ||
            config.ble_timeout_seconds != RFSCOUT_OBSERVATION_TIMEOUT_DEFAULT_SECONDS )
        {
            cJSON *retention = cJSON_AddObjectToObject( root, "retention" );
            if( config.wifi_timeout_seconds != RFSCOUT_OBSERVATION_TIMEOUT_DEFAULT_SECONDS )
            {
                cJSON_AddNumberToObject( retention, "wifi_seconds", config.wifi_timeout_seconds );
            }
            if( config.ble_timeout_seconds != RFSCOUT_OBSERVATION_TIMEOUT_DEFAULT_SECONDS )
            {
                cJSON_AddNumberToObject(
                    retention, "bluetooth_seconds", config.ble_timeout_seconds );
            }
        }
        if( RFSCOUT_EVIDENCE_STRENGTH_POSSIBLE != config.evidence_threshold )
        {
            cJSON_AddStringToObject( root,
                                     "evidence_threshold",
                                     evidence_threshold_config_name( config.evidence_threshold ) );
        }
    }
    else if( RFSCOUT_APP_AUDIO == a_app &&
             CONFIG_RECORDER_VOLUME_DEFAULT != config.recorder_volume_percent )
    {
        cJSON *recorder = cJSON_AddObjectToObject( root, "recorder" );
        cJSON_AddNumberToObject( recorder, "volume", config.recorder_volume_percent );
    }

    add_notification_overrides( root, a_app );
    return root;
}

static esp_err_t persist_app_records( void )
{
    for( size_t index = 0U; index < RFSCOUT_APP_COUNT; ++index )
    {
        const rfscout_app_id_t app = (rfscout_app_id_t)index;
        cJSON *root = build_app_record( app );
        if( NULL == root )
        {
            return ESP_ERR_NO_MEM;
        }
        if( 1 == cJSON_GetArraySize( root ) )
        {
            cJSON_Delete( root );
            const esp_err_t remove_result = persistent_store_app_remove( app, "config" );
            if( ESP_OK != remove_result && ESP_ERR_NOT_FOUND != remove_result )
            {
                return remove_result;
            }
            continue;
        }
        char *text = cJSON_PrintUnformatted( root );
        cJSON_Delete( root );
        if( NULL == text )
        {
            return ESP_ERR_NO_MEM;
        }
        const esp_err_t result = persistent_store_app_write( app, "config", text, strlen( text ) );
        free( text );
        if( ESP_OK != result )
        {
            return result;
        }
    }
    return ESP_OK;
}

static cJSON *build_system_record( void )
{
    cJSON *root = cJSON_CreateObject();
    if( NULL == root )
    {
        return NULL;
    }
    cJSON_AddNumberToObject( root, "version", RFSCOUT_CONFIG_SCHEMA_VERSION );

    cJSON *display = cJSON_AddObjectToObject( root, "display" );
    cJSON_AddNumberToObject( display, "brightness", config.brightness_percent );
    cJSON_AddNumberToObject( display, "dim_timeout_seconds", config.display_dim_timeout_seconds );
    cJSON_AddNumberToObject( display, "off_timeout_seconds", config.display_off_timeout_seconds );
    cJSON_AddBoolToObject( display, "clock_24h", config.clock_24h );
    cJSON_AddStringToObject( display, "theme", ui_theme_config_name( config.theme ) );

    cJSON *audio = cJSON_AddObjectToObject( root, "audio" );
    cJSON_AddStringToObject( audio, "policy", audio_policy_config_name( config.audio_policy ) );
    cJSON_AddNumberToObject(
        audio, "interaction_gain_percent", config.audio_interaction_gain_percent );
    cJSON_AddNumberToObject(
        audio, "notification_gain_percent", config.audio_notification_gain_percent );

    cJSON *power = cJSON_AddObjectToObject( root, "power" );
    cJSON_AddStringToObject(
        power, "short_press", power_action_config_name( config.power_button_short_action ) );
    cJSON_AddStringToObject(
        power, "long_press", power_action_config_name( config.power_button_long_action ) );
    cJSON_AddNumberToObject( power, "low_battery_percent", config.low_battery_threshold_percent );
    cJSON_AddStringToObject(
        power, "low_battery_action", power_action_config_name( config.low_battery_action ) );
    cJSON_AddNumberToObject( power, "high_temperature_c", config.high_temperature_threshold_c );
    cJSON_AddStringToObject( power,
                             "high_temperature_action",
                             power_action_config_name( config.high_temperature_action ) );

    cJSON *apps = cJSON_AddObjectToObject( root, "apps" );
    for( size_t index = 0U; index < RFSCOUT_APP_COUNT; ++index )
    {
        cJSON_AddBoolToObject(
            apps, app_config_name( (rfscout_app_id_t)index ), config.apps_enabled[ index ] );
    }
    cJSON_AddStringToObject( apps,
                             "startup",
                             RFSCOUT_APP_DEVICE == config.startup_app
                                 ? "home"
                                 : ( RFSCOUT_APP_AUDIO == config.startup_app
                                         ? "recorder"
                                         : app_config_name( config.startup_app ) ) );

    cJSON *logging = cJSON_AddObjectToObject( root, "logging" );
    cJSON_AddBoolToObject( logging, "enabled", config.logging_enabled );

    cJSON *notifications = cJSON_AddObjectToObject( root, "notifications" );
    cJSON_AddStringToObject( notifications, "mode", alert_mode_name( config.alert_mode ) );
    cJSON_AddNumberToObject( notifications, "volume", config.alert_volume_percent );

    cJSON *timezone = cJSON_AddObjectToObject( root, "timezone" );
    cJSON_AddStringToObject( timezone, "selected", config.timezone_selected );
    return root;
}

static void remove_legacy_config_files( void )
{
    (void)unlink( RFSCOUT_INTERNAL_ROOT "/rfscout.json" );
    (void)unlink( RFSCOUT_INTERNAL_ROOT "/rfscout.json.tmp" );
    (void)unlink( RFSCOUT_INTERNAL_ROOT "/rfscout.json.bak" );
    (void)unlink( RFSCOUT_INTERNAL_ROOT "/rfscout.json.bad" );
}

esp_err_t device_config_init( void )
{
    set_defaults();
    const esp_err_t store_result = persistent_store_init();
    if( ESP_OK != store_result )
    {
        return store_result;
    }
    remove_legacy_config_files();

    const esp_err_t system_result = load_system_record();
    if( ESP_OK != system_result && ESP_ERR_NOT_FOUND != system_result )
    {
        ESP_LOGW( TAG,
                  "system configuration incompatible; using defaults: %s",
                  esp_err_to_name( system_result ) );
    }
    load_app_records();

    if( ESP_OK == system_result )
    {
        ESP_LOGI( TAG, "loaded persistent configuration" );
        return ESP_OK;
    }

    const esp_err_t save_result = device_config_save();
    if( ESP_OK == save_result )
    {
        rfscout_event_emit( RFSCOUT_EVENT_CONFIG_DEFAULTED,
                            RFSCOUT_EVENT_SOURCE_SYSTEM,
                            (int32_t)RFSCOUT_CONFIG_SCHEMA_VERSION );
    }
    return save_result;
}

const device_config_t *device_config_get( void )
{
    return &config;
}

bool device_config_app_enabled( rfscout_app_id_t a_app )
{
    return a_app < RFSCOUT_APP_COUNT && config.apps_enabled[ a_app ];
}

esp_err_t device_config_set_app_enabled( rfscout_app_id_t a_app, bool a_enabled )
{
    if( a_app >= RFSCOUT_APP_COUNT || RFSCOUT_APP_DEVICE == a_app )
    {
        return ESP_ERR_INVALID_ARG;
    }
    if( config.apps_enabled[ a_app ] != a_enabled )
    {
        config.apps_enabled[ a_app ] = a_enabled;
        config_dirty = true;
    }
    return ESP_OK;
}

esp_err_t device_config_set_startup_app( rfscout_app_id_t a_app )
{
    if( a_app >= RFSCOUT_APP_COUNT )
    {
        return ESP_ERR_INVALID_ARG;
    }
    if( config.startup_app != a_app )
    {
        config.startup_app = a_app;
        config_dirty = true;
    }
    return ESP_OK;
}

rfscout_app_id_t device_config_startup_app( void )
{
    return config.startup_app;
}

rfscout_theme_id_t device_config_theme( void )
{
    return config.theme;
}

esp_err_t device_config_set_theme( rfscout_theme_id_t a_theme )
{
    if( a_theme >= RFSCOUT_THEME_COUNT )
    {
        return ESP_ERR_INVALID_ARG;
    }
    if( config.theme != a_theme )
    {
        config.theme = a_theme;
        config_dirty = true;
    }
    return ESP_OK;
}

rfscout_audio_policy_id_t device_config_audio_policy( void )
{
    return config.audio_policy;
}

esp_err_t device_config_set_audio_policy( rfscout_audio_policy_id_t a_policy )
{
    if( a_policy >= RFSCOUT_AUDIO_POLICY_COUNT )
    {
        return ESP_ERR_INVALID_ARG;
    }
    if( config.audio_policy != a_policy )
    {
        config.audio_policy = a_policy;
        config_dirty = true;
    }
    return ESP_OK;
}

esp_err_t device_config_set_audio_gains( uint8_t a_interaction_percent,
                                         uint8_t a_notification_percent )
{
    if( a_interaction_percent > 100U || a_notification_percent > 100U )
    {
        return ESP_ERR_INVALID_ARG;
    }
    if( config.audio_interaction_gain_percent != a_interaction_percent ||
        config.audio_notification_gain_percent != a_notification_percent )
    {
        config.audio_interaction_gain_percent = a_interaction_percent;
        config.audio_notification_gain_percent = a_notification_percent;
        config_dirty = true;
    }
    return ESP_OK;
}

bool device_config_notification_app_enabled( rfscout_app_id_t a_app )
{
    if( a_app >= RFSCOUT_APP_COUNT )
    {
        return false;
    }
    const rfscout_app_audio_policy_t *policy = app_audio_policy_get( a_app );
    return policy->notifications_supported && config.notification_app_enabled[ a_app ];
}

esp_err_t device_config_set_notification_app_enabled( rfscout_app_id_t a_app, bool a_enabled )
{
    if( a_app >= RFSCOUT_APP_COUNT )
    {
        return ESP_ERR_INVALID_ARG;
    }
    const rfscout_app_audio_policy_t *policy = app_audio_policy_get( a_app );
    if( !policy->notifications_supported )
    {
        return ESP_ERR_NOT_SUPPORTED;
    }
    if( config.notification_app_enabled[ a_app ] != a_enabled )
    {
        config.notification_app_enabled[ a_app ] = a_enabled;
        config_dirty = true;
    }
    return ESP_OK;
}

bool device_config_wifi_device_feedback_enabled( void )
{
    return config.wifi_device_feedback_enabled;
}

esp_err_t device_config_set_wifi_device_feedback_enabled( bool a_enabled )
{
    if( config.wifi_device_feedback_enabled != a_enabled )
    {
        config.wifi_device_feedback_enabled = a_enabled;
        config_dirty = true;
    }
    return ESP_OK;
}

bool device_config_ble_device_feedback_enabled( void )
{
    return config.ble_device_feedback_enabled;
}

esp_err_t device_config_set_ble_device_feedback_enabled( bool a_enabled )
{
    if( config.ble_device_feedback_enabled != a_enabled )
    {
        config.ble_device_feedback_enabled = a_enabled;
        config_dirty = true;
    }
    return ESP_OK;
}

uint16_t device_config_wifi_timeout_seconds( void )
{
    return timeout_seconds_valid( config.wifi_timeout_seconds )
               ? config.wifi_timeout_seconds
               : RFSCOUT_OBSERVATION_TIMEOUT_DEFAULT_SECONDS;
}

esp_err_t device_config_set_wifi_timeout_seconds( uint16_t a_seconds )
{
    if( !timeout_seconds_valid( a_seconds ) )
    {
        return ESP_ERR_INVALID_ARG;
    }
    if( config.wifi_timeout_seconds != a_seconds )
    {
        config.wifi_timeout_seconds = a_seconds;
        config_dirty = true;
    }
    return ESP_OK;
}

uint16_t device_config_ble_timeout_seconds( void )
{
    return timeout_seconds_valid( config.ble_timeout_seconds )
               ? config.ble_timeout_seconds
               : RFSCOUT_OBSERVATION_TIMEOUT_DEFAULT_SECONDS;
}

esp_err_t device_config_set_ble_timeout_seconds( uint16_t a_seconds )
{
    if( !timeout_seconds_valid( a_seconds ) )
    {
        return ESP_ERR_INVALID_ARG;
    }
    if( config.ble_timeout_seconds != a_seconds )
    {
        config.ble_timeout_seconds = a_seconds;
        config_dirty = true;
    }
    return ESP_OK;
}

rfscout_evidence_strength_t device_config_evidence_threshold( void )
{
    return config.evidence_threshold < RFSCOUT_EVIDENCE_STRENGTH_COUNT
               ? config.evidence_threshold
               : RFSCOUT_EVIDENCE_STRENGTH_POSSIBLE;
}

esp_err_t device_config_set_evidence_threshold( rfscout_evidence_strength_t a_threshold )
{
    if( a_threshold >= RFSCOUT_EVIDENCE_STRENGTH_COUNT )
    {
        return ESP_ERR_INVALID_ARG;
    }
    if( config.evidence_threshold != a_threshold )
    {
        config.evidence_threshold = a_threshold;
        config_dirty = true;
    }
    return ESP_OK;
}

esp_err_t device_config_set_logging_enabled( bool a_enabled )
{
    if( config.logging_enabled != a_enabled )
    {
        config.logging_enabled = a_enabled;
        config_dirty = true;
    }
    return ESP_OK;
}

esp_err_t device_config_set_brightness( uint8_t a_percent )
{
    if( a_percent < 10U || a_percent > 100U )
    {
        return ESP_ERR_INVALID_ARG;
    }
    if( config.brightness_percent != a_percent )
    {
        config.brightness_percent = a_percent;
        config_dirty = true;
    }
    return ESP_OK;
}

esp_err_t device_config_set_display_dim_timeout( uint16_t a_seconds )
{
    if( a_seconds > CONFIG_DISPLAY_TIMEOUT_MAX_SECONDS ||
        ( 0U != a_seconds && 0U != config.display_off_timeout_seconds &&
          a_seconds >= config.display_off_timeout_seconds ) )
    {
        return ESP_ERR_INVALID_ARG;
    }
    if( config.display_dim_timeout_seconds != a_seconds )
    {
        config.display_dim_timeout_seconds = a_seconds;
        config_dirty = true;
    }
    return ESP_OK;
}

esp_err_t device_config_set_display_off_timeout( uint16_t a_seconds )
{
    if( a_seconds > CONFIG_DISPLAY_TIMEOUT_MAX_SECONDS ||
        ( 0U != a_seconds && 0U != config.display_dim_timeout_seconds &&
          a_seconds <= config.display_dim_timeout_seconds ) )
    {
        return ESP_ERR_INVALID_ARG;
    }
    if( config.display_off_timeout_seconds != a_seconds )
    {
        config.display_off_timeout_seconds = a_seconds;
        config_dirty = true;
    }
    return ESP_OK;
}

esp_err_t device_config_set_power_button_short_action( power_action_t a_action )
{
    if( !power_action_valid_for_gesture( a_action, false ) )
    {
        return ESP_ERR_INVALID_ARG;
    }
    if( config.power_button_short_action != a_action )
    {
        config.power_button_short_action = a_action;
        config_dirty = true;
    }
    return ESP_OK;
}

esp_err_t device_config_set_power_button_long_action( power_action_t a_action )
{
    if( !power_action_valid_for_gesture( a_action, true ) )
    {
        return ESP_ERR_INVALID_ARG;
    }
    if( config.power_button_long_action != a_action )
    {
        config.power_button_long_action = a_action;
        config_dirty = true;
    }
    return ESP_OK;
}

esp_err_t device_config_set_low_battery_threshold( uint8_t a_percent )
{
    if( a_percent > RFSCOUT_LOW_BATTERY_THRESHOLD_MAX_PERCENT ||
        ( 0U != a_percent && 0U != ( a_percent % 5U ) ) )
    {
        return ESP_ERR_INVALID_ARG;
    }
    if( config.low_battery_threshold_percent != a_percent )
    {
        config.low_battery_threshold_percent = a_percent;
        config_dirty = true;
    }
    return ESP_OK;
}

esp_err_t device_config_set_low_battery_action( power_action_t a_action )
{
    if( !power_action_valid_for_policy( a_action ) )
    {
        return ESP_ERR_INVALID_ARG;
    }
    if( config.low_battery_action != a_action )
    {
        config.low_battery_action = a_action;
        config_dirty = true;
    }
    return ESP_OK;
}

esp_err_t device_config_set_high_temperature_threshold( uint8_t a_temperature_c )
{
    if( 0U != a_temperature_c &&
        ( a_temperature_c < RFSCOUT_HIGH_TEMPERATURE_THRESHOLD_MIN_C ||
          a_temperature_c > RFSCOUT_HIGH_TEMPERATURE_THRESHOLD_MAX_C ||
          0U != ( a_temperature_c % RFSCOUT_HIGH_TEMPERATURE_THRESHOLD_STEP_C ) ) )
    {
        return ESP_ERR_INVALID_ARG;
    }
    if( config.high_temperature_threshold_c != a_temperature_c )
    {
        config.high_temperature_threshold_c = a_temperature_c;
        config_dirty = true;
    }
    return ESP_OK;
}

esp_err_t device_config_set_high_temperature_action( power_action_t a_action )
{
    if( !power_action_valid_for_policy( a_action ) )
    {
        return ESP_ERR_INVALID_ARG;
    }
    if( config.high_temperature_action != a_action )
    {
        config.high_temperature_action = a_action;
        config_dirty = true;
    }
    return ESP_OK;
}

esp_err_t device_config_set_clock_24h( bool a_enabled )
{
    if( config.clock_24h != a_enabled )
    {
        config.clock_24h = a_enabled;
        config_dirty = true;
    }
    return ESP_OK;
}

esp_err_t device_config_set_alert_mode( rfscout_alert_mode_t a_mode )
{
    if( a_mode >= RFSCOUT_ALERT_MODE_COUNT )
    {
        return ESP_ERR_INVALID_ARG;
    }
    if( config.alert_mode != a_mode )
    {
        config.alert_mode = a_mode;
        config_dirty = true;
    }
    alert_set_mode( a_mode );
    return ESP_OK;
}

esp_err_t device_config_set_alert_volume( uint8_t a_percent )
{
    if( a_percent > RFSCOUT_ALERT_VOLUME_MAX_PERCENT )
    {
        return ESP_ERR_INVALID_ARG;
    }
    if( !alert_set_volume_percent( a_percent ) )
    {
        return ESP_ERR_INVALID_STATE;
    }
    if( config.alert_volume_percent != a_percent )
    {
        config.alert_volume_percent = a_percent;
        config_dirty = true;
    }
    return ESP_OK;
}

esp_err_t device_config_set_recorder_volume( uint8_t a_percent )
{
    if( a_percent > 100U )
    {
        return ESP_ERR_INVALID_ARG;
    }
    if( config.recorder_volume_percent != a_percent )
    {
        config.recorder_volume_percent = a_percent;
        config_dirty = true;
    }
    return ESP_OK;
}

char device_config_notification_character( rfscout_notification_type_t a_type )
{
    if( a_type >= RFSCOUT_NOTIFICATION_TYPE_COUNT )
    {
        return '\0';
    }
    return config.notification_characters[ a_type ];
}

esp_err_t device_config_set_notification_character( rfscout_notification_type_t a_type,
                                                    char a_character )
{
    if( a_type >= RFSCOUT_NOTIFICATION_TYPE_COUNT )
    {
        return ESP_ERR_INVALID_ARG;
    }
    const char character = (char)toupper( (unsigned char)a_character );
    if( !alert_morse_character_supported( character ) )
    {
        return ESP_ERR_INVALID_ARG;
    }
    if( config.notification_characters[ a_type ] != character )
    {
        config.notification_characters[ a_type ] = character;
        config_dirty = true;
    }
    return ESP_OK;
}

esp_err_t device_config_set_timezone( const char *a_name )
{
    if( NULL == a_name )
    {
        return ESP_ERR_INVALID_ARG;
    }
    for( size_t index = 0U; index < config.timezone_count; ++index )
    {
        if( 0 == strcmp( config.timezones[ index ].name, a_name ) )
        {
            if( 0 != strcmp( config.timezone_selected, a_name ) )
            {
                copy_string( config.timezone_selected, sizeof( config.timezone_selected ), a_name );
                config_dirty = true;
            }
            return ESP_OK;
        }
    }
    return ESP_ERR_NOT_FOUND;
}

const device_timezone_t *device_config_timezone( void )
{
    for( size_t index = 0U; index < config.timezone_count; ++index )
    {
        if( 0 == strcmp( config.timezones[ index ].name, config.timezone_selected ) )
        {
            return &config.timezones[ index ];
        }
    }
    return NULL;
}

esp_err_t device_config_save( void )
{
    if( !persistent_store_is_available() )
    {
        return ESP_ERR_INVALID_STATE;
    }

    cJSON *root = build_system_record();
    if( NULL == root )
    {
        return ESP_ERR_NO_MEM;
    }
    const esp_err_t app_result = persist_app_records();
    if( ESP_OK != app_result )
    {
        cJSON_Delete( root );
        return app_result;
    }
    char *text = cJSON_PrintUnformatted( root );
    cJSON_Delete( root );
    if( NULL == text )
    {
        return ESP_ERR_NO_MEM;
    }
    const esp_err_t result = system_store_write( "config", text, strlen( text ) );
    free( text );
    if( ESP_OK == result )
    {
        config_dirty = false;
    }
    return result;
}

esp_err_t device_config_save_if_dirty( void )
{
    return config_dirty ? device_config_save() : ESP_OK;
}
