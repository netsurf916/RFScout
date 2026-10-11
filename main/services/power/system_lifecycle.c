/**
    system_lifecycle.c : terminal service transition coordinator
    Description: Framework-owned ordering and rollback of persistent storage clients.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "system_lifecycle.h"

#include "breadcrumbs.h"
#include "device_config.h"
#include "log_writer.h"
#include "media_store.h"
#include "recorder.h"

#include "esp_log.h"

#include <stddef.h>

#define SYSTEM_LOG_DRAIN_MS 3000U
#define SYSTEM_BREADCRUMB_DRAIN_MS 3000U
#define SYSTEM_RECORDER_DRAIN_MS 5000U

static const char *TAG = "system_lifecycle";

typedef struct
{
    const char *name;
    bool ( *prepare )( void );
    bool ( *quiesce )( void );
    void ( *abort )( void );
} system_lifecycle_participant_t;

static bool recorder_can_stop( void )
{
    const rfscout_recorder_state_t state = recorder_state();
    return RFSCOUT_RECORDER_RECORDING != state && RFSCOUT_RECORDER_SAVING != state;
}

static bool recorder_quiesce( void )
{
    return recorder_prepare_storage_shutdown( SYSTEM_RECORDER_DRAIN_MS );
}

static bool breadcrumbs_quiesce( void )
{
    return breadcrumbs_shutdown( SYSTEM_BREADCRUMB_DRAIN_MS );
}

static bool logging_quiesce( void )
{
    return log_writer_shutdown( SYSTEM_LOG_DRAIN_MS );
}

static void breadcrumbs_restore( void )
{
    const esp_err_t result = breadcrumbs_init();
    if( ESP_OK != result && ESP_ERR_NOT_SUPPORTED != result )
    {
        ESP_LOGE( TAG, "breadcrumb restart failed: %s", esp_err_to_name( result ) );
    }
}

static void logging_restore( void )
{
    if( device_config_get()->logging_enabled )
    {
        const esp_err_t result = log_writer_init();
        if( ESP_OK != result )
        {
            ESP_LOGE( TAG, "logger restart failed: %s", esp_err_to_name( result ) );
        }
    }
}

/* Producers stop before persistent writers. Reverse order restores writers first. */
static const system_lifecycle_participant_t participants[] = {
    { "recorder", recorder_can_stop, recorder_quiesce, NULL },
    { "breadcrumbs", NULL, breadcrumbs_quiesce, breadcrumbs_restore },
    { "logger", NULL, logging_quiesce, logging_restore },
};

#define PARTICIPANT_COUNT ( sizeof( participants ) / sizeof( participants[ 0 ] ) )

static size_t quiesced_count;
static bool storage_participating;

static void restore_quiesced( void )
{
    {
        while( quiesced_count > 0U )
        {
            --quiesced_count;
            if( NULL != participants[ quiesced_count ].abort )
            {
                participants[ quiesced_count ].abort();
            }
        }
    }
    quiesced_count = 0U;
    storage_participating = false;
}

bool system_lifecycle_prepare( system_lifecycle_target_t a_target )
{
    (void)a_target;
    if( 0U != quiesced_count )
    {
        ESP_LOGW( TAG, "system transition already prepared" );
        return false;
    }
    const esp_err_t saved = device_config_save_if_dirty();
    if( ESP_OK != saved )
    {
        ESP_LOGW( TAG, "configuration save failed: %s", esp_err_to_name( saved ) );
        return false;
    }
    storage_participating = media_store_available();
    if( !storage_participating )
    {
        return true;
    }

    /* Preflight every participant before any irreversible quiescence. */
    for( size_t index = 0U; index < PARTICIPANT_COUNT; ++index )
    {
        if( NULL != participants[ index ].prepare && !participants[ index ].prepare() )
        {
            ESP_LOGW( TAG, "transition vetoed by %s", participants[ index ].name );
            storage_participating = false;
            return false;
        }
    }

    for( size_t index = 0U; index < PARTICIPANT_COUNT; ++index )
    {
        if( NULL != participants[ index ].quiesce && !participants[ index ].quiesce() )
        {
            ESP_LOGW( TAG, "transition quiesce failed: %s", participants[ index ].name );
            restore_quiesced();
            return false;
        }
        ++quiesced_count;
    }
    return true;
}

void system_lifecycle_abort( system_lifecycle_target_t a_target )
{
    (void)a_target;
    restore_quiesced();
}
