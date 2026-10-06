/**
    nrf24.c : RFScout nRF24 receive-only channel activity scanner
    Description: Sweeps nRF24 2.4 GHz channels using the RPD energy detector.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "nrf24.h"
#include "nrf24_activity.h"
#include "board.h"
#include "board_spi.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

#define NRF24_SPI_HOST SPI2_HOST
#define NRF24_CMD_R_REGISTER 0x00U
#define NRF24_CMD_W_REGISTER 0x20U
#define NRF24_CMD_NOP 0xFFU
#define NRF24_REG_CONFIG 0x00U
#define NRF24_REG_EN_AA 0x01U
#define NRF24_REG_EN_RXADDR 0x02U
#define NRF24_REG_SETUP_AW 0x03U
#define NRF24_REG_RF_CH 0x05U
#define NRF24_REG_RF_SETUP 0x06U
#define NRF24_REG_STATUS 0x07U
#define NRF24_REG_RPD 0x09U
#define NRF24_CONFIG_PRIM_RX 0x01U
#define NRF24_CONFIG_PWR_UP 0x02U
#define NRF24_MIN_RX_SETTLE_US 170U
#define NRF24_TASK_STACK_SIZE 3072U
#define NRF24_TASK_PRIORITY ( tskIDLE_PRIORITY + 1U )
#define NRF24_DELAY_SLICE_US 1000U
#define NRF24_CHANNELS_PER_SCHEDULER_BREAK 16U
#define NRF24_FRAME_NOTIFY_INTERVAL_MS 125U
#define NRF24_STOP_TIMEOUT_MS 250U
#define NRF24_PROBE_ATTEMPTS 3U
#define NRF24_PROBE_RETRY_US 100U

static const char *TAG = "nrf24";
static spi_device_handle_t device;
static TaskHandle_t scan_task;
static portMUX_TYPE state_lock = portMUX_INITIALIZER_UNLOCKED;
static bool initialized;
static bool detected;
static volatile bool running;
static volatile bool worker_active;
static volatile bool frame_ready;
static TickType_t last_frame_take_tick;
static uint8_t acquisition[ NRF24_CHANNEL_COUNT ];
static uint8_t completed[ NRF24_CHANNEL_COUNT ];
static nrf24_scan_config_t config = { .low_channel = 0U, .high_channel = 125U, .dwell_us = 250U };

static esp_err_t transfer( const uint8_t *a_tx, uint8_t *a_rx, size_t a_length )
{
    spi_transaction_t transaction = {
        .length = a_length * 8U, .rxlength = a_length * 8U, .tx_buffer = a_tx, .rx_buffer = a_rx };
    if( !board_spi_lock( RFSCOUT_BOARD_SPI_WAIT_FOREVER ) )
    {
        return ESP_ERR_TIMEOUT;
    }
    const esp_err_t result = spi_device_transmit( device, &transaction );
    board_spi_unlock();
    return result;
}

static esp_err_t read_register( uint8_t a_register, uint8_t *a_value )
{
    uint8_t tx[ 2 ] = { NRF24_CMD_R_REGISTER | ( a_register & 0x1FU ), NRF24_CMD_NOP };
    uint8_t rx[ 2 ] = { 0U, 0U };
    const esp_err_t result = transfer( tx, rx, sizeof( tx ) );
    if( ( ESP_OK == result ) && ( NULL != a_value ) )
    {
        *a_value = rx[ 1 ];
    }
    return result;
}

static esp_err_t write_register( uint8_t a_register, uint8_t a_value )
{
    uint8_t tx[ 2 ] = { NRF24_CMD_W_REGISTER | ( a_register & 0x1FU ), a_value };
    uint8_t rx[ 2 ] = { 0U, 0U };
    return transfer( tx, rx, sizeof( tx ) );
}

static void ce( bool a_enabled )
{
    gpio_set_level( RFSCOUT_BOARD_NRF24_CE_PIN, a_enabled ? 1 : 0 );
}

static esp_err_t begin_channel( uint8_t a_channel )
{
    ce( false );
    const esp_err_t result = write_register( NRF24_REG_RF_CH, a_channel );
    if( ESP_OK != result )
    {
        return result;
    }
    ce( true );
    return ESP_OK;
}

static nrf24_scan_config_t config_snapshot( void )
{
    portENTER_CRITICAL( &state_lock );
    const nrf24_scan_config_t snapshot = config;
    portEXIT_CRITICAL( &state_lock );
    return snapshot;
}

static void dwell_wait( uint16_t a_dwell_us )
{
    uint32_t remaining_us = a_dwell_us;
    const TickType_t one_ms_ticks = pdMS_TO_TICKS( 1U );

    while( running && ( remaining_us >= NRF24_DELAY_SLICE_US ) )
    {
        if( one_ms_ticks > 0U )
        {
            vTaskDelay( one_ms_ticks );
        }
        else
        {
            esp_rom_delay_us( NRF24_DELAY_SLICE_US );
        }
        remaining_us -= NRF24_DELAY_SLICE_US;
    }

    if( running && ( remaining_us > 0U ) )
    {
        esp_rom_delay_us( remaining_us );
    }
}

static void scan_worker( void *a_argument )
{
    (void)a_argument;

    while( true )
    {
        ulTaskNotifyTake( pdTRUE, portMAX_DELAY );
        worker_active = true;

        while( running )
        {
            const nrf24_scan_config_t snapshot = config_snapshot();
            uint16_t dwell_us = snapshot.dwell_us;
            if( dwell_us < NRF24_MIN_RX_SETTLE_US )
            {
                dwell_us = NRF24_MIN_RX_SETTLE_US;
            }

            memset( acquisition, 0, sizeof( acquisition ) );
            bool scan_ok = true;
            unsigned channels_since_break = 0U;

            for( uint16_t channel = snapshot.low_channel;
                 running && ( channel <= snapshot.high_channel );
                 ++channel )
            {
                esp_err_t result = begin_channel( (uint8_t)channel );
                if( ESP_OK != result )
                {
                    ESP_LOGW( TAG,
                              "RF_CH write failed for channel %u: %s",
                              (unsigned)channel,
                              esp_err_to_name( result ) );
                    ce( false );
                    scan_ok = false;
                    break;
                }

                dwell_wait( dwell_us );
                if( !running )
                {
                    ce( false );
                    break;
                }

                uint8_t rpd = 0U;
                result = read_register( NRF24_REG_RPD, &rpd );
                ce( false );
                if( ESP_OK != result )
                {
                    ESP_LOGW( TAG,
                              "RPD read failed for channel %u: %s",
                              (unsigned)channel,
                              esp_err_to_name( result ) );
                    scan_ok = false;
                    break;
                }

                acquisition[ channel ] = ( rpd & 0x01U ) ? 1U : 0U;
                ++channels_since_break;
                if( channels_since_break >= NRF24_CHANNELS_PER_SCHEDULER_BREAK )
                {
                    channels_since_break = 0U;
                    vTaskDelay( 1U );
                }
            }

            if( !running )
            {
                break;
            }
            if( !scan_ok )
            {
                running = false;
                break;
            }

            /* A completed sweep is one histogram observation. Keep this out
               of the display path so scan timing remains independent of LCD. */
            nrf24_activity_observe_sweep( acquisition, NRF24_CHANNEL_COUNT );

            portENTER_CRITICAL( &state_lock );
            memcpy( completed, acquisition, sizeof( completed ) );
            /* Multiple sweeps may complete before the UI consumes this flag.
               A bool intentionally coalesces them into one pending snapshot. */
            frame_ready = true;
            portEXIT_CRITICAL( &state_lock );
            vTaskDelay( 1U );
        }
        ce( false );
        worker_active = false;
    }
}

esp_err_t nrf24_probe( void )
{
    if( running )
    {
        return detected ? ESP_OK : ESP_ERR_NOT_FOUND;
    }

    gpio_set_direction( RFSCOUT_BOARD_NRF24_CE_PIN, GPIO_MODE_OUTPUT );
    ce( false );

    if( NULL == device )
    {
        gpio_reset_pin( RFSCOUT_BOARD_NRF24_CS_PIN );
        spi_device_interface_config_t device_config = { .clock_speed_hz = 4000000,
                                                        .mode = 0,
                                                        .spics_io_num = RFSCOUT_BOARD_NRF24_CS_PIN,
                                                        .queue_size = 1 };

        const esp_err_t attach_result =
            spi_bus_add_device( NRF24_SPI_HOST, &device_config, &device );
        if( ESP_OK != attach_result )
        {
            detected = false;
            return attach_result;
        }
    }

    esp_err_t result = ESP_OK;
    uint8_t setup_aw = 0xFFU;
    uint8_t rf_channel = 0xFFU;
    uint8_t status = 0xFFU;
    detected = false;

    for( unsigned attempt = 0U; attempt < NRF24_PROBE_ATTEMPTS; ++attempt )
    {
        setup_aw = 0xFFU;
        rf_channel = 0xFFU;
        status = 0xFFU;

        result = read_register( NRF24_REG_SETUP_AW, &setup_aw );
        if( ESP_OK == result )
        {
            result = read_register( NRF24_REG_RF_CH, &rf_channel );
        }
        if( ESP_OK == result )
        {
            result = read_register( NRF24_REG_STATUS, &status );
        }

        detected = ( ESP_OK == result ) && ( setup_aw >= 1U ) && ( setup_aw <= 3U ) &&
                   ( rf_channel <= 125U ) && ( 0xFFU != status );

        ESP_LOGD( TAG,
                  "probe attempt=%u setup_aw=0x%02x rf_ch=%u status=0x%02x detected=%d",
                  attempt + 1U,
                  setup_aw,
                  rf_channel,
                  status,
                  detected );
        if( detected )
        {
            break;
        }
        esp_rom_delay_us( NRF24_PROBE_RETRY_US );
    }

    ce( false );
    if( detected )
    {
        result = write_register( NRF24_REG_CONFIG, 0x00U );
        if( ESP_OK != result )
        {
            detected = false;
        }
    }

    if( !detected )
    {
        if( NULL != device )
        {
            (void)spi_bus_remove_device( device );
            device = NULL;
        }
        return ESP_OK == result ? ESP_ERR_NOT_FOUND : result;
    }
    return ESP_OK;
}

esp_err_t nrf24_init( void )
{
    if( initialized )
    {
        return detected ? ESP_OK : nrf24_probe();
    }

    if( !detected )
    {
        const esp_err_t probe_result = nrf24_probe();
        if( ESP_OK != probe_result )
        {
            return probe_result;
        }
    }

    memset( acquisition, 0, sizeof( acquisition ) );
    memset( completed, 0, sizeof( completed ) );

    if( NULL == scan_task )
    {
        if( pdPASS != xTaskCreate( scan_worker,
                                   "nrf24_scan",
                                   NRF24_TASK_STACK_SIZE,
                                   NULL,
                                   NRF24_TASK_PRIORITY,
                                   &scan_task ) )
        {
            scan_task = NULL;
            return ESP_ERR_NO_MEM;
        }
    }

    initialized = true;
    return ESP_OK;
}

esp_err_t nrf24_start( void )
{
    if( !initialized )
    {
        return ESP_ERR_INVALID_STATE;
    }

    portENTER_CRITICAL( &state_lock );
    const bool already_running = running;
    portEXIT_CRITICAL( &state_lock );
    if( already_running )
    {
        return ESP_OK;
    }

    ce( false );
    esp_err_t result = write_register( NRF24_REG_EN_AA, 0x00U );
    if( ESP_OK == result )
        result = write_register( NRF24_REG_EN_RXADDR, 0x00U );
    if( ESP_OK == result )
        result = write_register( NRF24_REG_RF_SETUP, 0x06U );
    if( ESP_OK == result )
    {
        result = write_register( NRF24_REG_CONFIG, NRF24_CONFIG_PWR_UP | NRF24_CONFIG_PRIM_RX );
    }
    if( ESP_OK != result )
    {
        return result;
    }

    portENTER_CRITICAL( &state_lock );
    memset( acquisition, 0, sizeof( acquisition ) );
    frame_ready = false;
    last_frame_take_tick = 0U;
    running = true;
    portEXIT_CRITICAL( &state_lock );
    xTaskNotifyGive( scan_task );
    return ESP_OK;
}

bool nrf24_stop( void )
{
    if( !initialized )
        return true;
    running = false;

    const TickType_t started = xTaskGetTickCount();
    const TickType_t timeout = pdMS_TO_TICKS( NRF24_STOP_TIMEOUT_MS );
    while( worker_active && ( xTaskGetTickCount() - started < timeout ) )
    {
        vTaskDelay( pdMS_TO_TICKS( 1U ) );
    }
    if( worker_active )
    {
        running = true;
        if( NULL != scan_task )
        {
            xTaskNotifyGive( scan_task );
        }
        ESP_LOGE( TAG,
                  "scan worker did not quiesce within %u ms; resuming scan",
                  (unsigned)NRF24_STOP_TIMEOUT_MS );
        return false;
    }

    ce( false );
    (void)write_register( NRF24_REG_CONFIG, 0x00U );
    return true;
}

void nrf24_poll( void )
{
}

bool nrf24_is_detected( void )
{
    return detected;
}

bool nrf24_is_active( void )
{
    return running;
}

const nrf24_scan_config_t *nrf24_config( void )
{
    return &config;
}

static void restart_scan( void )
{
    portENTER_CRITICAL( &state_lock );
    frame_ready = false;
    portEXIT_CRITICAL( &state_lock );
}

void nrf24_adjust_low( int a_delta )
{
    portENTER_CRITICAL( &state_lock );
    int value = (int)config.low_channel + a_delta;
    if( value < 0 )
        value = 0;
    if( value >= config.high_channel )
        value = (int)config.high_channel - 1;
    config.low_channel = (uint8_t)value;
    portEXIT_CRITICAL( &state_lock );
    restart_scan();
}

void nrf24_adjust_high( int a_delta )
{
    portENTER_CRITICAL( &state_lock );
    int value = (int)config.high_channel + a_delta;
    if( value > 125 )
        value = 125;
    if( value <= config.low_channel )
        value = (int)config.low_channel + 1;
    config.high_channel = (uint8_t)value;
    portEXIT_CRITICAL( &state_lock );
    restart_scan();
}

void nrf24_adjust_dwell( int a_delta )
{
    static const uint16_t dwell_values[] = { 200U, 250U, 500U, 1000U, 2000U, 5000U };

    portENTER_CRITICAL( &state_lock );
    size_t index = 0U;
    while( ( index + 1U < ( sizeof( dwell_values ) / sizeof( dwell_values[ 0 ] ) ) ) &&
           ( dwell_values[ index ] < config.dwell_us ) )
    {
        ++index;
    }

    if( ( a_delta > 0 ) &&
        ( index + 1U < ( sizeof( dwell_values ) / sizeof( dwell_values[ 0 ] ) ) ) )
    {
        ++index;
    }
    else if( ( a_delta < 0 ) && ( index > 0U ) )
    {
        --index;
    }

    config.dwell_us = dwell_values[ index ];
    portEXIT_CRITICAL( &state_lock );
    restart_scan();
}

bool nrf24_take_frame_ready( void )
{
    const TickType_t now = xTaskGetTickCount();
    const TickType_t minimum_interval = pdMS_TO_TICKS( NRF24_FRAME_NOTIFY_INTERVAL_MS );

    portENTER_CRITICAL( &state_lock );
    const bool interval_elapsed = ( 0U == last_frame_take_tick ) || ( 0U == minimum_interval ) ||
                                  ( ( now - last_frame_take_tick ) >= minimum_interval );
    const bool ready = frame_ready && interval_elapsed;
    if( ready )
    {
        /* Leave frame_ready asserted while throttled so completed sweeps are
           coalesced rather than discarded. The UI always receives the newest
           completed snapshot once the human-facing refresh interval elapses. */
        frame_ready = false;
        last_frame_take_tick = now;
    }
    portEXIT_CRITICAL( &state_lock );
    return ready;
}

size_t nrf24_spectrum( uint8_t *a_output, size_t a_capacity )
{
    if( ( NULL == a_output ) || ( 0U == a_capacity ) )
        return 0U;
    const size_t count = ( a_capacity < NRF24_CHANNEL_COUNT ) ? a_capacity : NRF24_CHANNEL_COUNT;
    portENTER_CRITICAL( &state_lock );
    memcpy( a_output, completed, count );
    portEXIT_CRITICAL( &state_lock );
    return count;
}
