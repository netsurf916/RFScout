/**
    apps/recorder/app.c : RFScout Recorder application runtime
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "app_descriptor.h"

#include "app_framework.h"
#include "device_config.h"
#include "event.h"
#include "recorder.h"
#include "recorder_view.h"
#include "storage.h"

#include "esp_log.h"

#include <stddef.h>

#define RECORDER_REFRESH_INTERVAL_US 100000LL
#define RECORDER_DEPENDENCY_INTERVAL_US 500000LL

#define RECORDER_BROWSER_BACK 0U
#define RECORDER_BROWSER_NEW 1U
#define RECORDER_BROWSER_FILE_BASE 2U

static const char *TAG = "recorder_app";
static bool microphone_probed;
static esp_err_t microphone_result = ESP_ERR_NOT_FOUND;
static bool browser_active;
static bool delete_mode;
static bool delete_confirm;
static unsigned browser_item;
static rfscout_recorder_state_t last_state = RFSCOUT_RECORDER_IDLE;
static int64_t last_refresh_us;
static int64_t last_dependency_probe_us;

static void emit_event( rfscout_event_type_t a_type, int32_t a_value )
{
    rfscout_event_emit( a_type, (uint8_t)RFSCOUT_APP_AUDIO, a_value );
}

static esp_err_t probe_recorder( void )
{
    if( !storage_is_available() || ( ESP_OK != storage_probe_card() ) )
    {
        return ESP_ERR_NOT_FOUND;
    }
    if( !microphone_probed )
    {
        microphone_result = recorder_probe_microphone();
        microphone_probed = true;
    }
    return microphone_result;
}

static void render( void )
{
    if( browser_active )
    {
        recorder_view_browser( browser_item, delete_mode, delete_confirm );
    }
    else
    {
        recorder_view_landing();
    }
}

static void save_volume( void )
{
    const esp_err_t result = device_config_save( false, 0, NULL );
    if( ESP_OK != result )
    {
        ESP_LOGW( TAG, "could not save Recorder volume: %s", esp_err_to_name( result ) );
    }
}

static void stop_playback( void )
{
    recorder_stop();
    save_volume();
    render();
}

static bool handle_active_operation( const rfscout_input_event_t *a_input )
{
    const rfscout_recorder_state_t state = recorder_state();
    if( RFSCOUT_RECORDER_RECORDING == state )
    {
        if( ( RFSCOUT_INPUT_SELECT == a_input->type ) ||
            ( RFSCOUT_INPUT_LONG_SELECT == a_input->type ) )
        {
            recorder_request_stop_recording();
            last_state = RFSCOUT_RECORDER_SAVING;
            emit_event( RFSCOUT_EVENT_ACTIVATE, 0 );
        }
        render();
        return true;
    }
    if( RFSCOUT_RECORDER_SAVING == state )
    {
        render();
        return true;
    }
    if( RFSCOUT_RECORDER_PLAYING == state )
    {
        if( ( RFSCOUT_INPUT_SELECT == a_input->type ) || ( RFSCOUT_INPUT_BACK == a_input->type ) )
        {
            stop_playback();
            emit_event( RFSCOUT_EVENT_BACK, 0 );
            return true;
        }
        if( RFSCOUT_INPUT_NAVIGATION == a_input->type )
        {
            const device_config_t *config = device_config_get();
            const int old_volume = NULL != config ? (int)config->recorder_volume_percent : 50;
            int volume = old_volume + ( a_input->value > 0 ? 5 : -5 );
            if( volume < 0 )
            {
                volume = 0;
            }
            else if( volume > 100 )
            {
                volume = 100;
            }
            if( ( volume != old_volume ) &&
                ( ESP_OK == device_config_set_recorder_volume( (uint8_t)volume ) ) )
            {
                recorder_set_playback_volume( (uint8_t)volume );
                emit_event( RFSCOUT_EVENT_NAVIGATE, a_input->value > 0 ? 1 : -1 );
            }
            render();
            return true;
        }
        return true;
    }
    return false;
}

static void close_browser( void )
{
    browser_active = false;
    delete_mode = false;
    delete_confirm = false;
    browser_item = RECORDER_BROWSER_BACK;
    render();
}

static void handle_browser_input( const rfscout_input_event_t *a_input )
{
    if( handle_active_operation( a_input ) )
    {
        return;
    }

    if( delete_mode )
    {
        if( RFSCOUT_INPUT_BACK == a_input->type )
        {
            delete_mode = false;
            delete_confirm = false;
            render();
            return;
        }
        if( RFSCOUT_INPUT_NAVIGATION == a_input->type )
        {
            delete_confirm = !delete_confirm;
            render();
            return;
        }
        if( RFSCOUT_INPUT_SELECT == a_input->type )
        {
            if( delete_confirm && ( browser_item >= RECORDER_BROWSER_FILE_BASE ) )
            {
                const size_t index = (size_t)( browser_item - RECORDER_BROWSER_FILE_BASE );
                const esp_err_t result = recorder_delete_file( index );
                if( ESP_OK != result )
                {
                    ESP_LOGW( TAG, "could not delete recording: %s", esp_err_to_name( result ) );
                }
                const unsigned count = RECORDER_BROWSER_FILE_BASE + (unsigned)recorder_file_count();
                if( browser_item >= count )
                {
                    browser_item = count > RECORDER_BROWSER_FILE_BASE ? count - 1U
                                                                      : RECORDER_BROWSER_NEW;
                }
            }
            delete_mode = false;
            delete_confirm = false;
            render();
        }
        return;
    }

    if( RFSCOUT_INPUT_BACK == a_input->type )
    {
        close_browser();
        emit_event( RFSCOUT_EVENT_BACK, 0 );
        return;
    }
    if( ( RFSCOUT_INPUT_LONG_SELECT == a_input->type ) &&
        ( browser_item >= RECORDER_BROWSER_FILE_BASE ) )
    {
        delete_mode = true;
        delete_confirm = false;
        render();
        emit_event( RFSCOUT_EVENT_ENTER, 0 );
        return;
    }
    if( RFSCOUT_INPUT_SELECT == a_input->type )
    {
        if( RECORDER_BROWSER_BACK == browser_item )
        {
            close_browser();
            emit_event( RFSCOUT_EVENT_BACK, 0 );
            return;
        }
        if( RECORDER_BROWSER_NEW == browser_item )
        {
            const device_config_t *config = device_config_get();
            recorder_set_input_gain_db( NULL != config ? config->recorder_input_gain_db : 12U );
            if( ESP_OK != recorder_start_recording() )
            {
                ESP_LOGW( TAG, "could not start recording" );
            }
            render();
            return;
        }

        const size_t index = (size_t)( browser_item - RECORDER_BROWSER_FILE_BASE );
        const device_config_t *config = device_config_get();
        const uint8_t volume = NULL != config ? config->recorder_volume_percent : 50U;
        if( ESP_OK != recorder_start_playback( index, volume ) )
        {
            ESP_LOGW( TAG, "could not start playback" );
        }
        render();
        return;
    }
    if( RFSCOUT_INPUT_NAVIGATION != a_input->type )
    {
        return;
    }

    const unsigned count = RECORDER_BROWSER_FILE_BASE + (unsigned)recorder_file_count();
    int next = (int)browser_item + ( a_input->value > 0 ? 1 : -1 );
    if( next < 0 )
    {
        next = (int)count - 1;
    }
    else if( next >= (int)count )
    {
        next = 0;
    }
    browser_item = (unsigned)next;
    render();
    emit_event( RFSCOUT_EVENT_NAVIGATE, a_input->value > 0 ? 1 : -1 );
}

static bool dependency_lost( int64_t a_now_us )
{
    const rfscout_recorder_state_t state = recorder_state();
    if( ( RFSCOUT_RECORDER_ERROR == state ) || ( RFSCOUT_RECORDER_NO_STORAGE == state ) )
    {
        return true;
    }
    if( RFSCOUT_RECORDER_IDLE != state )
    {
        return false;
    }
    if( a_now_us - last_dependency_probe_us < RECORDER_DEPENDENCY_INTERVAL_US )
    {
        return false;
    }
    last_dependency_probe_us = a_now_us;
    return ESP_OK != storage_probe_card();
}

static esp_err_t prepare_leave( void )
{
    const rfscout_recorder_state_t state = recorder_state();
    if( ( RFSCOUT_RECORDER_RECORDING == state ) || ( RFSCOUT_RECORDER_SAVING == state ) )
    {
        /* Never reconstruct an interrupted recording. Finish it before navigation is allowed. */
        return ESP_ERR_INVALID_STATE;
    }
    return ESP_OK;
}

static void poll_recorder( int64_t a_now_us )
{
    const rfscout_recorder_state_t current = recorder_state();
    if( current != last_state )
    {
        if( ( RFSCOUT_RECORDER_PLAYING == last_state ) && ( RFSCOUT_RECORDER_IDLE == current ) )
        {
            save_volume();
        }
        if( ( RFSCOUT_RECORDER_SAVING == last_state ) && ( RFSCOUT_RECORDER_IDLE == current ) )
        {
            browser_item = recorder_file_count() > 0U ? RECORDER_BROWSER_FILE_BASE
                                                       : RECORDER_BROWSER_NEW;
        }
        last_state = current;
        last_refresh_us = a_now_us;
        render();
        return;
    }
    if( a_now_us - last_refresh_us >= RECORDER_REFRESH_INTERVAL_US )
    {
        last_refresh_us = a_now_us;
        render();
    }
}

static rfscout_app_runtime_result_t recorder_runtime( const rfscout_app_runtime_event_t *a_event )
{
    if( NULL == a_event )
    {
        return RFSCOUT_APP_RUNTIME_IGNORED;
    }
    if( RFSCOUT_APP_RUNTIME_ENTER == a_event->type )
    {
        browser_active = false;
        delete_mode = false;
        delete_confirm = false;
        browser_item = RECORDER_BROWSER_BACK;
        last_state = RFSCOUT_RECORDER_IDLE;
        last_refresh_us = 0;
        last_dependency_probe_us = 0;
        if( ESP_OK != recorder_init() )
        {
            ESP_LOGW( TAG, "Recorder initialization unavailable" );
        }
        render();
        return RFSCOUT_APP_RUNTIME_HANDLED;
    }
    if( RFSCOUT_APP_RUNTIME_LEAVE == a_event->type )
    {
        browser_active = false;
        delete_mode = false;
        delete_confirm = false;
        return RFSCOUT_APP_RUNTIME_HANDLED;
    }
    if( ( RFSCOUT_APP_RUNTIME_INPUT == a_event->type ) ||
        ( RFSCOUT_APP_RUNTIME_POLL == a_event->type ) )
    {
        if( dependency_lost( a_event->now_us ) )
        {
            app_framework_report_availability( RFSCOUT_APP_AUDIO,
                                               RFSCOUT_APP_AVAILABILITY_UNAVAILABLE );
            return RFSCOUT_APP_RUNTIME_REQUEST_HOME;
        }
    }
    if( RFSCOUT_APP_RUNTIME_INPUT == a_event->type )
    {
        if( browser_active || handle_active_operation( &a_event->input ) )
        {
            if( browser_active )
            {
                handle_browser_input( &a_event->input );
            }
            return RFSCOUT_APP_RUNTIME_HANDLED;
        }
        if( RFSCOUT_INPUT_SELECT == a_event->input.type )
        {
            browser_active = true;
            browser_item = RECORDER_BROWSER_BACK;
            (void)recorder_refresh();
            render();
            emit_event( RFSCOUT_EVENT_ENTER, 0 );
            return RFSCOUT_APP_RUNTIME_HANDLED;
        }
        return RFSCOUT_APP_RUNTIME_IGNORED;
    }
    if( RFSCOUT_APP_RUNTIME_POLL == a_event->type )
    {
        poll_recorder( a_event->now_us );
    }
    return RFSCOUT_APP_RUNTIME_HANDLED;
}

const rfscout_app_descriptor_t rfscout_recorder_app = {
    .id = RFSCOUT_APP_AUDIO,
    .name = "Recorder",
    .probe_policy = RFSCOUT_APP_PROBE_HOTPLUG,
    .probe = probe_recorder,
    .requires_activation = false,
    .services = RFSCOUT_APP_SERVICE_RECORDER,
    .runtime = recorder_runtime,
    .prepare_leave = prepare_leave,
    .reactivate_leave = NULL,
};
