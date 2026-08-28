/**
    device_config.c : RFScout persistent device configuration implementation
    Description: Loads validated SD-backed JSON preferences over compiled defaults and writes them atomically.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "device_config.h"

#include "storage.h"

#include "cJSON.h"
#include "esp_log.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#define CONFIG_TEMP_PATH RFSCOUT_CONFIG_PATH ".tmp"
#define CONFIG_BACKUP_PATH RFSCOUT_CONFIG_PATH ".bak"
#define CONFIG_BAD_PATH RFSCOUT_CONFIG_PATH ".bad"
#define CONFIG_BRIGHTNESS_DEFAULT 70U
#define CONFIG_RECORDER_VOLUME_DEFAULT 50U
#define CONFIG_RECORDER_INPUT_GAIN_DEFAULT_DB 12U
#define CONFIG_MIN_VALID_EPOCH 1577836800LL
#define CONFIG_MAX_VALID_EPOCH 4102444800LL

static const char *TAG = "device_config";
static device_config_t config;
static bool saved_time_valid;
static int64_t saved_time_epoch;
static char saved_time_source[ 24 ];

/** @brief Copy a bounded configuration string. */
static void copy_string( char *a_destination, size_t a_capacity, const char *a_source )
{
    if( ( NULL == a_destination ) || ( 0U == a_capacity ) )
    {
        return;
    }
    snprintf( a_destination, a_capacity, "%s", ( NULL != a_source ) ? a_source : "" );
}

/** @brief Populate one compiled timezone option. */
static void
timezone_default( size_t a_index, const char *a_name, const char *a_label, const char *a_posix )
{
    device_timezone_t *zone = &config.timezones[ a_index ];
    copy_string( zone->name, sizeof( zone->name ), a_name );
    copy_string( zone->label, sizeof( zone->label ), a_label );
    copy_string( zone->posix, sizeof( zone->posix ), a_posix );
}

/** @brief Restore all compiled configuration defaults. */
static void set_defaults( void )
{
    memset( &config, 0, sizeof( config ) );
    config.brightness_percent = CONFIG_BRIGHTNESS_DEFAULT;
    config.logging_enabled = true;
    config.clock_24h = false;
    config.alert_mode = RFSCOUT_ALERT_OFF;
    config.alert_volume_percent = RFSCOUT_ALERT_VOLUME_DEFAULT_PERCENT;
    config.recorder_volume_percent = CONFIG_RECORDER_VOLUME_DEFAULT;
    config.recorder_input_gain_db = CONFIG_RECORDER_INPUT_GAIN_DEFAULT_DB;

    for( size_t index = 0; index < RFSCOUT_APP_COUNT; ++index )
    {
        config.apps_enabled[ index ] = true;
    }
    config.apps_enabled[ RFSCOUT_APP_DEVICE ] = true;

    for( size_t index = 0; index < RFSCOUT_NOTIFICATION_TYPE_COUNT; ++index )
    {
        config.notification_characters[ index ] =
            alert_notification_default_character( (rfscout_notification_type_t)index );
    }

    config.timezone_count = 2U;
    timezone_default( 0U, "America/Chicago", "US Central", "CST6CDT,M3.2.0,M11.1.0" );
    timezone_default( 1U, "UTC", "UTC", "UTC0" );
    copy_string( config.timezone_selected, sizeof( config.timezone_selected ), "America/Chicago" );

    saved_time_valid = false;
    saved_time_epoch = 0;
    saved_time_source[ 0 ] = '\0';
}

/** @brief Read one optional JSON boolean, retaining a default on type mismatch. */
static bool json_bool( const cJSON *a_parent, const char *a_name, bool a_default )
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive( a_parent, a_name );
    return cJSON_IsBool( item ) ? cJSON_IsTrue( item ) : a_default;
}

/** @brief Apply validated application enablement overrides. */
static void load_apps( const cJSON *a_root )
{
    static const char *names[ RFSCOUT_APP_COUNT ] = {
        "wifi_bt", "cc1101", "nrf24", "audio", "thermal", "device" };
    const cJSON *apps = cJSON_GetObjectItemCaseSensitive( a_root, "apps" );
    if( !cJSON_IsObject( apps ) )
    {
        return;
    }

    for( size_t index = 0; index < RFSCOUT_APP_COUNT; ++index )
    {
        config.apps_enabled[ index ] =
            json_bool( apps, names[ index ], config.apps_enabled[ index ] );
    }
    config.apps_enabled[ RFSCOUT_APP_DEVICE ] = true;
}

/** @brief Apply validated display preference overrides. */
static void load_display( const cJSON *a_root )
{
    const cJSON *display = cJSON_GetObjectItemCaseSensitive( a_root, "display" );
    if( !cJSON_IsObject( display ) )
    {
        return;
    }

    const cJSON *brightness = cJSON_GetObjectItemCaseSensitive( display, "brightness" );
    if( cJSON_IsNumber( brightness ) && ( brightness->valueint >= 10 ) &&
        ( brightness->valueint <= 100 ) )
    {
        config.brightness_percent = (uint8_t)brightness->valueint;
    }
    config.clock_24h = json_bool( display, "clock_24h", config.clock_24h );
}

/** @brief Apply validated logging preference overrides. */
static void load_logging( const cJSON *a_root )
{
    const cJSON *logging = cJSON_GetObjectItemCaseSensitive( a_root, "logging" );
    if( cJSON_IsObject( logging ) )
    {
        config.logging_enabled = json_bool( logging, "enabled", config.logging_enabled );
    }
}

/**
 * @brief Apply global notification mode/volume and one Morse character per canonical alert item.
 *
 * Alert keys come from alert.c's descriptor table. Adding a notification category therefore does
 * not require another key list here; update the enum/descriptor table and the producer mapping
 * documented in alert.h instead.
 */
static void load_notifications( const cJSON *a_root )
{
    const cJSON *notifications = cJSON_GetObjectItemCaseSensitive( a_root, "notifications" );
    if( !cJSON_IsObject( notifications ) )
    {
        return;
    }

    const cJSON *mode = cJSON_GetObjectItemCaseSensitive( notifications, "mode" );
    if( cJSON_IsString( mode ) && ( NULL != mode->valuestring ) )
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
    if( cJSON_IsNumber( volume ) && ( volume->valueint >= 0 ) &&
        ( volume->valueint <= RFSCOUT_ALERT_VOLUME_MAX_PERCENT ) )
    {
        config.alert_volume_percent = (uint8_t)volume->valueint;
    }

    const cJSON *characters = cJSON_GetObjectItemCaseSensitive( notifications, "characters" );
    if( !cJSON_IsObject( characters ) )
    {
        return;
    }

    for( size_t index = 0; index < RFSCOUT_NOTIFICATION_TYPE_COUNT; ++index )
    {
        const rfscout_notification_type_t type = (rfscout_notification_type_t)index;
        const char *key = alert_notification_config_key( type );
        if( NULL == key )
        {
            continue;
        }
        const cJSON *value = cJSON_GetObjectItemCaseSensitive( characters, key );
        if( !cJSON_IsString( value ) || ( NULL == value->valuestring ) ||
            ( '\0' == value->valuestring[ 0 ] ) || ( '\0' != value->valuestring[ 1 ] ) )
        {
            continue;
        }

        const char character = (char)toupper( (unsigned char)value->valuestring[ 0 ] );
        if( alert_morse_character_supported( character ) )
        {
            config.notification_characters[ index ] = character;
        }
    }
}

/** @brief Apply Recorder-specific persistent preferences. */
static void load_recorder( const cJSON *a_root )
{
    const cJSON *recorder = cJSON_GetObjectItemCaseSensitive( a_root, "recorder" );
    if( !cJSON_IsObject( recorder ) )
    {
        return;
    }

    const cJSON *volume = cJSON_GetObjectItemCaseSensitive( recorder, "volume" );
    if( cJSON_IsNumber( volume ) && ( volume->valueint >= 0 ) && ( volume->valueint <= 100 ) )
    {
        config.recorder_volume_percent = (uint8_t)volume->valueint;
    }
    const cJSON *input_gain = cJSON_GetObjectItemCaseSensitive( recorder, "input_gain_db" );
    if( cJSON_IsNumber( input_gain ) && ( input_gain->valueint >= 0 ) &&
        ( input_gain->valueint <= 24 ) && ( 0 == ( input_gain->valueint % 3 ) ) )
    {
        config.recorder_input_gain_db = (uint8_t)input_gain->valueint;
    }
}

/** @brief Apply validated timezone options and selection. */
static void load_timezones( const cJSON *a_root )
{
    const cJSON *timezone = cJSON_GetObjectItemCaseSensitive( a_root, "timezone" );
    if( !cJSON_IsObject( timezone ) )
    {
        return;
    }

    const cJSON *options = cJSON_GetObjectItemCaseSensitive( timezone, "options" );
    if( cJSON_IsArray( options ) )
    {
        size_t count = 0U;
        const cJSON *entry = NULL;
        cJSON_ArrayForEach( entry, options )
        {
            if( count >= RFSCOUT_TIMEZONE_MAX )
            {
                break;
            }
            const cJSON *name = cJSON_GetObjectItemCaseSensitive( entry, "name" );
            const cJSON *label = cJSON_GetObjectItemCaseSensitive( entry, "label" );
            const cJSON *posix = cJSON_GetObjectItemCaseSensitive( entry, "posix" );
            if( !cJSON_IsString( name ) || !cJSON_IsString( label ) || !cJSON_IsString( posix ) ||
                ( NULL == name->valuestring ) || ( NULL == label->valuestring ) ||
                ( NULL == posix->valuestring ) || ( '\0' == name->valuestring[ 0 ] ) ||
                ( '\0' == posix->valuestring[ 0 ] ) )
            {
                continue;
            }

            copy_string( config.timezones[ count ].name,
                         sizeof( config.timezones[ count ].name ),
                         name->valuestring );
            copy_string( config.timezones[ count ].label,
                         sizeof( config.timezones[ count ].label ),
                         label->valuestring );
            copy_string( config.timezones[ count ].posix,
                         sizeof( config.timezones[ count ].posix ),
                         posix->valuestring );
            ++count;
        }
        if( count > 0U )
        {
            config.timezone_count = count;
        }
    }

    const cJSON *selected = cJSON_GetObjectItemCaseSensitive( timezone, "selected" );
    if( cJSON_IsString( selected ) && ( NULL != selected->valuestring ) )
    {
        copy_string(
            config.timezone_selected, sizeof( config.timezone_selected ), selected->valuestring );
    }
}

/** @brief Read an optional saved hard-power wall-clock checkpoint. */
static void load_saved_time( const cJSON *a_root )
{
    const cJSON *time = cJSON_GetObjectItemCaseSensitive( a_root, "time" );
    if( !cJSON_IsObject( time ) )
    {
        return;
    }

    const cJSON *epoch = cJSON_GetObjectItemCaseSensitive( time, "epoch" );
    if( !cJSON_IsNumber( epoch ) || ( epoch->valuedouble < (double)CONFIG_MIN_VALID_EPOCH ) ||
        ( epoch->valuedouble > (double)CONFIG_MAX_VALID_EPOCH ) )
    {
        return;
    }

    saved_time_epoch = (int64_t)epoch->valuedouble;
    saved_time_valid = true;
    const cJSON *source = cJSON_GetObjectItemCaseSensitive( time, "source" );
    if( cJSON_IsString( source ) && ( NULL != source->valuestring ) )
    {
        copy_string( saved_time_source, sizeof( saved_time_source ), source->valuestring );
    }
}

/** @brief Ensure the selected timezone names a usable configured option. */
static bool timezone_selected_valid( void )
{
    for( size_t index = 0; index < config.timezone_count; ++index )
    {
        if( 0 == strcmp( config.timezones[ index ].name, config.timezone_selected ) )
        {
            return true;
        }
    }
    return false;
}

/** @brief Quarantine a configuration document that cannot safely be loaded. */
static void quarantine_config( void )
{
    (void)unlink( CONFIG_BAD_PATH );
    (void)rename( RFSCOUT_CONFIG_PATH, CONFIG_BAD_PATH );
}

/** @brief Recover the previous configuration after an interrupted replacement. */
static void recover_interrupted_save( void )
{
    if( 0 == access( RFSCOUT_CONFIG_PATH, F_OK ) )
    {
        return;
    }
    if( 0 == access( CONFIG_BACKUP_PATH, F_OK ) )
    {
        if( 0 == rename( CONFIG_BACKUP_PATH, RFSCOUT_CONFIG_PATH ) )
        {
            ESP_LOGW( TAG, "recovered configuration after interrupted save" );
        }
    }
}

/** @brief Parse and apply the configuration file from SD. */
static esp_err_t load_file( void )
{
    FILE *file = fopen( RFSCOUT_CONFIG_PATH, "rb" );
    if( NULL == file )
    {
        return ( ENOENT == errno ) ? ESP_ERR_NOT_FOUND : ESP_FAIL;
    }
    if( 0 != fseek( file, 0, SEEK_END ) )
    {
        fclose( file );
        return ESP_FAIL;
    }
    const long length = ftell( file );
    if( ( length <= 0L ) || ( length > 16384L ) || ( 0 != fseek( file, 0, SEEK_SET ) ) )
    {
        fclose( file );
        return ESP_ERR_INVALID_SIZE;
    }

    char *buffer = calloc( (size_t)length + 1U, 1U );
    if( NULL == buffer )
    {
        fclose( file );
        return ESP_ERR_NO_MEM;
    }
    const size_t read_count = fread( buffer, 1U, (size_t)length, file );
    fclose( file );
    if( read_count != (size_t)length )
    {
        free( buffer );
        return ESP_FAIL;
    }

    cJSON *root = cJSON_Parse( buffer );
    free( buffer );
    if( NULL == root )
    {
        quarantine_config();
        return ESP_ERR_INVALID_RESPONSE;
    }

    const cJSON *version = cJSON_GetObjectItemCaseSensitive( root, "version" );
    if( !cJSON_IsNumber( version ) || ( version->valueint != (int)RFSCOUT_CONFIG_SCHEMA_VERSION ) )
    {
        cJSON_Delete( root );
        quarantine_config();
        return ESP_ERR_INVALID_VERSION;
    }

    load_display( root );
    load_apps( root );
    load_logging( root );
    load_notifications( root );
    load_recorder( root );
    load_timezones( root );
    load_saved_time( root );
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

/** @brief Build the canonical JSON representation for the current configuration. */
static cJSON *build_json( bool a_update_time, int64_t a_epoch, const char *a_time_source )
{
    static const char *app_names[ RFSCOUT_APP_COUNT ] = {
        "wifi_bt", "cc1101", "nrf24", "audio", "thermal", "device" };

    if( a_update_time )
    {
        saved_time_valid =
            ( a_epoch >= CONFIG_MIN_VALID_EPOCH ) && ( a_epoch <= CONFIG_MAX_VALID_EPOCH );
        saved_time_epoch = saved_time_valid ? a_epoch : 0;
        copy_string( saved_time_source,
                     sizeof( saved_time_source ),
                     saved_time_valid && ( NULL != a_time_source ) ? a_time_source : "" );
    }

    cJSON *root = cJSON_CreateObject();
    if( NULL == root )
    {
        return NULL;
    }
    cJSON_AddNumberToObject( root, "version", RFSCOUT_CONFIG_SCHEMA_VERSION );

    cJSON *display = cJSON_AddObjectToObject( root, "display" );
    cJSON_AddNumberToObject( display, "brightness", config.brightness_percent );
    cJSON_AddBoolToObject( display, "clock_24h", config.clock_24h );

    cJSON *apps = cJSON_AddObjectToObject( root, "apps" );
    for( size_t index = 0; index < RFSCOUT_APP_COUNT; ++index )
    {
        cJSON_AddBoolToObject( apps, app_names[ index ], config.apps_enabled[ index ] );
    }

    cJSON *logging = cJSON_AddObjectToObject( root, "logging" );
    cJSON_AddBoolToObject( logging, "enabled", config.logging_enabled );

    cJSON *recorder = cJSON_AddObjectToObject( root, "recorder" );
    cJSON_AddNumberToObject( recorder, "volume", config.recorder_volume_percent );
    cJSON_AddNumberToObject( recorder, "input_gain_db", config.recorder_input_gain_db );

    cJSON *notifications = cJSON_AddObjectToObject( root, "notifications" );
    cJSON_AddStringToObject( notifications, "mode", alert_mode_name( config.alert_mode ) );
    cJSON_AddNumberToObject( notifications, "volume", config.alert_volume_percent );
    cJSON *characters = cJSON_AddObjectToObject( notifications, "characters" );
    for( size_t index = 0; index < RFSCOUT_NOTIFICATION_TYPE_COUNT; ++index )
    {
        const rfscout_notification_type_t type = (rfscout_notification_type_t)index;
        const char *key = alert_notification_config_key( type );
        if( NULL == key )
        {
            continue;
        }
        char value[ 2 ] = { config.notification_characters[ index ], '\0' };
        cJSON_AddStringToObject( characters, key, value );
    }

    cJSON *timezone = cJSON_AddObjectToObject( root, "timezone" );
    cJSON_AddStringToObject( timezone, "selected", config.timezone_selected );
    cJSON *options = cJSON_AddArrayToObject( timezone, "options" );
    for( size_t index = 0; index < config.timezone_count; ++index )
    {
        cJSON *entry = cJSON_CreateObject();
        cJSON_AddStringToObject( entry, "name", config.timezones[ index ].name );
        cJSON_AddStringToObject( entry, "label", config.timezones[ index ].label );
        cJSON_AddStringToObject( entry, "posix", config.timezones[ index ].posix );
        cJSON_AddItemToArray( options, entry );
    }

    if( saved_time_valid )
    {
        cJSON *time = cJSON_AddObjectToObject( root, "time" );
        cJSON_AddNumberToObject( time, "epoch", (double)saved_time_epoch );
        cJSON_AddStringToObject(
            time, "source", saved_time_source[ 0 ] ? saved_time_source : "config" );
    }
    return root;
}

/** @brief Replace the active configuration while retaining crash recovery. */
static esp_err_t replace_config_file( void )
{
    (void)unlink( CONFIG_BACKUP_PATH );

    if( ( 0 != rename( RFSCOUT_CONFIG_PATH, CONFIG_BACKUP_PATH ) ) && ( ENOENT != errno ) )
    {
        (void)unlink( CONFIG_TEMP_PATH );
        return ESP_FAIL;
    }

    if( 0 != rename( CONFIG_TEMP_PATH, RFSCOUT_CONFIG_PATH ) )
    {
        (void)rename( CONFIG_BACKUP_PATH, RFSCOUT_CONFIG_PATH );
        (void)unlink( CONFIG_TEMP_PATH );
        return ESP_FAIL;
    }

    (void)unlink( CONFIG_BACKUP_PATH );
    return ESP_OK;
}

esp_err_t device_config_init( void )
{
    set_defaults();
    if( !storage_is_available() )
    {
        return ESP_OK;
    }

    recover_interrupted_save();
    const esp_err_t result = load_file();
    if( ESP_OK == result )
    {
        ESP_LOGI( TAG, "loaded %s", RFSCOUT_CONFIG_PATH );
        return ESP_OK;
    }

    if( ESP_ERR_NOT_FOUND != result )
    {
        ESP_LOGW(
            TAG, "configuration invalid; regenerating defaults: %s", esp_err_to_name( result ) );
    }
    return device_config_save( false, 0, NULL );
}

const device_config_t *device_config_get( void )
{
    return &config;
}

bool device_config_app_enabled( rfscout_app_id_t a_app )
{
    return ( a_app < RFSCOUT_APP_COUNT ) && config.apps_enabled[ a_app ];
}

esp_err_t device_config_set_app_enabled( rfscout_app_id_t a_app, bool a_enabled )
{
    if( ( a_app >= RFSCOUT_APP_COUNT ) || ( RFSCOUT_APP_DEVICE == a_app ) )
    {
        return ESP_ERR_INVALID_ARG;
    }
    config.apps_enabled[ a_app ] = a_enabled;
    return ESP_OK;
}

esp_err_t device_config_set_logging_enabled( bool a_enabled )
{
    config.logging_enabled = a_enabled;
    return ESP_OK;
}

esp_err_t device_config_set_brightness( uint8_t a_percent )
{
    if( ( a_percent < 10U ) || ( a_percent > 100U ) )
    {
        return ESP_ERR_INVALID_ARG;
    }
    config.brightness_percent = a_percent;
    return ESP_OK;
}

esp_err_t device_config_set_clock_24h( bool a_enabled )
{
    config.clock_24h = a_enabled;
    return ESP_OK;
}

esp_err_t device_config_set_alert_mode( rfscout_alert_mode_t a_mode )
{
    if( a_mode >= RFSCOUT_ALERT_MODE_COUNT )
    {
        return ESP_ERR_INVALID_ARG;
    }
    config.alert_mode = a_mode;
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
    config.alert_volume_percent = a_percent;
    return ESP_OK;
}

esp_err_t device_config_set_recorder_volume( uint8_t a_percent )
{
    if( a_percent > 100U )
    {
        return ESP_ERR_INVALID_ARG;
    }
    config.recorder_volume_percent = a_percent;
    return ESP_OK;
}

esp_err_t device_config_set_recorder_input_gain( uint8_t a_gain_db )
{
    if( a_gain_db > 24U || 0U != ( a_gain_db % 3U ) )
    {
        return ESP_ERR_INVALID_ARG;
    }
    config.recorder_input_gain_db = a_gain_db;
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

    config.notification_characters[ a_type ] = character;
    return ESP_OK;
}

esp_err_t device_config_set_timezone( const char *a_name )
{
    if( NULL == a_name )
    {
        return ESP_ERR_INVALID_ARG;
    }
    for( size_t index = 0; index < config.timezone_count; ++index )
    {
        if( 0 == strcmp( config.timezones[ index ].name, a_name ) )
        {
            copy_string( config.timezone_selected, sizeof( config.timezone_selected ), a_name );
            return ESP_OK;
        }
    }
    return ESP_ERR_NOT_FOUND;
}

const device_timezone_t *device_config_timezone( void )
{
    for( size_t index = 0; index < config.timezone_count; ++index )
    {
        if( 0 == strcmp( config.timezones[ index ].name, config.timezone_selected ) )
        {
            return &config.timezones[ index ];
        }
    }
    return NULL;
}

bool device_config_saved_time( int64_t *a_epoch, const char **a_source )
{
    if( !saved_time_valid )
    {
        return false;
    }
    if( NULL != a_epoch )
    {
        *a_epoch = saved_time_epoch;
    }
    if( NULL != a_source )
    {
        *a_source = saved_time_source;
    }
    return true;
}

esp_err_t device_config_save( bool a_update_time, int64_t a_epoch, const char *a_time_source )
{
    if( !storage_is_available() )
    {
        return ESP_ERR_INVALID_STATE;
    }

    cJSON *root = build_json( a_update_time, a_epoch, a_time_source );
    if( NULL == root )
    {
        return ESP_ERR_NO_MEM;
    }
    char *text = cJSON_Print( root );
    cJSON_Delete( root );
    if( NULL == text )
    {
        return ESP_ERR_NO_MEM;
    }

    FILE *file = fopen( CONFIG_TEMP_PATH, "wb" );
    if( NULL == file )
    {
        free( text );
        return ESP_FAIL;
    }
    const size_t length = strlen( text );
    bool ok = ( length == fwrite( text, 1U, length, file ) );
    ok = ok && ( EOF != fputc( '\n', file ) );
    ok = ok && ( 0 == fflush( file ) );
    ok = ok && ( 0 == fsync( fileno( file ) ) );
    if( 0 != fclose( file ) )
    {
        ok = false;
    }
    free( text );

    if( !ok )
    {
        (void)unlink( CONFIG_TEMP_PATH );
        return ESP_FAIL;
    }
    return replace_config_file();
}
