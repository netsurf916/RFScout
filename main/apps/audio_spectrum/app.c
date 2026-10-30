/**
    apps/audio_spectrum/app.c : RFScout live audio spectrum application
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "app_descriptor.h"
#include "app_manager.h"

#include "recorder.h"
#include "display_idle.h"
#include "view.h"

#include "esp_log.h"

#include <stddef.h>
#include <string.h>

#define SPECTRUM_REFRESH_INTERVAL_US 100000LL

static const char *TAG = "audio_spectrum_app";
static bool monitor_started;
static int64_t last_refresh_us;
static uint8_t last_bins[ RFSCOUT_RECORDER_MONITOR_BINS ];
static size_t last_bin_count;
static bool bins_valid;

static esp_err_t probe_audio_spectrum( void )
{
    return recorder_probe_microphone();
}

static void render( void )
{
    if( !app_manager_rendering() )
    {
        app_manager_request_render( RFSCOUT_APP_AUDIO_SPECTRUM, true );
        return;
    }
    audio_spectrum_view_render();
}

static rfscout_app_runtime_result_t spectrum_runtime( const rfscout_app_runtime_event_t *a_event )
{
    if( NULL == a_event )
    {
        return RFSCOUT_APP_RUNTIME_IGNORED;
    }

    if( RFSCOUT_APP_RUNTIME_ENTER == a_event->type )
    {
        monitor_started = false;
        last_refresh_us = 0;
        bins_valid = false;
        display_idle_set_keep_awake( true );
        render();
        return RFSCOUT_APP_RUNTIME_HANDLED;
    }

    if( RFSCOUT_APP_RUNTIME_ACTIVATE == a_event->type )
    {
        /* The live monitor is microphone-only; it does not require Recorder storage. */
        const esp_err_t result = recorder_start_monitor();
        if( ESP_OK != result )
        {
            ESP_LOGW(
                TAG, "could not start audio spectrum monitor: %s", esp_err_to_name( result ) );
            return RFSCOUT_APP_RUNTIME_REQUEST_HOME;
        }
        monitor_started = true;
        render();
        return RFSCOUT_APP_RUNTIME_HANDLED;
    }

    if( RFSCOUT_APP_RUNTIME_LEAVE == a_event->type )
    {
        display_idle_set_keep_awake( false );
        monitor_started = false;
        return RFSCOUT_APP_RUNTIME_HANDLED;
    }

    if( RFSCOUT_APP_RUNTIME_RENDER == a_event->type )
    {
        render();
        return RFSCOUT_APP_RUNTIME_HANDLED;
    }
    if( RFSCOUT_APP_RUNTIME_INPUT == a_event->type )
    {
        return RFSCOUT_APP_RUNTIME_IGNORED;
    }

    if( RFSCOUT_APP_RUNTIME_POLL != a_event->type )
    {
        return RFSCOUT_APP_RUNTIME_HANDLED;
    }

    if( a_event->activated && !monitor_started )
    {
        return RFSCOUT_APP_RUNTIME_REQUEST_HOME;
    }
    if( monitor_started && ( RFSCOUT_RECORDER_MONITORING != recorder_state() ) )
    {
        ESP_LOGW( TAG, "audio spectrum monitor stopped unexpectedly" );
        return RFSCOUT_APP_RUNTIME_REQUEST_HOME;
    }
    if( a_event->now_us - last_refresh_us >= SPECTRUM_REFRESH_INTERVAL_US )
    {
        last_refresh_us = a_event->now_us;
        uint8_t bins[ RFSCOUT_RECORDER_MONITOR_BINS ] = { 0 };
        const size_t count = recorder_monitor_bins( bins, sizeof( bins ) );
        if( !bins_valid || count != last_bin_count ||
            0 != memcmp( bins, last_bins, sizeof( bins ) ) )
        {
            memcpy( last_bins, bins, sizeof( bins ) );
            last_bin_count = count;
            bins_valid = true;
            app_manager_request_render( RFSCOUT_APP_AUDIO_SPECTRUM, false );
        }
    }
    return RFSCOUT_APP_RUNTIME_HANDLED;
}

static bool app_telemetry( rfscout_log_record_t *a_record )
{
    bool success = log_uint( a_record, "state", recorder_state() ) &&
                   log_double( a_record, "range_db", recorder_monitor_range_db() );
    return success;
}

const rfscout_app_descriptor_t rfscout_audio_spectrum_app = {
    .id = RFSCOUT_APP_AUDIO_SPECTRUM,
    .name = "Audio Spectrum",
    .persistence_key = "audio_spectrum",
    .probe_policy = RFSCOUT_APP_PROBE_HOTPLUG,
    .probe = probe_audio_spectrum,
    .requires_activation = true,
    .services = RFSCOUT_APP_SERVICE_AUDIO_CAPTURE,
    .runtime = spectrum_runtime,
    .telemetry = app_telemetry,
    .prepare_leave = NULL,
};
