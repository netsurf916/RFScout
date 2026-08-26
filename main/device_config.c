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

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define CONFIG_TEMP_PATH RFSCOUT_CONFIG_PATH ".tmp"
#define CONFIG_BAD_PATH RFSCOUT_CONFIG_PATH ".bad"
#define CONFIG_BRIGHTNESS_DEFAULT 70U

static const char *TAG = "device_config";
static device_config_t config;
static bool loaded_from_sd;
static bool saved_time_valid;
static int64_t saved_time_epoch;
static char saved_time_source[ 24 ];

static void copy_string( char *a_destination, size_t a_capacity, const char *a_source )
{
    if( ( NULL == a_destination ) || ( 0U == a_capacity ) )
    {
        return;
    }
    snprintf( a_destination, a_capacity, "%s", ( NULL != a_source ) ? a_source : "" );
}

static void timezone_default( size_t a_index,
                              const char *a_name,
                              const char *a_label,
                              const char *a_posix )
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
    config.logging_enabled = true;
    config.clock_24h = false;

    for( size_t index = 0; index < RFSCOUT_APP_COUNT; ++index )
    {
        config.apps_enabled[ index ] = true;
    }

    config.timezone_count = 2U;
    timezone_default( 0U, "America/Chicago", "US Central", "CST6CDT,M3.2.0,M11.1.0" );
    timezone_default( 1U, "UTC", "UTC", "UTC0" );
    copy_string( config.timezone_selected, sizeof( config.timezone_selected ), "America/Chicago" );

    loaded_from_sd = false;
    saved_time_valid = false;
    saved_time_epoch = 0;
    saved_time_source[ 0 ] = '\0';
}

static bool json_bool( const cJSON *a_parent, const char *a_name, bool a_default )
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive( a_parent, a_name );
    return cJSON_IsBool( item ) ? cJSON_IsTrue( item ) : a_default;
}

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
        config.apps_enabled[ index ] = json_bool( apps, names[ index ], config.apps_enabled[ index ] );
    }
    config.apps_enabled[ RFSCOUT_APP_DEVICE ] = true;
}

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

static void load_logging( const cJSON *a_root )
{
    const cJSON *logging = cJSON_GetObjectItemCaseSensitive( a_root, "logging" );
    if( cJSON_IsObject( logging ) )
    {
        config.logging_enabled = json_bool( logging, "enabled", config.logging_enabled );
    }
}

static void load_timezones( const cJSON *a_root )
{
    const cJSON *timezone = cJSON_GetObjectItemCaseSensitive( a_root, "timezone" );
    if( !cJSON_IsObject( timezone ) )
    {
        return;
    }

    const cJSON *selected = cJSON_GetObjectItemCaseSensitive( timezone, "selected" );
    if( cJSON_IsString( selected ) && ( NULL != selected->valuestring ) )
    {
        copy_string( config.timezone_selected, sizeof( config.timezone_selected ), selected->valuestring );
    }

    const cJSON *options = cJSON_GetObjectItemCaseSensitive( timezone, "options" );
    if( !cJSON_IsArray( options ) )
    {
        return;
    }

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

static void load_saved_time( const cJSON *a_root )
{
    const cJSON *time = cJSON_GetObjectItemCaseSensitive( a_root, "time" );
    if( !cJSON_IsObject( time ) )
    {
        return;
    }
    const cJSON *epoch = cJSON_GetObjectItemCaseSensitive( time, "epoch" );
    const cJSON *source = cJSON_GetObjectItemCaseSensitive( time, "source" );
    if( cJSON_IsNumber( epoch ) && ( epoch->valuedouble >= 1577836800.0 ) &&
        ( epoch->valuedouble <= 4102444800.0 ) )
    {
        saved_time_epoch = (int64_t)epoch->valuedouble;
        saved_time_valid = true;
        if( cJSON_IsString( source ) && ( NULL != source->valuestring ) )
        {
            copy_string( saved_time_source, sizeof( saved_time_source ), source->valuestring );
        }
    }
}

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
        (void)unlink( CONFIG_BAD_PATH );
        (void)rename( RFSCOUT_CONFIG_PATH, CONFIG_BAD_PATH );
        return ESP_ERR_INVALID_RESPONSE;
    }

    load_display( root );
    load_apps( root );
    load_logging( root );
    load_timezones( root );
    load_saved_time( root );
    cJSON_Delete( root );

    if( !timezone_selected_valid() )
    {
        copy_string( config.timezone_selected, sizeof( config.timezone_selected ), "UTC" );
    }
    loaded_from_sd = true;
    return ESP_OK;
}

static cJSON *build_json( bool a_include_time, int64_t a_epoch, const char *a_time_source )
{
    static const char *app_names[ RFSCOUT_APP_COUNT ] = {
        "wifi_bt", "cc1101", "nrf24", "audio", "thermal", "device" };

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

    if( a_include_time && ( a_epoch >= 1577836800LL ) )
    {
        cJSON *time = cJSON_AddObjectToObject( root, "time" );
        cJSON_AddNumberToObject( time, "epoch", (double)a_epoch );
        cJSON_AddStringToObject( time, "source", ( NULL != a_time_source ) ? a_time_source : "config" );
    }
    return root;
}

esp_err_t device_config_init( void )
{
    set_defaults();
    if( !storage_is_available() )
    {
        return ESP_OK;
    }
    const esp_err_t result = load_file();
    if( ESP_ERR_NOT_FOUND == result )
    {
        return device_config_save( false, 0, NULL );
    }
    if( ESP_OK != result )
    {
        ESP_LOGW( TAG, "configuration invalid; using defaults: %s", esp_err_to_name( result ) );
        return device_config_save( false, 0, NULL );
    }
    ESP_LOGI( TAG, "loaded %s", RFSCOUT_CONFIG_PATH );
    return ESP_OK;
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

esp_err_t device_config_save( bool a_include_time, int64_t a_epoch, const char *a_time_source )
{
    if( !storage_is_available() )
    {
        return ESP_ERR_INVALID_STATE;
    }
    cJSON *root = build_json( a_include_time, a_epoch, a_time_source );
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
    const bool ok = ( length == fwrite( text, 1U, length, file ) ) &&
                    ( EOF != fputc( '\n', file ) ) && ( 0 == fflush( file ) ) &&
                    ( 0 == fsync( fileno( file ) ) ) && ( 0 == fclose( file ) );
    free( text );
    if( !ok )
    {
        (void)unlink( CONFIG_TEMP_PATH );
        return ESP_FAIL;
    }
    if( 0 != rename( CONFIG_TEMP_PATH, RFSCOUT_CONFIG_PATH ) )
    {
        (void)unlink( CONFIG_TEMP_PATH );
        return ESP_FAIL;
    }
    loaded_from_sd = true;
    return ESP_OK;
}
