/**
    system_events.c : RFScout system-event history and notification policy
    Description: Records semantic system state, diagnostics, and transient attention presentation.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "system_events.h"

#include "app_framework.h"
#include "battery.h"
#include "display.h"
#include "display_idle.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"

#include <stdio.h>
#include <string.h>

#define SYSTEM_EVENT_NOTIFICATION_MS 3000U
#define SYSTEM_EVENT_NOTIFICATION_US ( (int64_t)SYSTEM_EVENT_NOTIFICATION_MS * 1000LL )

static const char *TAG = "system_events";
static rfscout_system_event_entry_t history[ RFSCOUT_SYSTEM_EVENT_HISTORY_CAPACITY ];
static size_t history_head;
static size_t history_count;
static rfscout_system_event_notification_t notification;
static int64_t notification_expires_us;
static bool notifications_enabled;
static bool attention_pending;
static bool power_state_known;
static bool external_power;
static portMUX_TYPE system_event_lock = portMUX_INITIALIZER_UNLOCKED;

static const char *app_name( uint8_t a_source )
{
    if( a_source >= (uint8_t)RFSCOUT_APP_COUNT )
    {
        return "HARDWARE";
    }
    const rfscout_app_descriptor_t *descriptor =
        app_framework_descriptor( (rfscout_app_id_t)a_source );
    return ( NULL != descriptor && NULL != descriptor->name ) ? descriptor->name : "HARDWARE";
}

static bool is_system_state_event( const rfscout_event_t *a_event )
{
    if( NULL == a_event )
    {
        return false;
    }

    switch( a_event->type )
    {
        case RFSCOUT_EVENT_POWER_CONNECTED:
        case RFSCOUT_EVENT_POWER_DISCONNECTED:
        case RFSCOUT_EVENT_STORAGE_MOUNTED:
        case RFSCOUT_EVENT_STORAGE_MOUNT_FAILED:
        case RFSCOUT_EVENT_STORAGE_UNMOUNTED:
        case RFSCOUT_EVENT_INTERNAL_STORAGE_MOUNTED:
        case RFSCOUT_EVENT_INTERNAL_STORAGE_FORMATTED:
        case RFSCOUT_EVENT_INTERNAL_STORAGE_MOUNT_FAILED:
        case RFSCOUT_EVENT_CONFIG_IMPORTED:
        case RFSCOUT_EVENT_CONFIG_DEFAULTED:
        case RFSCOUT_EVENT_APP_DISCOVERED:
        case RFSCOUT_EVENT_APP_AVAILABLE:
        case RFSCOUT_EVENT_APP_UNAVAILABLE:
        case RFSCOUT_EVENT_APP_PROBE_ERROR:
        case RFSCOUT_EVENT_DISPLAY_PROVIDER_SELECTED:
        case RFSCOUT_EVENT_INPUT_PROVIDER_SELECTED:
            return true;
        case RFSCOUT_EVENT_WARNING:
        case RFSCOUT_EVENT_ERROR:
            return RFSCOUT_EVENT_SOURCE_SYSTEM == a_event->source;
        default:
            return false;
    }
}

static rfscout_system_event_severity_t severity_for( const rfscout_event_t *a_event )
{
    if( NULL == a_event )
    {
        return RFSCOUT_SYSTEM_EVENT_INFO;
    }
    if( RFSCOUT_EVENT_ERROR == a_event->type )
    {
        return RFSCOUT_SYSTEM_EVENT_CRITICAL;
    }
    if( RFSCOUT_EVENT_WARNING == a_event->type ||
        RFSCOUT_EVENT_STORAGE_MOUNT_FAILED == a_event->type ||
        RFSCOUT_EVENT_INTERNAL_STORAGE_MOUNT_FAILED == a_event->type ||
        RFSCOUT_EVENT_APP_PROBE_ERROR == a_event->type ||
        ( RFSCOUT_EVENT_APP_DISCOVERED == a_event->type &&
          RFSCOUT_APP_AVAILABILITY_ERROR == (rfscout_app_availability_t)a_event->value ) )
    {
        return RFSCOUT_SYSTEM_EVENT_WARNING;
    }
    return RFSCOUT_SYSTEM_EVENT_INFO;
}

static bool notification_eligible( const rfscout_event_t *a_event )
{
    if( NULL == a_event )
    {
        return false;
    }
    switch( a_event->type )
    {
        case RFSCOUT_EVENT_POWER_CONNECTED:
        case RFSCOUT_EVENT_POWER_DISCONNECTED:
        case RFSCOUT_EVENT_STORAGE_MOUNTED:
        case RFSCOUT_EVENT_STORAGE_MOUNT_FAILED:
        case RFSCOUT_EVENT_STORAGE_UNMOUNTED:
        case RFSCOUT_EVENT_INTERNAL_STORAGE_MOUNTED:
        case RFSCOUT_EVENT_INTERNAL_STORAGE_FORMATTED:
        case RFSCOUT_EVENT_INTERNAL_STORAGE_MOUNT_FAILED:
        case RFSCOUT_EVENT_CONFIG_IMPORTED:
        case RFSCOUT_EVENT_CONFIG_DEFAULTED:
        case RFSCOUT_EVENT_APP_AVAILABLE:
        case RFSCOUT_EVENT_APP_UNAVAILABLE:
        case RFSCOUT_EVENT_WARNING:
        case RFSCOUT_EVENT_ERROR:
            return true;
        default:
            return false;
    }
}

static void render_event( const rfscout_event_t *a_event,
                          char *a_title,
                          size_t a_title_size,
                          char *a_detail,
                          size_t a_detail_size )
{
    if( NULL == a_event || NULL == a_title || NULL == a_detail || 0U == a_title_size ||
        0U == a_detail_size )
    {
        return;
    }

    a_title[ 0 ] = '\0';
    a_detail[ 0 ] = '\0';
    switch( a_event->type )
    {
        case RFSCOUT_EVENT_POWER_CONNECTED:
            snprintf( a_title, a_title_size, "EXTERNAL POWER" );
            if( a_event->value >= 0 && a_event->value <= 100 )
            {
                snprintf( a_detail, a_detail_size, "CONNECTED - %ld%%", (long)a_event->value );
            }
            else
            {
                snprintf( a_detail, a_detail_size, "CONNECTED" );
            }
            break;
        case RFSCOUT_EVENT_POWER_DISCONNECTED:
            snprintf( a_title, a_title_size, "EXTERNAL POWER" );
            if( a_event->value >= 0 && a_event->value <= 100 )
            {
                snprintf( a_detail, a_detail_size, "DISCONNECTED - %ld%%", (long)a_event->value );
            }
            else
            {
                snprintf( a_detail, a_detail_size, "DISCONNECTED" );
            }
            break;
        case RFSCOUT_EVENT_STORAGE_MOUNTED:
            snprintf( a_title, a_title_size, "SD CARD" );
            snprintf( a_detail, a_detail_size, "MOUNTED" );
            break;
        case RFSCOUT_EVENT_STORAGE_MOUNT_FAILED:
            snprintf( a_title, a_title_size, "SD CARD" );
            snprintf( a_detail, a_detail_size, "MOUNT FAILED" );
            break;
        case RFSCOUT_EVENT_STORAGE_UNMOUNTED:
            snprintf( a_title, a_title_size, "SD CARD" );
            snprintf( a_detail, a_detail_size, "UNMOUNTED" );
            break;
        case RFSCOUT_EVENT_INTERNAL_STORAGE_MOUNTED:
            snprintf( a_title, a_title_size, "INTERNAL STORAGE" );
            snprintf( a_detail, a_detail_size, "MOUNTED" );
            break;
        case RFSCOUT_EVENT_INTERNAL_STORAGE_FORMATTED:
            snprintf( a_title, a_title_size, "INTERNAL STORAGE" );
            snprintf( a_detail, a_detail_size, "FORMATTED" );
            break;
        case RFSCOUT_EVENT_INTERNAL_STORAGE_MOUNT_FAILED:
            snprintf( a_title, a_title_size, "INTERNAL STORAGE" );
            snprintf( a_detail, a_detail_size, "MOUNT FAILED" );
            break;
        case RFSCOUT_EVENT_CONFIG_IMPORTED:
            snprintf( a_title, a_title_size, "CONFIG" );
            snprintf( a_detail, a_detail_size, "IMPORTED FROM SD" );
            break;
        case RFSCOUT_EVENT_CONFIG_DEFAULTED:
            snprintf( a_title, a_title_size, "CONFIG" );
            snprintf( a_detail, a_detail_size, "DEFAULTS RESTORED" );
            break;
        case RFSCOUT_EVENT_APP_DISCOVERED:
            snprintf( a_title, a_title_size, "%s", app_name( a_event->source ) );
            if( RFSCOUT_APP_AVAILABILITY_AVAILABLE == (rfscout_app_availability_t)a_event->value )
            {
                snprintf( a_detail, a_detail_size, "DETECTED" );
            }
            else if( RFSCOUT_APP_AVAILABILITY_UNAVAILABLE ==
                     (rfscout_app_availability_t)a_event->value )
            {
                snprintf( a_detail, a_detail_size, "NOT PRESENT" );
            }
            else
            {
                snprintf( a_detail, a_detail_size, "PROBE ERROR" );
            }
            break;
        case RFSCOUT_EVENT_APP_AVAILABLE:
            snprintf( a_title, a_title_size, "%s", app_name( a_event->source ) );
            snprintf( a_detail, a_detail_size, "CONNECTED" );
            break;
        case RFSCOUT_EVENT_APP_UNAVAILABLE:
            snprintf( a_title, a_title_size, "%s", app_name( a_event->source ) );
            snprintf( a_detail, a_detail_size, "DISCONNECTED" );
            break;
        case RFSCOUT_EVENT_APP_PROBE_ERROR:
            snprintf( a_title, a_title_size, "%s", app_name( a_event->source ) );
            snprintf( a_detail, a_detail_size, "PROBE ERROR" );
            break;
        case RFSCOUT_EVENT_DISPLAY_PROVIDER_SELECTED:
            snprintf( a_title, a_title_size, "DISPLAY" );
            snprintf( a_detail, a_detail_size, "ST7789 SELECTED" );
            break;
        case RFSCOUT_EVENT_INPUT_PROVIDER_SELECTED:
            snprintf( a_title, a_title_size, "INPUT" );
            snprintf( a_detail, a_detail_size, "ROTARY/BUTTONS SELECTED" );
            break;
        case RFSCOUT_EVENT_WARNING:
            snprintf( a_title, a_title_size, "SYSTEM WARNING" );
            snprintf( a_detail, a_detail_size, "ATTENTION REQUIRED" );
            break;
        case RFSCOUT_EVENT_ERROR:
            snprintf( a_title, a_title_size, "SYSTEM ERROR" );
            snprintf( a_detail, a_detail_size, "FAULT DETECTED" );
            break;
        default:
            snprintf( a_title, a_title_size, "SYSTEM EVENT" );
            break;
    }
}

static void log_event( const rfscout_event_t *a_event,
                       rfscout_system_event_severity_t a_severity,
                       const char *a_title,
                       const char *a_detail )
{
    const char *title = ( NULL != a_title && '\0' != a_title[ 0 ] ) ? a_title : "SYSTEM EVENT";
    const char *detail = ( NULL != a_detail ) ? a_detail : "";
    if( RFSCOUT_SYSTEM_EVENT_CRITICAL == a_severity )
    {
        ESP_LOGE( TAG,
                  "%s: %s (type=%u source=%u value=%ld)",
                  title,
                  detail,
                  (unsigned)a_event->type,
                  (unsigned)a_event->source,
                  (long)a_event->value );
    }
    else if( RFSCOUT_SYSTEM_EVENT_WARNING == a_severity )
    {
        ESP_LOGW( TAG,
                  "%s: %s (type=%u source=%u value=%ld)",
                  title,
                  detail,
                  (unsigned)a_event->type,
                  (unsigned)a_event->source,
                  (long)a_event->value );
    }
    else
    {
        ESP_LOGI( TAG,
                  "%s: %s (type=%u source=%u value=%ld)",
                  title,
                  detail,
                  (unsigned)a_event->type,
                  (unsigned)a_event->source,
                  (long)a_event->value );
    }
}

static void record_event( const rfscout_event_t *a_event, bool a_allow_notification )
{
    if( !is_system_state_event( a_event ) )
    {
        return;
    }

    const int64_t now_us = esp_timer_get_time();
    const rfscout_system_event_severity_t severity = severity_for( a_event );
    char title[ sizeof( notification.title ) ];
    char detail[ sizeof( notification.detail ) ];
    render_event( a_event, title, sizeof( title ), detail, sizeof( detail ) );

    portENTER_CRITICAL( &system_event_lock );
    history[ history_head ] = ( rfscout_system_event_entry_t ){
        .monotonic_us = now_us, .event = *a_event, .severity = severity };
    history_head = ( history_head + 1U ) % RFSCOUT_SYSTEM_EVENT_HISTORY_CAPACITY;
    if( history_count < RFSCOUT_SYSTEM_EVENT_HISTORY_CAPACITY )
    {
        ++history_count;
    }

    if( a_allow_notification && notifications_enabled && notification_eligible( a_event ) )
    {
        notification.active = true;
        notification.severity = severity;
        snprintf( notification.title, sizeof( notification.title ), "%s", title );
        snprintf( notification.detail, sizeof( notification.detail ), "%s", detail );
        notification_expires_us = now_us + SYSTEM_EVENT_NOTIFICATION_US;
        attention_pending = true;
    }
    portEXIT_CRITICAL( &system_event_lock );

    log_event( a_event, severity, title, detail );
}

static void record_discovery( rfscout_event_type_t a_type, uint8_t a_source, int32_t a_value )
{
    const rfscout_event_t event = { .type = a_type, .source = a_source, .value = a_value };
    record_event( &event, false );
}

static void on_event( const rfscout_event_t *a_event, void *a_context )
{
    (void)a_context;
    record_event( a_event, true );
}

esp_err_t system_events_init( void )
{
    portENTER_CRITICAL( &system_event_lock );
    memset( history, 0, sizeof( history ) );
    history_head = 0U;
    history_count = 0U;
    memset( &notification, 0, sizeof( notification ) );
    notification_expires_us = 0;
    notifications_enabled = false;
    attention_pending = false;
    power_state_known = false;
    portEXIT_CRITICAL( &system_event_lock );

    return rfscout_event_subscribe( on_event, NULL ) ? ESP_OK : ESP_ERR_NO_MEM;
}

void system_events_enable_notifications( void )
{
    portENTER_CRITICAL( &system_event_lock );
    notifications_enabled = true;
    portEXIT_CRITICAL( &system_event_lock );
}

bool system_events_notifications_enabled( void )
{
    bool enabled = false;
    portENTER_CRITICAL( &system_event_lock );
    enabled = notifications_enabled;
    portEXIT_CRITICAL( &system_event_lock );
    return enabled;
}

void system_events_poll( void )
{
    battery_status_t battery;
    if( battery_get_status( &battery ) && battery.charger_available )
    {
        const int32_t percentage = battery.available ? (int32_t)battery.percentage : -1;
        if( !power_state_known )
        {
            power_state_known = true;
            external_power = battery.external_power;
            record_discovery( external_power ? RFSCOUT_EVENT_POWER_CONNECTED
                                             : RFSCOUT_EVENT_POWER_DISCONNECTED,
                              RFSCOUT_EVENT_SOURCE_SYSTEM,
                              percentage );
        }
        else if( external_power != battery.external_power )
        {
            external_power = battery.external_power;
            rfscout_event_emit( external_power ? RFSCOUT_EVENT_POWER_CONNECTED
                                               : RFSCOUT_EVENT_POWER_DISCONNECTED,
                                RFSCOUT_EVENT_SOURCE_SYSTEM,
                                percentage );
        }
    }

    const int64_t now_us = esp_timer_get_time();
    bool expired = false;
    bool request_attention = false;
    portENTER_CRITICAL( &system_event_lock );
    if( notification.active && now_us >= notification_expires_us )
    {
        notification.active = false;
        notification_expires_us = 0;
        expired = true;
    }
    if( attention_pending )
    {
        attention_pending = false;
        request_attention = true;
    }
    portEXIT_CRITICAL( &system_event_lock );

    if( request_attention )
    {
        (void)display_idle_request_attention( SYSTEM_EVENT_NOTIFICATION_MS );
        display_invalidate();
    }
    else if( expired )
    {
        display_invalidate();
    }
}

size_t system_events_history_count( void )
{
    size_t count = 0U;
    portENTER_CRITICAL( &system_event_lock );
    count = history_count;
    portEXIT_CRITICAL( &system_event_lock );
    return count;
}

bool system_events_history_get( size_t a_index, rfscout_system_event_entry_t *a_entry )
{
    if( NULL == a_entry )
    {
        return false;
    }

    bool found = false;
    portENTER_CRITICAL( &system_event_lock );
    if( a_index < history_count )
    {
        const size_t newest = ( history_head + RFSCOUT_SYSTEM_EVENT_HISTORY_CAPACITY - 1U ) %
                              RFSCOUT_SYSTEM_EVENT_HISTORY_CAPACITY;
        const size_t slot = ( newest + RFSCOUT_SYSTEM_EVENT_HISTORY_CAPACITY - a_index ) %
                            RFSCOUT_SYSTEM_EVENT_HISTORY_CAPACITY;
        *a_entry = history[ slot ];
        found = true;
    }
    portEXIT_CRITICAL( &system_event_lock );
    return found;
}

bool system_events_notification_get( rfscout_system_event_notification_t *a_notification )
{
    if( NULL == a_notification )
    {
        return false;
    }

    const int64_t now_us = esp_timer_get_time();
    bool active = false;
    portENTER_CRITICAL( &system_event_lock );
    if( notification.active && now_us < notification_expires_us )
    {
        *a_notification = notification;
        active = true;
    }
    portEXIT_CRITICAL( &system_event_lock );
    return active;
}
