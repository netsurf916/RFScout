#!/usr/bin/env python3
"""Temporary branch integration helper for the Device/settings feature."""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def patch(path: str, old: str, new: str) -> None:
    target = ROOT / path
    text = target.read_text()
    if old not in text:
        raise SystemExit(f"pattern not found in {path}: {old[:80]!r}")
    target.write_text(text.replace(old, new, 1))


# App manager integration.
patch(
    "main/app_manager.c",
    '#include "cc1101_activity.h"\n#include "mlx90640.h"',
    '#include "cc1101_activity.h"\n#include "device_config.h"\n#include "device_view.h"\n#include "mlx90640.h"',
)
patch(
    "main/app_manager.c",
    'static bool app_available( rfscout_app_id_t a_app )\n{\n    return ( RFSCOUT_APP_THERMAL != a_app ) || mlx90640_is_detected();\n}',
    'static bool app_available( rfscout_app_id_t a_app )\n{\n    if( RFSCOUT_APP_DEVICE == a_app )\n    {\n        return true;\n    }\n    if( !device_config_app_enabled( a_app ) )\n    {\n        return false;\n    }\n    return ( RFSCOUT_APP_THERMAL != a_app ) || mlx90640_is_detected();\n}',
)
patch(
    "main/app_manager.c",
    '        case RFSCOUT_APP_THERMAL:\n            thermal_view_enter();\n            break;\n\n        default:',
    '        case RFSCOUT_APP_THERMAL:\n            thermal_view_enter();\n            break;\n\n        case RFSCOUT_APP_DEVICE:\n            device_view_enter();\n            break;\n\n        default:',
)
patch(
    "main/app_manager.c",
    '        const rfscout_app_id_t candidate = (rfscout_app_id_t)next;\n        if( ( RFSCOUT_APP_THERMAL == candidate ) && !app_available( candidate ) )',
    '        const rfscout_app_id_t candidate = (rfscout_app_id_t)next;\n        if( !device_config_app_enabled( candidate ) && ( RFSCOUT_APP_DEVICE != candidate ) )\n        {\n            continue;\n        }\n        if( ( RFSCOUT_APP_THERMAL == candidate ) && !app_available( candidate ) )',
)
patch(
    "main/app_manager.c",
    '    const bool pressed = platform_encoder_button_pressed();\n    const int delta = platform_encoder_delta();\n\n    if( RFSCOUT_APP_CC1101 == active_app )',
    '    const bool pressed = platform_encoder_button_pressed();\n    const int delta = platform_encoder_delta();\n\n    if( RFSCOUT_APP_DEVICE == active_app )\n    {\n        if( device_view_input( pressed, delta ) )\n        {\n            const rfscout_app_id_t candidate = next_available_app( 1, now );\n            if( candidate != active_app )\n            {\n                leave_app( active_app );\n                active_app = candidate;\n                enter_app( active_app );\n            }\n        }\n        return;\n    }\n\n    if( RFSCOUT_APP_CC1101 == active_app )',
)

# App log token table.
patch(
    "main/app_logging.c",
    '"WIFI_BT", "CC1101", "NRF24", "AUDIO", "THERMAL"',
    '"WIFI_BT", "CC1101", "NRF24", "AUDIO", "THERMAL", "DEVICE"',
)

# Startup ordering and config/time initialization.
patch(
    "main/rfscout.c",
    '#include "ble_monitor.h"\n#include "log_writer.h"',
    '#include "ble_monitor.h"\n#include "device_config.h"\n#include "device_time.h"\n#include "display.h"\n#include "log_writer.h"',
)
patch(
    "main/rfscout.c",
    '    ESP_ERROR_CHECK( app_manager_init() );\n\n    result = storage_init();\n    if( ESP_OK == result )\n    {\n        result = log_writer_init();',
    '    result = storage_init();\n    if( ESP_OK != result )\n    {\n        ESP_LOGW( TAG, "SD storage unavailable: %s", esp_err_to_name( result ) );\n    }\n\n    ESP_ERROR_CHECK( device_config_init() );\n    ESP_ERROR_CHECK( device_time_init() );\n    ESP_ERROR_CHECK( app_manager_init() );\n    display_set_brightness( device_config_get()->brightness_percent );\n\n    if( storage_is_available() && device_config_get()->logging_enabled )\n    {\n        result = log_writer_init();',
)

# Power-off time checkpoint is hard-off only, not deep sleep.
patch(
    "main/power.c",
    '#include "board.h"\n#include "log_writer.h"',
    '#include "board.h"\n#include "device_time.h"\n#include "log_writer.h"',
)
patch(
    "main/power.c",
    'static bool prepare_storage_for_power_state( void )',
    'static bool prepare_storage_for_power_state( bool a_checkpoint_time )',
)
patch(
    "main/power.c",
    '    storage_shutdown();\n    return true;',
    '    if( a_checkpoint_time )\n    {\n        const esp_err_t checkpoint = device_time_checkpoint();\n        if( ESP_OK != checkpoint )\n        {\n            ESP_LOGW( TAG, "wall-clock checkpoint failed: %s", esp_err_to_name( checkpoint ) );\n        }\n    }\n\n    storage_shutdown();\n    return true;',
)
patch(
    "main/power.c",
    '    if( !prepare_storage_for_power_state() )',
    '    if( !prepare_storage_for_power_state( false ) )',
)
patch(
    "main/power.c",
    '        if( !prepare_storage_for_power_state() )',
    '        if( !prepare_storage_for_power_state( true ) )',
)

# Shared status-bar clock.
patch(
    "main/status_bar.c",
    '#include "display.h"\n#include "log_writer.h"',
    '#include "device_time.h"\n#include "display.h"\n#include "log_writer.h"',
)
patch(
    "main/status_bar.c",
    '#include <stdbool.h>\n#include <stdint.h>',
    '#include <stdbool.h>\n#include <stdint.h>\n#include <string.h>',
)
patch(
    "main/status_bar.c",
    '    if( mlx90640_is_detected() )\n    {\n        draw_thermal_icon( x );\n        x -= STATUS_BAR_ICON_WIDTH + STATUS_BAR_ICON_GAP;\n    }\n\n    (void)x;',
    '    if( mlx90640_is_detected() )\n    {\n        draw_thermal_icon( x );\n        x -= STATUS_BAR_ICON_WIDTH + STATUS_BAR_ICON_GAP;\n    }\n\n    char clock[ 12 ];\n    (void)device_time_format_clock( clock, sizeof( clock ) );\n    const int clock_width = (int)strlen( clock ) * 6;\n    display_draw_text( x - clock_width, STATUS_BAR_ICON_Y + 3, clock, STATUS_BAR_COLOR_NORMAL, 1U );',
)

# Backlight PWM brightness API.
patch(
    "main/display.h",
    '/**\n * @brief Fill the local framebuffer with one RGB565 color.',
    '/** @brief Set LCD backlight brightness from 10 to 100 percent. */\nvoid display_set_brightness( uint8_t a_percent );\n\n/**\n * @brief Fill the local framebuffer with one RGB565 color.',
)
patch(
    "main/display.c",
    '#include "driver/gpio.h"\n#include "driver/spi_master.h"',
    '#include "driver/gpio.h"\n#include "driver/ledc.h"\n#include "driver/spi_master.h"',
)
patch(
    "main/display.c",
    '#define DISPLAY_DMA_PIXEL_COUNT ( RFSCOUT_DISPLAY_WIDTH * DISPLAY_DMA_ROWS )',
    '#define DISPLAY_DMA_PIXEL_COUNT ( RFSCOUT_DISPLAY_WIDTH * DISPLAY_DMA_ROWS )\n#define DISPLAY_BACKLIGHT_LEDC_MODE LEDC_LOW_SPEED_MODE\n#define DISPLAY_BACKLIGHT_LEDC_TIMER LEDC_TIMER_1\n#define DISPLAY_BACKLIGHT_LEDC_CHANNEL LEDC_CHANNEL_1',
)
patch(
    "main/display.c",
    '    gpio_config_t backlight = { .pin_bit_mask = 1ULL << RFSCOUT_BOARD_DISPLAY_BACKLIGHT_PIN,\n                                .mode = GPIO_MODE_OUTPUT };\n    ESP_ERROR_CHECK( gpio_config( &backlight ) );\n    gpio_set_level( RFSCOUT_BOARD_DISPLAY_BACKLIGHT_PIN, 0 );',
    '    ledc_timer_config_t backlight_timer = {\n        .speed_mode = DISPLAY_BACKLIGHT_LEDC_MODE,\n        .duty_resolution = LEDC_TIMER_8_BIT,\n        .timer_num = DISPLAY_BACKLIGHT_LEDC_TIMER,\n        .freq_hz = 5000,\n        .clk_cfg = LEDC_AUTO_CLK,\n    };\n    ESP_ERROR_CHECK( ledc_timer_config( &backlight_timer ) );\n    ledc_channel_config_t backlight_channel = {\n        .gpio_num = RFSCOUT_BOARD_DISPLAY_BACKLIGHT_PIN,\n        .speed_mode = DISPLAY_BACKLIGHT_LEDC_MODE,\n        .channel = DISPLAY_BACKLIGHT_LEDC_CHANNEL,\n        .intr_type = LEDC_INTR_DISABLE,\n        .timer_sel = DISPLAY_BACKLIGHT_LEDC_TIMER,\n        .duty = 0U,\n        .hpoint = 0,\n    };\n    ESP_ERROR_CHECK( ledc_channel_config( &backlight_channel ) );',
)
patch(
    "main/display.c",
    '    gpio_set_level( RFSCOUT_BOARD_DISPLAY_BACKLIGHT_PIN, 1 );\n    ESP_LOGI(',
    '    display_set_brightness( 100U );\n    ESP_LOGI(',
)
patch(
    "main/display.c",
    'void display_clear( uint16_t a_color )',
    'void display_set_brightness( uint8_t a_percent )\n{\n    if( a_percent < 10U )\n    {\n        a_percent = 10U;\n    }\n    if( a_percent > 100U )\n    {\n        a_percent = 100U;\n    }\n    const uint32_t duty = ( (uint32_t)a_percent * 255U ) / 100U;\n    (void)ledc_set_duty( DISPLAY_BACKLIGHT_LEDC_MODE, DISPLAY_BACKLIGHT_LEDC_CHANNEL, duty );\n    (void)ledc_update_duty( DISPLAY_BACKLIGHT_LEDC_MODE, DISPLAY_BACKLIGHT_LEDC_CHANNEL );\n}\n\nvoid display_clear( uint16_t a_color )',
)

# Build in configuration/time/device modules and cJSON.
patch(
    "main/CMakeLists.txt",
    '        "app_logging.c"\n        "radio_app_view.c"',
    '        "app_logging.c"\n        "device_config.c"\n        "device_time.c"\n        "device_view.c"\n        "radio_app_view.c"',
)
patch(
    "main/CMakeLists.txt",
    'REQUIRES esp_wifi esp_event esp_netif nvs_flash esp_timer driver esp_driver_i2c esp_driver_i2s esp_lcd fatfs sdmmc',
    'REQUIRES esp_wifi esp_event esp_netif nvs_flash esp_timer driver esp_driver_i2c esp_driver_i2s esp_lcd fatfs sdmmc json',
)

print("Device/settings integration patch applied")
