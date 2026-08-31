from pathlib import Path


def replace(path, old, new, count=1):
    p = Path(path)
    text = p.read_text()
    actual = text.count(old)
    if actual < count:
        raise SystemExit(f"{path}: expected at least {count} occurrence(s), found {actual}: {old[:80]!r}")
    p.write_text(text.replace(old, new, count))


def write(path, content):
    Path(path).write_text(content)


write(
    "main/display_idle.h",
    '''/**
    display_idle.h : RFScout global display-idle policy
    Description: Dims or disables LCD output after inactivity while keeping applications running.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#pragma once

#ifndef _RFSCOUT_DISPLAY_IDLE_H_
#define _RFSCOUT_DISPLAY_IDLE_H_

#include "esp_err.h"

/** @brief Initialize the global display-idle state from current configuration. */
esp_err_t display_idle_init( void );

/** @brief Poll user activity and apply dim/off/wake transitions. */
void display_idle_poll( void );

#endif // _RFSCOUT_DISPLAY_IDLE_H_
''',
)

write(
    "main/display_idle.c",
    '''/**
    display_idle.c : RFScout global display-idle policy
    Description: Keeps application execution independent from LCD dim/off presentation state.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "display_idle.h"

#include "device_config.h"
#include "display.h"
#include "platform.h"
#include "power.h"

#include "esp_log.h"
#include "esp_timer.h"

#include <stdint.h>

#define DISPLAY_IDLE_US_PER_SECOND 1000000LL
#define DISPLAY_IDLE_DIM_DIVISOR 4U
#define DISPLAY_IDLE_DIM_MIN_PERCENT 1U

static const char *TAG = "display_idle";

typedef enum
{
    DISPLAY_IDLE_ACTIVE = 0,
    DISPLAY_IDLE_DIMMED,
    DISPLAY_IDLE_OFF
} display_idle_state_t;

static display_idle_state_t state;
static int64_t last_activity_us;

static uint8_t dim_brightness( uint8_t a_normal_percent )
{
    uint8_t dimmed = (uint8_t)( a_normal_percent / DISPLAY_IDLE_DIM_DIVISOR );
    if( dimmed < DISPLAY_IDLE_DIM_MIN_PERCENT )
    {
        dimmed = DISPLAY_IDLE_DIM_MIN_PERCENT;
    }
    return dimmed;
}

static void set_dimmed( void )
{
    const uint8_t brightness = device_config_get()->brightness_percent;
    display_set_brightness( dim_brightness( brightness ) );
    state = DISPLAY_IDLE_DIMMED;
    ESP_LOGI( TAG, "display dimmed after inactivity" );
}

static void set_off( void )
{
    display_set_brightness( 0U );
    const esp_err_t result = display_set_output_enabled( false );
    if( ESP_OK != result )
    {
        ESP_LOGW( TAG, "could not disable LCD output: %s", esp_err_to_name( result ) );
    }
    state = DISPLAY_IDLE_OFF;
    ESP_LOGI( TAG, "display output disabled after inactivity" );
}

static void wake_display( void )
{
    const device_config_t *config = device_config_get();
    if( DISPLAY_IDLE_OFF == state )
    {
        const esp_err_t result = display_set_output_enabled( true );
        if( ESP_OK != result )
        {
            ESP_LOGW( TAG, "could not re-enable LCD output: %s", esp_err_to_name( result ) );
        }
        else
        {
            /* Keep the backlight dark until the latest application framebuffer has
               replaced the LCD's pre-timeout contents. */
            display_invalidate();
            const esp_err_t presented = display_present();
            if( ESP_OK != presented )
            {
                ESP_LOGW( TAG, "wake presentation failed: %s", esp_err_to_name( presented ) );
            }
        }
    }
    display_set_brightness( config->brightness_percent );
    state = DISPLAY_IDLE_ACTIVE;
    ESP_LOGI( TAG, "display woke from user activity" );
}

esp_err_t display_idle_init( void )
{
    state = DISPLAY_IDLE_ACTIVE;
    last_activity_us = esp_timer_get_time();
    const esp_err_t result = display_set_output_enabled( true );
    display_set_brightness( device_config_get()->brightness_percent );
    return result;
}

void display_idle_poll( void )
{
    const int64_t now_us = esp_timer_get_time();
    if( platform_input_activity_pending() )
    {
        if( DISPLAY_IDLE_ACTIVE != state )
        {
            /* Wake is a device-level gesture. Consume the entire gesture before
               lock, app navigation, menu, or power-button semantics can see it. */
            platform_consume_wake_input();
            power_consume_wake_input();
            wake_display();
        }
        last_activity_us = now_us;
        return;
    }

    const device_config_t *config = device_config_get();
    const int64_t idle_us = now_us - last_activity_us;
    const uint16_t off_seconds = config->display_off_timeout_seconds;
    const uint16_t dim_seconds = config->display_dim_timeout_seconds;

    if( ( 0U != off_seconds ) &&
        ( idle_us >= (int64_t)off_seconds * DISPLAY_IDLE_US_PER_SECOND ) )
    {
        if( DISPLAY_IDLE_OFF != state )
        {
            set_off();
        }
        return;
    }

    if( ( DISPLAY_IDLE_ACTIVE == state ) && ( 0U != dim_seconds ) &&
        ( idle_us >= (int64_t)dim_seconds * DISPLAY_IDLE_US_PER_SECOND ) )
    {
        set_dimmed();
    }
}
''',
)

replace(
    "main/CMakeLists.txt",
    '        "display_brightness.c"\n        "ui_present.c"',
    '        "display_brightness.c"\n        "display_idle.c"\n        "ui_present.c"',
)

replace(
    "main/display.h",
    '/** @brief Set LCD backlight brightness from 10 to 100 percent. */\nvoid display_set_brightness( uint8_t a_percent );',
    '/** @brief Set LCD backlight brightness from 0 through 100 percent. Zero is reserved for display-idle blanking. */\nvoid display_set_brightness( uint8_t a_percent );\n\n/** @brief Enable or disable ST7789 display output without changing the application framebuffer. */\nesp_err_t display_set_output_enabled( bool a_enabled );\n\n/** @brief Return whether LCD output transfers are currently enabled. */\nbool display_output_enabled( void );',
)

replace(
    "main/display.c",
    'static uint16_t *dma_buffer;\nstatic bool display_dirty = true;',
    'static uint16_t *dma_buffer;\nstatic bool display_dirty = true;\nstatic bool output_enabled;',
)
replace(
    "main/display.c",
    'void display_invalidate( void )\n{\n    display_dirty = true;\n}',
    '''esp_err_t display_set_output_enabled( bool a_enabled )
{
    if( NULL == panel )
    {
        return ESP_ERR_INVALID_STATE;
    }
    if( output_enabled == a_enabled )
    {
        if( a_enabled )
        {
            display_invalidate();
        }
        return ESP_OK;
    }

    const esp_err_t result = esp_lcd_panel_disp_on_off( panel, a_enabled );
    if( ESP_OK == result )
    {
        output_enabled = a_enabled;
        if( a_enabled )
        {
            display_invalidate();
        }
    }
    return result;
}

bool display_output_enabled( void )
{
    return output_enabled;
}

void display_invalidate( void )
{
    display_dirty = true;
}''',
)
replace(
    "main/display.c",
    'esp_err_t display_init( void )\n{\n    display_dirty = true;',
    'esp_err_t display_init( void )\n{\n    display_dirty = true;\n    output_enabled = false;',
)
replace(
    "main/display.c",
    '    ESP_ERROR_CHECK( esp_lcd_panel_disp_on_off( panel, true ) );\n\n    display_clear',
    '    ESP_ERROR_CHECK( esp_lcd_panel_disp_on_off( panel, true ) );\n    output_enabled = true;\n\n    display_clear',
)
replace(
    "main/display.c",
    'esp_err_t display_flush( void )\n{\n    if( !display_dirty )\n    {\n        return ESP_OK;\n    }',
    'esp_err_t display_flush( void )\n{\n    if( !display_dirty || !output_enabled )\n    {\n        return ESP_OK;\n    }',
)

replace(
    "main/display_brightness.c",
    '    if( a_percent < 10U )\n    {\n        a_percent = 10U;\n    }\n    if( a_percent > 100U )',
    '    if( a_percent > 100U )',
)

replace(
    "main/ui_present.c",
    'esp_err_t display_present( void )\n{\n    if( !display_is_dirty() )',
    'esp_err_t display_present( void )\n{\n    if( !display_output_enabled() )\n    {\n        return ESP_OK;\n    }\n    if( !display_is_dirty() )',
)

replace(
    "main/platform.h",
    '/**\n * @brief Consume one board-neutral user-input event.',
    '''/**
 * @brief Return whether any physical user activity is pending, independent of input lock.
 *
 * Used by the global display-idle policy before ordinary input dispatch so a locked
 * device can still wake its display without leaking the wake gesture into an app.
 */
bool platform_input_activity_pending( void );

/** @brief Discard the complete physical input gesture currently being used to wake the display. */
void platform_consume_wake_input( void );

/**
 * @brief Consume one board-neutral user-input event.''',
)

replace(
    "main/platform.c",
    'bool platform_input_get( rfscout_input_event_t *a_event )\n{',
    '''bool platform_input_activity_pending( void )
{
    bool movement;
    bool button_activity;

    portENTER_CRITICAL( &encoder_lock );
    movement = ( 0 != encoder_delta );
    portEXIT_CRITICAL( &encoder_lock );

    portENTER_CRITICAL( &encoder_button_lock );
    button_activity = encoder_button_down || ( encoder_button_pending_duration_us > 0 );
    portEXIT_CRITICAL( &encoder_button_lock );

    return movement || button_activity || encoder_button_event || encoder_button_long_event ||
           encoder_button_back_event || ( 0 == gpio_get_level( RFSCOUT_BOARD_USER_KEY_PIN ) );
}

void platform_consume_wake_input( void )
{
    clear_encoder_delta();
    encoder_button_event = false;
    encoder_button_long_event = false;
    encoder_button_back_event = false;

    uint32_t press_sequence;
    bool button_down;
    portENTER_CRITICAL( &encoder_button_lock );
    encoder_button_pending_duration_us = 0;
    encoder_button_pending_sequence = 0U;
    press_sequence = encoder_button_press_sequence;
    button_down = encoder_button_down;
    portEXIT_CRITICAL( &encoder_button_lock );

    encoder_button_seen_sequence = press_sequence;
    encoder_button_lock_sequence = press_sequence;
    encoder_button_lock_fired = button_down;
}

bool platform_input_get( rfscout_input_event_t *a_event )
{''',
)

replace(
    "main/power.h",
    '/** @brief Poll the user button and execute sleep/shutdown actions. */\nvoid power_poll( void );',
    '/** @brief Poll the user button and execute sleep/shutdown actions. */\nvoid power_poll( void );\n\n/** @brief Consume a currently held user-button gesture because it was used only to wake the display. */\nvoid power_consume_wake_input( void );',
)
replace(
    "main/power.c",
    'static int64_t pressed_since_us;\nstatic power_status_t status;',
    'static int64_t pressed_since_us;\nstatic bool suppress_current_press;\nstatic power_status_t status;',
)
replace(
    "main/power.c",
    '    pressed_since_us = stable_pressed ? raw_changed_us : 0;\n    status.state = POWER_HOLD_IDLE;',
    '    pressed_since_us = stable_pressed ? raw_changed_us : 0;\n    suppress_current_press = false;\n    status.state = POWER_HOLD_IDLE;',
)
replace(
    "main/power.c",
    'void power_poll( void )\n{\n    const int64_t now_us = esp_timer_get_time();\n    const bool pressed = ( 0 == gpio_get_level( RFSCOUT_BOARD_USER_KEY_PIN ) );',
    '''void power_consume_wake_input( void )
{
    if( 0 == gpio_get_level( RFSCOUT_BOARD_USER_KEY_PIN ) )
    {
        suppress_current_press = true;
        pressed_since_us = 0;
        set_power_status( POWER_HOLD_IDLE, 0U );
    }
}

void power_poll( void )
{
    const int64_t now_us = esp_timer_get_time();
    const bool pressed = ( 0 == gpio_get_level( RFSCOUT_BOARD_USER_KEY_PIN ) );

    if( suppress_current_press )
    {
        raw_pressed = pressed;
        stable_pressed = pressed;
        raw_changed_us = now_us;
        pressed_since_us = 0;
        set_power_status( POWER_HOLD_IDLE, 0U );
        if( !pressed )
        {
            suppress_current_press = false;
        }
        return;
    }''',
)

replace(
    "main/device_config.h",
    '    uint8_t brightness_percent;\n    bool apps_enabled',
    '    uint8_t brightness_percent;\n    uint16_t display_dim_timeout_seconds;\n    uint16_t display_off_timeout_seconds;\n    bool apps_enabled',
)
replace(
    "main/device_config.h",
    '/** @brief Change display brightness percentage. */\nesp_err_t device_config_set_brightness( uint8_t a_percent );',
    '/** @brief Change display brightness percentage. */\nesp_err_t device_config_set_brightness( uint8_t a_percent );\n\n/** @brief Change inactivity timeout before display dimming; zero disables dimming. */\nesp_err_t device_config_set_display_dim_timeout( uint16_t a_seconds );\n\n/** @brief Change inactivity timeout before LCD output is disabled; zero disables screen-off. */\nesp_err_t device_config_set_display_off_timeout( uint16_t a_seconds );',
)

replace(
    "main/device_config.c",
    '#define CONFIG_BRIGHTNESS_DEFAULT 70U\n#define CONFIG_RECORDER_VOLUME_DEFAULT',
    '#define CONFIG_BRIGHTNESS_DEFAULT 70U\n#define CONFIG_DISPLAY_DIM_TIMEOUT_DEFAULT_SECONDS 120U\n#define CONFIG_DISPLAY_OFF_TIMEOUT_DEFAULT_SECONDS 300U\n#define CONFIG_DISPLAY_TIMEOUT_MAX_SECONDS 3600U\n#define CONFIG_RECORDER_VOLUME_DEFAULT',
)
replace(
    "main/device_config.c",
    '    config.brightness_percent = CONFIG_BRIGHTNESS_DEFAULT;\n    config.startup_app',
    '    config.brightness_percent = CONFIG_BRIGHTNESS_DEFAULT;\n    config.display_dim_timeout_seconds = CONFIG_DISPLAY_DIM_TIMEOUT_DEFAULT_SECONDS;\n    config.display_off_timeout_seconds = CONFIG_DISPLAY_OFF_TIMEOUT_DEFAULT_SECONDS;\n    config.startup_app',
)
replace(
    "main/device_config.c",
    '/** @brief Apply validated display preference overrides. */\nstatic void load_display',
    '''static uint16_t json_display_timeout_seconds( const cJSON *a_parent,
                                              const char *a_name,
                                              uint16_t a_default )
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive( a_parent, a_name );
    if( !cJSON_IsNumber( item ) || item->valueint < 0 ||
        item->valueint > (int)CONFIG_DISPLAY_TIMEOUT_MAX_SECONDS )
    {
        return a_default;
    }
    return (uint16_t)item->valueint;
}

/** @brief Apply validated display preference overrides. */
static void load_display''',
)
replace(
    "main/device_config.c",
    '    config.clock_24h = json_bool( display, "clock_24h", config.clock_24h );',
    '''    config.display_dim_timeout_seconds = json_display_timeout_seconds(
        display, "dim_timeout_seconds", config.display_dim_timeout_seconds );
    config.display_off_timeout_seconds = json_display_timeout_seconds(
        display, "off_timeout_seconds", config.display_off_timeout_seconds );
    if( ( 0U != config.display_dim_timeout_seconds ) &&
        ( 0U != config.display_off_timeout_seconds ) &&
        ( config.display_off_timeout_seconds <= config.display_dim_timeout_seconds ) )
    {
        config.display_off_timeout_seconds = 0U;
    }
    config.clock_24h = json_bool( display, "clock_24h", config.clock_24h );''',
)
replace(
    "main/device_config.c",
    '    cJSON_AddNumberToObject( display, "brightness", config.brightness_percent );\n    cJSON_AddBoolToObject',
    '    cJSON_AddNumberToObject( display, "brightness", config.brightness_percent );\n    cJSON_AddNumberToObject(\n        display, "dim_timeout_seconds", config.display_dim_timeout_seconds );\n    cJSON_AddNumberToObject(\n        display, "off_timeout_seconds", config.display_off_timeout_seconds );\n    cJSON_AddBoolToObject',
)
replace(
    "main/device_config.c",
    'esp_err_t device_config_set_clock_24h( bool a_enabled )\n{',
    '''esp_err_t device_config_set_display_dim_timeout( uint16_t a_seconds )
{
    if( a_seconds > CONFIG_DISPLAY_TIMEOUT_MAX_SECONDS ||
        ( ( 0U != a_seconds ) && ( 0U != config.display_off_timeout_seconds ) &&
          ( a_seconds >= config.display_off_timeout_seconds ) ) )
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
        ( ( 0U != a_seconds ) && ( 0U != config.display_dim_timeout_seconds ) &&
          ( a_seconds <= config.display_dim_timeout_seconds ) ) )
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

esp_err_t device_config_set_clock_24h( bool a_enabled )
{''',
)

replace(
    "main/device_view.c",
    '''#define DEVICE_ITEM_BACK 0U
#define DEVICE_ITEM_BRIGHTNESS 1U
#define DEVICE_ITEM_THEME 2U
#define DEVICE_ITEM_CLOCK_FORMAT 3U
#define DEVICE_ITEM_TIMEZONE 4U
#define DEVICE_ITEM_STARTUP 5U
#define DEVICE_ITEM_YEAR 6U
#define DEVICE_ITEM_MONTH 7U
#define DEVICE_ITEM_DAY 8U
#define DEVICE_ITEM_HOUR 9U
#define DEVICE_ITEM_MINUTE 10U
#define DEVICE_ITEM_WIFI 11U
#define DEVICE_ITEM_CC1101 12U
#define DEVICE_ITEM_NRF24 13U
#define DEVICE_ITEM_AUDIO 14U
#define DEVICE_ITEM_THERMAL 15U
#define DEVICE_ITEM_LOGGING 16U
#define DEVICE_ITEM_ALERT_MODE 17U
#define DEVICE_ITEM_ALERT_VOLUME 18U
#define DEVICE_ITEM_SAVE 19U''',
    '''#define DEVICE_ITEM_BACK 0U
#define DEVICE_ITEM_BRIGHTNESS 1U
#define DEVICE_ITEM_DIM_TIMEOUT 2U
#define DEVICE_ITEM_OFF_TIMEOUT 3U
#define DEVICE_ITEM_THEME 4U
#define DEVICE_ITEM_CLOCK_FORMAT 5U
#define DEVICE_ITEM_TIMEZONE 6U
#define DEVICE_ITEM_STARTUP 7U
#define DEVICE_ITEM_YEAR 8U
#define DEVICE_ITEM_MONTH 9U
#define DEVICE_ITEM_DAY 10U
#define DEVICE_ITEM_HOUR 11U
#define DEVICE_ITEM_MINUTE 12U
#define DEVICE_ITEM_WIFI 13U
#define DEVICE_ITEM_CC1101 14U
#define DEVICE_ITEM_NRF24 15U
#define DEVICE_ITEM_AUDIO 16U
#define DEVICE_ITEM_THERMAL 17U
#define DEVICE_ITEM_LOGGING 18U
#define DEVICE_ITEM_ALERT_MODE 19U
#define DEVICE_ITEM_ALERT_VOLUME 20U
#define DEVICE_ITEM_SAVE 21U''',
)
replace(
    "main/device_view.c",
    '#define DEVICE_ITEM_NOTIFICATION_FIRST 20U',
    '#define DEVICE_ITEM_NOTIFICATION_FIRST 22U',
)
replace(
    "main/device_view.c",
    '#define DEVICE_ALERT_VOLUME_STEP 10',
    '''#define DEVICE_ALERT_VOLUME_STEP 10

static const uint16_t DIM_TIMEOUT_OPTIONS[] = { 0U, 30U, 60U, 120U, 300U };
static const uint16_t OFF_TIMEOUT_OPTIONS[] = { 0U, 60U, 120U, 300U, 600U, 1800U };''',
)
replace(
    "main/device_view.c",
    'static const char *startup_app_name',
    '''static void format_timeout( uint16_t a_seconds, char *a_value, size_t a_capacity )
{
    if( 0U == a_seconds )
    {
        snprintf( a_value, a_capacity, "Off" );
    }
    else if( 0U == ( a_seconds % 60U ) )
    {
        snprintf( a_value, a_capacity, "%um", (unsigned)( a_seconds / 60U ) );
    }
    else
    {
        snprintf( a_value, a_capacity, "%us", (unsigned)a_seconds );
    }
}

static uint16_t next_timeout_option( const uint16_t *a_options,
                                     size_t a_count,
                                     uint16_t a_current,
                                     int a_delta )
{
    size_t index = 0U;
    for( size_t item = 0U; item < a_count; ++item )
    {
        if( a_options[ item ] == a_current )
        {
            index = item;
            break;
        }
    }
    index = ( a_delta > 0 ) ? ( index + 1U ) % a_count : ( index + a_count - 1U ) % a_count;
    return a_options[ index ];
}

static const char *startup_app_name''',
)
replace(
    "main/device_view.c",
    '        case DEVICE_ITEM_THEME:\n            return "Theme";',
    '        case DEVICE_ITEM_DIM_TIMEOUT:\n            return "Dim after";\n        case DEVICE_ITEM_OFF_TIMEOUT:\n            return "Screen off";\n        case DEVICE_ITEM_THEME:\n            return "Theme";',
)
replace(
    "main/device_view.c",
    '        case DEVICE_ITEM_THEME:\n            snprintf( a_value, a_capacity, "%s", ui_theme_display_name( ui_theme_current() ) );',
    '        case DEVICE_ITEM_DIM_TIMEOUT:\n            format_timeout( config->display_dim_timeout_seconds, a_value, a_capacity );\n            break;\n        case DEVICE_ITEM_OFF_TIMEOUT:\n            format_timeout( config->display_off_timeout_seconds, a_value, a_capacity );\n            break;\n        case DEVICE_ITEM_THEME:\n            snprintf( a_value, a_capacity, "%s", ui_theme_display_name( ui_theme_current() ) );',
)
replace(
    "main/device_view.c",
    'static void adjust_theme( int a_delta )\n{',
    '''static void adjust_display_timeout( bool a_off_timeout, int a_delta )
{
    const device_config_t *config = device_config_get();
    const uint16_t old_dim = config->display_dim_timeout_seconds;
    const uint16_t old_off = config->display_off_timeout_seconds;

    if( !a_off_timeout )
    {
        const uint16_t new_dim = next_timeout_option( DIM_TIMEOUT_OPTIONS,
                                                      sizeof( DIM_TIMEOUT_OPTIONS ) /
                                                          sizeof( DIM_TIMEOUT_OPTIONS[ 0 ] ),
                                                      old_dim,
                                                      a_delta );
        uint16_t new_off = old_off;
        if( ( 0U != new_dim ) && ( 0U != new_off ) && ( new_off <= new_dim ) )
        {
            new_off = 0U;
            for( size_t item = 1U;
                 item < sizeof( OFF_TIMEOUT_OPTIONS ) / sizeof( OFF_TIMEOUT_OPTIONS[ 0 ] );
                 ++item )
            {
                if( OFF_TIMEOUT_OPTIONS[ item ] > new_dim )
                {
                    new_off = OFF_TIMEOUT_OPTIONS[ item ];
                    break;
                }
            }
            if( ESP_OK != device_config_set_display_off_timeout( new_off ) )
            {
                return;
            }
        }
        if( ESP_OK == device_config_set_display_dim_timeout( new_dim ) )
        {
            char old_text[ 16 ];
            char new_text[ 16 ];
            format_timeout( old_dim, old_text, sizeof( old_text ) );
            format_timeout( new_dim, new_text, sizeof( new_text ) );
            log_config_change( "display.dim_timeout_seconds", old_text, new_text );
            emit_device_event( RFSCOUT_EVENT_NAVIGATE, ( a_delta > 0 ) ? 1 : -1 );
        }
        return;
    }

    uint16_t candidate = old_off;
    const size_t option_count = sizeof( OFF_TIMEOUT_OPTIONS ) / sizeof( OFF_TIMEOUT_OPTIONS[ 0 ] );
    for( size_t attempt = 0U; attempt < option_count; ++attempt )
    {
        candidate = next_timeout_option( OFF_TIMEOUT_OPTIONS, option_count, candidate, a_delta );
        if( ( 0U == candidate ) || ( 0U == old_dim ) || ( candidate > old_dim ) )
        {
            break;
        }
    }
    if( ESP_OK == device_config_set_display_off_timeout( candidate ) )
    {
        char old_text[ 16 ];
        char new_text[ 16 ];
        format_timeout( old_off, old_text, sizeof( old_text ) );
        format_timeout( candidate, new_text, sizeof( new_text ) );
        log_config_change( "display.off_timeout_seconds", old_text, new_text );
        emit_device_event( RFSCOUT_EVENT_NAVIGATE, ( a_delta > 0 ) ? 1 : -1 );
    }
}

static void adjust_theme( int a_delta )
{''',
)
replace(
    "main/device_view.c",
    '    else if( DEVICE_ITEM_THEME == selected_item )\n    {\n        adjust_theme( a_delta );\n    }',
    '    else if( DEVICE_ITEM_DIM_TIMEOUT == selected_item )\n    {\n        adjust_display_timeout( false, a_delta );\n    }\n    else if( DEVICE_ITEM_OFF_TIMEOUT == selected_item )\n    {\n        adjust_display_timeout( true, a_delta );\n    }\n    else if( DEVICE_ITEM_THEME == selected_item )\n    {\n        adjust_theme( a_delta );\n    }',
)

replace(
    "main/rfscout.c",
    '#include "display.h"\n#include "event.h"',
    '#include "display.h"\n#include "display_idle.h"\n#include "event.h"',
)
replace(
    "main/rfscout.c",
    '    ESP_ERROR_CHECK( app_manager_init() );\n    display_set_brightness( device_config_get()->brightness_percent );',
    '    ESP_ERROR_CHECK( app_manager_init() );\n    display_set_brightness( device_config_get()->brightness_percent );\n    ESP_ERROR_CHECK( display_idle_init() );',
)
replace(
    "main/rfscout.c",
    '    while( true )\n    {\n        platform_poll();\n        power_poll();',
    '    while( true )\n    {\n        /* Display wake interception runs before semantic input processing. Applications\n           continue polling/rendering normally while LCD output is disabled. */\n        display_idle_poll();\n        platform_poll();\n        power_poll();',
)

print("display idle patch applied")
