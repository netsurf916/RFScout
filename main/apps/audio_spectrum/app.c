/**
    apps/audio_spectrum/app.c : RFScout live audio spectrum application
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "app_descriptor.h"

#include "event.h"
#include "recorder.h"
#include "view.h"

#include "esp_log.h"

#include <stddef.h>

#define SPECTRUM_REFRESH_INTERVAL_US 100000LL
#define SPECTRUM_GAIN_MIN_DB 0U
#define SPECTRUM_GAIN_MAX_DB 48U
#define SPECTRUM_GAIN_STEP_DB 3U
#define SPECTRUM_GAIN_DEFAULT_DB 24U

static const char *TAG = "audio_spectrum_app";
static bool settings_active;
static bool monitor_started;
static uint8_t gain_db = SPECTRUM_GAIN_DEFAULT_DB;
static int64_t last_refresh_us;

static esp_err_t probe_audio_spectrum( void )
{
    return recorder_probe_microphone();
}

static void emit_event( rfscout_event_type_t a_type, int32_t a_value )
{
    rfscout_event_emit( a_type, (uint8_t)RFSCOUT_APP_AUDIO_SPECTRUM, a_value );
}

static void render( void )
{
    audio_spectrum_view_render( gain_db, settings_active );
}

static rfscout_app_runtime_result_t spectrum_runtime( const rfscout_app_runtime_event_t *a_event )
{
    if( NULL == a_event )
    {
        return RFSCOUT_APP_RUNTIME_IGNORED;
    }

    if( RFSCOUT_APP_RUNTIME_ENTER == a_event->type )
    {
        settings_active = false;
        monitor_started = false;
        last_refresh_us = 0;
        render();
        return RFSCOUT_APP_RUNTIME_HANDLED;
    }

    if( RFSCOUT_APP_RUNTIME_ACTIVATE == a_event->type )
    {
        /* The live monitor is microphone-only; it does not require Recorder storage. */
        const esp_err_t result = recorder_start_monitor( gain_db );
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
        settings_active = false;
        monitor_started = false;
        return RFSCOUT_APP_RUNTIME_HANDLED;
    }

    if( RFSCOUT_APP_RUNTIME_INPUT == a_event->type )
    {
        if( settings_active )
        {
            if( ( RFSCOUT_INPUT_SELECT == a_event->input.type ) ||
                ( RFSCOUT_INPUT_BACK == a_event->input.type ) )
            {
                settings_active = false;
                render();
                emit_event( RFSCOUT_EVENT_BACK, 0 );
                return RFSCOUT_APP_RUNTIME_HANDLED;
            }
            if( RFSCOUT_INPUT_NAVIGATION == a_event->input.type )
            {
                int next =
                    (int)gain_db + ( a_event->input.value > 0 ? (int)SPECTRUM_GAIN_STEP_DB
                                                              : -(int)SPECTRUM_GAIN_STEP_DB );
                if( next < (int)SPECTRUM_GAIN_MIN_DB )
                {
                    next = (int)SPECTRUM_GAIN_MIN_DB;
                }
                else if( next > (int)SPECTRUM_GAIN_MAX_DB )
                {
                    next = (int)SPECTRUM_GAIN_MAX_DB;
                }
                if( (uint8_t)next != gain_db )
                {
                    gain_db = (uint8_t)next;
                    recorder_set_input_gain_db( gain_db );
                    render();
                    emit_event( RFSCOUT_EVENT_NAVIGATE, a_event->input.value > 0 ? 1 : -1 );
                }
                return RFSCOUT_APP_RUNTIME_HANDLED;
            }
            return RFSCOUT_APP_RUNTIME_HANDLED;
        }

        if( RFSCOUT_INPUT_SELECT == a_event->input.type )
        {
            settings_active = true;
            render();
            emit_event( RFSCOUT_EVENT_ENTER, 0 );
            return RFSCOUT_APP_RUNTIME_HANDLED;
        }
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
        render();
    }
    return RFSCOUT_APP_RUNTIME_HANDLED;
}

const rfscout_app_descriptor_t rfscout_audio_spectrum_app = {
    .id = RFSCOUT_APP_AUDIO_SPECTRUM,
    .name = "Audio Spectrum",
    .probe_policy = RFSCOUT_APP_PROBE_STARTUP,
    .probe = probe_audio_spectrum,
    .requires_activation = true,
    .services = RFSCOUT_APP_SERVICE_AUDIO_CAPTURE,
    .runtime = spectrum_runtime,
    .prepare_leave = NULL,
    .reactivate_leave = NULL,
};