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

#define SYSTEM_LOG_DRAIN_MS 3000U
#define SYSTEM_BREADCRUMB_DRAIN_MS 3000U
#define SYSTEM_RECORDER_DRAIN_MS 5000U
static const char *TAG = "system_lifecycle";
static bool writers_quiesced;

static void restore_writers( void )
{
    if( !media_store_available() )
    {
        return;
    }
    const esp_err_t breadcrumbs_result = breadcrumbs_init();
    if( ESP_OK != breadcrumbs_result && ESP_ERR_NOT_SUPPORTED != breadcrumbs_result )
    {
        ESP_LOGE( TAG, "breadcrumb restart failed: %s", esp_err_to_name( breadcrumbs_result ) );
    }
    if( device_config_get()->logging_enabled )
    {
        const esp_err_t logging_result = log_writer_init();
        if( ESP_OK != logging_result )
        {
            ESP_LOGE( TAG, "logger restart failed: %s", esp_err_to_name( logging_result ) );
        }
    }
}

bool system_lifecycle_prepare( system_lifecycle_target_t a_target )
{
    (void)a_target;
    writers_quiesced = false;
    const esp_err_t saved = device_config_save_if_dirty();
    if( ESP_OK != saved )
    {
        ESP_LOGW( TAG, "configuration save failed: %s", esp_err_to_name( saved ) );
        return false;
    }
    if( !media_store_available() )
    {
        return true;
    }
    const rfscout_recorder_state_t recorder = recorder_state();
    if( RFSCOUT_RECORDER_RECORDING == recorder || RFSCOUT_RECORDER_SAVING == recorder )
    {
        ESP_LOGW( TAG, "transition vetoed by active Recorder" );
        return false;
    }
    if( !recorder_prepare_storage_shutdown( SYSTEM_RECORDER_DRAIN_MS ) )
    {
        ESP_LOGW( TAG, "Recorder shutdown failed" );
        return false;
    }
    if( !breadcrumbs_shutdown( SYSTEM_BREADCRUMB_DRAIN_MS ) )
    {
        ESP_LOGW( TAG, "breadcrumb shutdown failed" );
        return false;
    }
    writers_quiesced = true;
    if( !log_writer_shutdown( SYSTEM_LOG_DRAIN_MS ) )
    {
        ESP_LOGW( TAG, "logger shutdown failed" );
        restore_writers();
        writers_quiesced = false;
        return false;
    }
    return true;
}

void system_lifecycle_abort( system_lifecycle_target_t a_target )
{
    (void)a_target;
    if( writers_quiesced )
    {
        restore_writers();
        writers_quiesced = false;
    }
}
