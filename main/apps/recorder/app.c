/**
    apps/recorder/app.c : Recorder application descriptor, runtime, and semantic rollback
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "app_descriptor.h"

#include "app_framework.h"
#include "audio_view.h"
#include "device_config.h"
#include "event.h"
#include "recorder.h"
#include "storage.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <stddef.h>

#define RECORDER_REFRESH_INTERVAL_US 100000LL
#define RECORDER_DEPENDENCY_INTERVAL_US 500000LL
#define RECORDER_QUIESCE_TIMEOUT_MS 5000U

#define AUDIO_BROWSER_BACK 0U
#define AUDIO_BROWSER_NEW 1U
#define AUDIO_BROWSER_MONITOR 2U
#define AUDIO_BROWSER_FILE_BASE 3U

static const char *TAG = "recorder_app";
static bool microphone_probed;
static esp_err_t microphone_result = ESP_ERR_NOT_FOUND;
static bool menu_active;
static bool menu_editing;
static bool delete_confirm;
static unsigned menu_item;
static rfscout_recorder_state_t last_state = RFSCOUT_RECORDER_IDLE;
static int64_t last_refresh_us;
static int64_t last_dependency_probe_us;

static struct
{
    rfscout_recorder_state_t state;
    unsigned item;
    uint8_t gain;
    uint8_t volume;
} rollback_state;

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

static void render_landing( void )
{
    audio_view_render( NULL, NULL );
}

static void render_menu( void )
{
    audio_view_menu( menu_item, menu_editing, delete_confirm );
}

static void save_volume( void )
{
    const esp_err_t result = device_config_save( false, 0, NULL );
    if( ESP_OK != result )
    {
        ESP_LOGW( TAG, "could not save Recorder volume: %s", esp_err_to_name( result ) );
    }
}

static void close_menu( void )
{
    menu_active = false;
    menu_editing = false;
    delete_confirm = false;
    menu_item = AUDIO_BROWSER_BACK;
    render_landing();
}

static void handle_menu_input( const rfscout_input_event_t *a_input )
{
    if( RFSCOUT_INPUT_BACK == a_input->type )
    {
        close_menu();
        emit_event( RFSCOUT_EVENT_BACK, 0 );
        return;
    }

    const rfscout_recorder_state_t state = recorder_state();
    if( menu_editing )
    {
        if( RFSCOUT_INPUT_NAVIGATION == a_input->type )
        {
            delete_confirm = !delete_confirm;
            render_menu();
            emit_event( RFSCOUT_EVENT_NAVIGATE, ( a_input->value > 0 ) ? 1 : -1 );
            return;
        }
        if( RFSCOUT_INPUT_SELECT == a_input->type )
        {
            bool activated = false;
            if( delete_confirm && ( menu_item >= AUDIO_BROWSER_FILE_BASE ) )
            {
                const size_t index = menu_item - AUDIO_BROWSER_FILE_BASE;
                const esp_err_t result = recorder_delete_file( index );
                activated = ( ESP_OK == result );
                if( ESP_OK != result )
                {
                    ESP_LOGW( TAG, "could not delete recording: %s", esp_err_to_name( result ) );
                }
                const unsigned item_count =
                    AUDIO_BROWSER_FILE_BASE + (unsigned)recorder_file_count();
                if( menu_item >= item_count )
                {
                    menu_item =
                        item_count > AUDIO_BROWSER_FILE_BASE ? item_count - 1U : AUDIO_BROWSER_NEW;
                }
            }
            menu_editing = false;
            delete_confirm = false;
            render_menu();
            emit_event( activated ? RFSCOUT_EVENT_ACTIVATE : RFSCOUT_EVENT_BACK, 0 );
        }
        return;
    }

    if( RFSCOUT_RECORDER_RECORDING == state )
    {
        if( ( RFSCOUT_INPUT_SELECT == a_input->type ) ||
            ( RFSCOUT_INPUT_LONG_SELECT == a_input->type ) )
        {
            recorder_request_stop_recording();
            last_state = RFSCOUT_RECORDER_SAVING;
            render_menu();
            emit_event( RFSCOUT_EVENT_ACTIVATE, 0 );
        }
        return;
    }
    if( RFSCOUT_RECORDER_SAVING == state )
    {
        return;
    }
    if( RFSCOUT_RECORDER_MONITORING == state )
    {
        if( ( RFSCOUT_INPUT_SELECT == a_input->type ) ||
            ( RFSCOUT_INPUT_LONG_SELECT == a_input->type ) )
        {
            recorder_stop_monitor();
            save_volume();
            render_menu();
            emit_event( RFSCOUT_EVENT_ACTIVATE, 0 );
            return;
        }
        if( RFSCOUT_INPUT_NAVIGATION == a_input->type )
        {
            const device_config_t *config = device_config_get();
            const int old_gain = ( NULL != config ) ? (int)config->recorder_input_gain_db : 12;
            int gain = old_gain + ( ( a_input->value > 0 ) ? 3 : -3 );
            if( gain < 0 )
            {
                gain = 0;
            }
            if( gain > 24 )
            {
                gain = 24;
            }
            if( ( gain != old_gain ) &&
                ( ESP_OK == device_config_set_recorder_input_gain( (uint8_t)gain ) ) )
            {
                recorder_set_input_gain_db( (uint8_t)gain );
                (void)device_config_save( false, 0, NULL );
                emit_event( RFSCOUT_EVENT_NAVIGATE, ( a_input->value > 0 ) ? 1 : -1 );
            }
        }
        render_menu();
        return;
    }
    if( RFSCOUT_RECORDER_PLAYING == state )
    {
        if( RFSCOUT_INPUT_SELECT == a_input->type )
        {
            recorder_stop();
            save_volume();
            emit_event( RFSCOUT_EVENT_ACTIVATE, 0 );
        }
        else if( RFSCOUT_INPUT_NAVIGATION == a_input->type )
        {
            const device_config_t *config = device_config_get();
            const int old_volume = ( NULL != config ) ? (int)config->recorder_volume_percent : 50;
            int volume = old_volume + ( ( a_input->value > 0 ) ? 5 : -5 );
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
                emit_event( RFSCOUT_EVENT_NAVIGATE, ( a_input->value > 0 ) ? 1 : -1 );
            }
        }
        render_menu();
        return;
    }

    if( ( RFSCOUT_INPUT_LONG_SELECT == a_input->type ) && ( menu_item >= AUDIO_BROWSER_FILE_BASE ) )
    {
        menu_editing = true;
        delete_confirm = false;
        render_menu();
        emit_event( RFSCOUT_EVENT_ENTER, 0 );
        return;
    }
    if( RFSCOUT_INPUT_SELECT == a_input->type )
    {
        if( AUDIO_BROWSER_BACK == menu_item )
        {
            close_menu();
            emit_event( RFSCOUT_EVENT_BACK, 0 );
            return;
        }
        if( AUDIO_BROWSER_NEW == menu_item )
        {
            const device_config_t *config = device_config_get();
            recorder_set_input_gain_db( NULL != config ? config->recorder_input_gain_db : 12U );
            if( ESP_OK == recorder_start_recording() )
            {
                emit_event( RFSCOUT_EVENT_ACTIVATE, 0 );
            }
            render_menu();
            return;
        }
        if( AUDIO_BROWSER_MONITOR == menu_item )
        {
            const device_config_t *config = device_config_get();
            if( ESP_OK ==
                recorder_start_monitor( NULL != config ? config->recorder_input_gain_db : 12U ) )
            {
                emit_event( RFSCOUT_EVENT_ACTIVATE, 0 );
            }
            render_menu();
            return;
        }
        const size_t index = menu_item - AUDIO_BROWSER_FILE_BASE;
        const device_config_t *config = device_config_get();
        const uint8_t volume = ( NULL != config ) ? config->recorder_volume_percent : 50U;
        if( ESP_OK == recorder_start_playback( index, volume ) )
        {
            emit_event( RFSCOUT_EVENT_ACTIVATE, 0 );
        }
        render_menu();
        return;
    }
    if( RFSCOUT_INPUT_NAVIGATION != a_input->type )
    {
        return;
    }

    const unsigned item_count = AUDIO_BROWSER_FILE_BASE + (unsigned)recorder_file_count();
    int item = (int)menu_item + ( ( a_input->value > 0 ) ? 1 : -1 );
    if( item < 0 )
    {
        item = (int)item_count - 1;
    }
    if( item >= (int)item_count )
    {
        item = 0;
    }
    if( (unsigned)item != menu_item )
    {
        menu_item = (unsigned)item;
        render_menu();
        emit_event( RFSCOUT_EVENT_NAVIGATE, ( a_input->value > 0 ) ? 1 : -1 );
    }
}

static bool dependency_lost( int64_t a_now_us )
{
    const rfscout_recorder_state_t state = recorder_state();
    if( ( RFSCOUT_RECORDER_ERROR == state ) || ( RFSCOUT_RECORDER_NO_STORAGE == state ) )
    {
        return true;
    }
    if( ( RFSCOUT_RECORDER_IDLE != state ) && ( RFSCOUT_RECORDER_MONITORING != state ) )
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

static void poll_menu( int64_t a_now_us )
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
            menu_item = recorder_file_count() > 0U ? AUDIO_BROWSER_FILE_BASE : AUDIO_BROWSER_NEW;
        }
        last_state = current;
        last_refresh_us = a_now_us;
        render_menu();
        return;
    }

    if( a_now_us - last_refresh_us >= RECORDER_REFRESH_INTERVAL_US )
    {
        last_refresh_us = a_now_us;
        if( ( RFSCOUT_RECORDER_RECORDING == current ) || ( RFSCOUT_RECORDER_SAVING == current ) ||
            ( RFSCOUT_RECORDER_MONITORING == current ) || ( RFSCOUT_RECORDER_PLAYING == current ) )
        {
            render_menu();
        }
    }
}

static bool worker_references_clear( void )
{
    return ESP_ERR_INVALID_STATE != recorder_probe_microphone();
}

static bool wait_for_workers( uint32_t a_timeout_ms )
{
    const int64_t deadline_us = esp_timer_get_time() + (int64_t)a_timeout_ms * 1000LL;
    do
    {
        if( worker_references_clear() )
        {
            return true;
        }
        vTaskDelay( pdMS_TO_TICKS( 10U ) );
    } while( esp_timer_get_time() < deadline_us );

    return worker_references_clear();
}

static esp_err_t prepare_leave( void )
{
    const device_config_t *config = device_config_get();
    rollback_state.state = recorder_state();
    rollback_state.item = menu_item;
    rollback_state.gain = recorder_input_gain_db();
    rollback_state.volume = NULL != config ? config->recorder_volume_percent : 50U;
    return ESP_OK;
}

static esp_err_t reactivate_leave( void )
{
    if( ( RFSCOUT_RECORDER_RECORDING != rollback_state.state ) &&
        ( RFSCOUT_RECORDER_MONITORING != rollback_state.state ) &&
        ( RFSCOUT_RECORDER_PLAYING != rollback_state.state ) )
    {
        return ESP_OK;
    }
    if( recorder_state() == rollback_state.state )
    {
        return ESP_OK;
    }

    if( !recorder_prepare_storage_shutdown( RECORDER_QUIESCE_TIMEOUT_MS ) )
    {
        ESP_LOGE( TAG, "Recorder output/storage work did not quiesce before abort reactivation" );
        return ESP_ERR_TIMEOUT;
    }
    if( !wait_for_workers( RECORDER_QUIESCE_TIMEOUT_MS ) )
    {
        ESP_LOGE( TAG, "Recorder could not drain workers before abort reactivation" );
        return ESP_ERR_TIMEOUT;
    }

    if( RFSCOUT_RECORDER_IDLE != recorder_state() )
    {
        const esp_err_t refresh_result = recorder_refresh();
        if( ESP_OK != refresh_result )
        {
            return refresh_result;
        }
    }

    esp_err_t result = ESP_ERR_INVALID_STATE;
    if( RFSCOUT_RECORDER_RECORDING == rollback_state.state )
    {
        recorder_set_input_gain_db( rollback_state.gain );
        result = recorder_start_recording();
        if( ESP_OK == result )
        {
            ESP_LOGW( TAG, "Recorder restarted recording after aborted app transition" );
        }
    }
    else if( RFSCOUT_RECORDER_MONITORING == rollback_state.state )
    {
        result = recorder_start_monitor( rollback_state.gain );
    }
    else if( RFSCOUT_RECORDER_PLAYING == rollback_state.state )
    {
        if( rollback_state.item < AUDIO_BROWSER_FILE_BASE )
        {
            return ESP_ERR_INVALID_STATE;
        }
        result = recorder_start_playback( rollback_state.item - AUDIO_BROWSER_FILE_BASE,
                                          rollback_state.volume );
    }

    if( ESP_OK == result )
    {
        last_state = rollback_state.state;
    }
    return result;
}

static rfscout_app_runtime_result_t recorder_runtime( const rfscout_app_runtime_event_t *a_event )
{
    if( NULL == a_event )
    {
        return RFSCOUT_APP_RUNTIME_IGNORED;
    }

    if( RFSCOUT_APP_RUNTIME_ENTER == a_event->type )
    {
        menu_active = false;
        menu_editing = false;
        delete_confirm = false;
        menu_item = AUDIO_BROWSER_BACK;
        last_state = RFSCOUT_RECORDER_IDLE;
        last_refresh_us = 0;
        last_dependency_probe_us = 0;
        if( ESP_OK != recorder_init() )
        {
            ESP_LOGW( TAG, "Recorder initialization unavailable" );
        }
        render_landing();
        return RFSCOUT_APP_RUNTIME_HANDLED;
    }
    if( RFSCOUT_APP_RUNTIME_LEAVE == a_event->type )
    {
        menu_active = false;
        menu_editing = false;
        delete_confirm = false;
        menu_item = AUDIO_BROWSER_BACK;
        return RFSCOUT_APP_RUNTIME_HANDLED;
    }
    if( RFSCOUT_APP_RUNTIME_INPUT == a_event->type )
    {
        if( menu_active )
        {
            handle_menu_input( &a_event->input );
            return RFSCOUT_APP_RUNTIME_HANDLED;
        }
        if( RFSCOUT_INPUT_SELECT == a_event->input.type )
        {
            menu_active = true;
            menu_item = AUDIO_BROWSER_BACK;
            (void)recorder_refresh();
            render_menu();
            emit_event( RFSCOUT_EVENT_ENTER, 0 );
            return RFSCOUT_APP_RUNTIME_HANDLED;
        }
        return RFSCOUT_APP_RUNTIME_IGNORED;
    }
    if( RFSCOUT_APP_RUNTIME_POLL != a_event->type )
    {
        return RFSCOUT_APP_RUNTIME_HANDLED;
    }

    if( dependency_lost( a_event->now_us ) )
    {
        app_framework_report_availability( RFSCOUT_APP_AUDIO,
                                           RFSCOUT_APP_AVAILABILITY_UNAVAILABLE );
        ESP_LOGW( TAG, "Recorder dependency lost; returning Home" );
        return RFSCOUT_APP_RUNTIME_REQUEST_HOME;
    }
    if( menu_active )
    {
        poll_menu( a_event->now_us );
    }
    else if( a_event->now_us - last_refresh_us >= RECORDER_REFRESH_INTERVAL_US )
    {
        last_refresh_us = a_event->now_us;
        render_landing();
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
    .reactivate_leave = reactivate_leave,
};
