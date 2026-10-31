/**
    cc1101.c : RFScout CC1101 receive-only radio service
    Description: Provides discovery sweeps and bounded fixed-frequency RMT captures.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "cc1101.h"
#include "board.h"
#include "board_spi.h"
#include "subghz_capture_provider.h"

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <stdint.h>
#include <string.h>

#define CC1101_SPI_HOST SPI2_HOST
#define CC1101_READ_BURST 0xC0U
#define CC1101_SRES 0x30U
#define CC1101_SIDLE 0x36U
#define CC1101_SRX 0x34U
#define CC1101_SPWD 0x39U

#define CC1101_IOCFG0 0x02U
#define CC1101_FIFOTHR 0x03U
#define CC1101_PKTCTRL0 0x08U
#define CC1101_FSCTRL1 0x0BU
#define CC1101_FREQ2 0x0DU
#define CC1101_FREQ1 0x0EU
#define CC1101_FREQ0 0x0FU
#define CC1101_MDMCFG4 0x10U
#define CC1101_MDMCFG3 0x11U
#define CC1101_MDMCFG2 0x12U
#define CC1101_MDMCFG1 0x13U
#define CC1101_MDMCFG0 0x14U
#define CC1101_MCSM0 0x18U
#define CC1101_FOCCFG 0x19U
#define CC1101_AGCCTRL2 0x1BU
#define CC1101_AGCCTRL1 0x1CU
#define CC1101_AGCCTRL0 0x1DU
#define CC1101_WORCTRL 0x20U
#define CC1101_FREND1 0x21U
#define CC1101_FREND0 0x22U

#define CC1101_RSSI 0x34U
#define CC1101_PARTNUM 0x30U
#define CC1101_VERSION 0x31U

#define CC1101_XOSC_HZ 26000000ULL
#define CC1101_SETTLE_US 180U
#define CC1101_TASK_STACK_SIZE 3072U
#define CC1101_TASK_PRIORITY 4U
#define CC1101_SAMPLES_PER_SCHEDULER_BREAK 16U
#define CC1101_STOP_TIMEOUT_MS 250U
#define CC1101_WAKE_TIMEOUT_US 1000U
#define CC1101_PROBE_ATTEMPTS 3U
#define CC1101_PROBE_RESET_SETTLE_US 100U
#define CC1101_SCAN_ERROR_RETRIES 3U

#define CC1101_RMT_RESOLUTION_HZ 500000U
#define CC1101_RMT_GLITCH_NS 2000U
#define CC1101_RMT_IDLE_NS 40000000U

static const char *TAG = "cc1101";
static spi_device_handle_t device;
static TaskHandle_t scan_task;
static portMUX_TYPE state_lock = portMUX_INITIALIZER_UNLOCKED;
static bool initialized;
static bool detected;
static bool service_started;
static volatile bool scan_running;
static volatile bool scan_worker_active;
static bool frame_ready;
static uint32_t config_generation;
static int8_t display_spectrum[ CC1101_SPECTRUM_BINS ];
static uint32_t display_frequency_khz[ CC1101_SPECTRUM_BINS ];
static cc1101_acquisition_mode_t acquisition_mode = CC1101_ACQUISITION_DISCOVERY;
static cc1101_scan_config_t scan_config = { CC1101_PROFILE_433, 433050U, 434790U, 15U };
static cc1101_receive_config_t receive_config = { 433920U, CC1101_RECEIVE_OOK_650 };

static esp_err_t transfer( uint8_t a_header, uint8_t a_data, uint8_t *a_result )
{
    spi_transaction_t transaction = { .length = 16, .rxlength = 16 };
    uint8_t tx[ 2 ] = { a_header, a_data };
    uint8_t rx[ 2 ] = { 0U, 0U };

    transaction.tx_buffer = tx;
    transaction.rx_buffer = rx;

    if( !board_spi_lock( RFSCOUT_BOARD_SPI_WAIT_FOREVER ) )
    {
        return ESP_ERR_TIMEOUT;
    }
    const esp_err_t result = spi_device_transmit( device, &transaction );
    board_spi_unlock();
    if( ( ESP_OK == result ) && ( NULL != a_result ) )
    {
        *a_result = rx[ 1 ];
    }
    return result;
}

static esp_err_t strobe( uint8_t a_command )
{
    return transfer( a_command, 0U, NULL );
}

static esp_err_t attach_spi_device( void )
{
    spi_device_interface_config_t device_config = { .clock_speed_hz = 4000000,
                                                    .mode = 0,
                                                    .spics_io_num = RFSCOUT_BOARD_CC1101_CS_PIN,
                                                    .queue_size = 1 };
    return spi_bus_add_device( CC1101_SPI_HOST, &device_config, &device );
}

static esp_err_t wake_from_power_down( void )
{
    esp_err_t result = board_spi_manual_acquire();
    if( ESP_OK != result )
    {
        return result;
    }

    if( NULL != device )
    {
        result = spi_bus_remove_device( device );
        if( ESP_OK != result )
        {
            board_spi_manual_release();
            return result;
        }
        device = NULL;
    }

    gpio_reset_pin( RFSCOUT_BOARD_CC1101_CS_PIN );
    gpio_set_direction( RFSCOUT_BOARD_CC1101_CS_PIN, GPIO_MODE_OUTPUT );
    gpio_set_level( RFSCOUT_BOARD_CC1101_CS_PIN, 1 );
    esp_rom_delay_us( 1U );
    gpio_set_level( RFSCOUT_BOARD_CC1101_CS_PIN, 0 );

    unsigned waited_us = 0U;
    while( ( 0 != gpio_get_level( RFSCOUT_BOARD_SPI_MISO_PIN ) ) &&
           ( waited_us < CC1101_WAKE_TIMEOUT_US ) )
    {
        esp_rom_delay_us( 1U );
        ++waited_us;
    }

    gpio_set_level( RFSCOUT_BOARD_CC1101_CS_PIN, 1 );
    board_spi_manual_release();

    if( waited_us >= CC1101_WAKE_TIMEOUT_US )
    {
        ESP_LOGE( TAG, "CC1101 wake timed out waiting for SO" );
        return ESP_ERR_TIMEOUT;
    }

    return attach_spi_device();
}

static esp_err_t write_register( uint8_t a_address, uint8_t a_value )
{
    return transfer( a_address, a_value, NULL );
}

static esp_err_t read_status( uint8_t a_address, uint8_t *a_value )
{
    return transfer( a_address | CC1101_READ_BURST, 0U, a_value );
}

static void select_rf_path( uint32_t a_frequency_khz )
{
    int sw1 = 0;
    int sw0 = 0;

    if( a_frequency_khz <= 348000U )
    {
        sw1 = 1;
    }
    else if( a_frequency_khz <= 464000U )
    {
        sw1 = 1;
        sw0 = 1;
    }
    else if( a_frequency_khz >= 779000U )
    {
        sw0 = 1;
    }

    gpio_set_level( RFSCOUT_BOARD_CC1101_SWITCH_1_PIN, sw1 );
    gpio_set_level( RFSCOUT_BOARD_CC1101_SWITCH_0_PIN, sw0 );
}

static void disable_rf_path( void )
{
    gpio_set_level( RFSCOUT_BOARD_CC1101_SWITCH_1_PIN, 0 );
    gpio_set_level( RFSCOUT_BOARD_CC1101_SWITCH_0_PIN, 0 );
}

static int8_t decode_rssi( uint8_t a_raw )
{
    const int16_t signed_raw = ( a_raw >= 128U ) ? ( (int16_t)a_raw - 256 ) : (int16_t)a_raw;
    const int16_t dbm = signed_raw / 2 - 74;
    /* CC1101 values below -128 dBm must saturate, not wrap to a positive RSSI. */
    return (int8_t)( dbm < INT8_MIN ? INT8_MIN : dbm );
}

static bool frequency_supported( uint32_t a_frequency_khz )
{
    return ( ( a_frequency_khz >= 300000U ) && ( a_frequency_khz <= 348000U ) ) ||
           ( ( a_frequency_khz >= 387000U ) && ( a_frequency_khz <= 464000U ) ) ||
           ( ( a_frequency_khz >= 779000U ) && ( a_frequency_khz <= 928000U ) );
}

static size_t sweep_count_for( uint32_t a_low_khz, uint32_t a_high_khz, uint32_t a_step_khz )
{
    if( ( 0U == a_step_khz ) || ( a_high_khz <= a_low_khz ) )
    {
        return 1U;
    }

    return (size_t)( ( a_high_khz - a_low_khz ) / a_step_khz ) + 1U;
}

static bool valid_sweep( uint32_t a_low_khz, uint32_t a_high_khz, uint32_t a_step_khz )
{
    return frequency_supported( a_low_khz ) && frequency_supported( a_high_khz ) &&
           ( a_high_khz > a_low_khz ) &&
           ( sweep_count_for( a_low_khz, a_high_khz, a_step_khz ) <= CC1101_MAX_SWEEP_SAMPLES );
}

static cc1101_scan_config_t scan_config_snapshot( uint32_t *a_generation )
{
    portENTER_CRITICAL( &state_lock );
    const cc1101_scan_config_t snapshot = scan_config;
    if( NULL != a_generation )
    {
        *a_generation = config_generation;
    }
    portEXIT_CRITICAL( &state_lock );
    return snapshot;
}

static uint32_t frequency_for_sample( const cc1101_scan_config_t *a_config, size_t a_index )
{
    uint64_t frequency = (uint64_t)a_config->low_khz + ( (uint64_t)a_index * a_config->step_khz );

    if( frequency > a_config->high_khz )
    {
        frequency = a_config->high_khz;
    }
    return (uint32_t)frequency;
}

static size_t display_bin_for_sample( size_t a_index, size_t a_count )
{
    if( a_count <= 1U )
    {
        return 0U;
    }

    return ( a_index * ( CC1101_SPECTRUM_BINS - 1U ) ) / ( a_count - 1U );
}

static esp_err_t tune( uint32_t a_frequency_khz )
{
    if( !frequency_supported( a_frequency_khz ) )
    {
        return ESP_ERR_INVALID_ARG;
    }

    select_rf_path( a_frequency_khz );
    const uint64_t word = ( (uint64_t)a_frequency_khz * 1000ULL << 16 ) / CC1101_XOSC_HZ;

    esp_err_t result = strobe( CC1101_SIDLE );
    if( ESP_OK == result )
    {
        result = write_register( CC1101_FREQ2, (uint8_t)( ( word >> 16 ) & 0xFFU ) );
    }
    if( ESP_OK == result )
    {
        result = write_register( CC1101_FREQ1, (uint8_t)( ( word >> 8 ) & 0xFFU ) );
    }
    if( ESP_OK == result )
    {
        result = write_register( CC1101_FREQ0, (uint8_t)( word & 0xFFU ) );
    }
    if( ESP_OK == result )
    {
        result = strobe( CC1101_SRX );
    }
    return result;
}

static void scan_worker( void *a_argument )
{
    (void)a_argument;
    int8_t acquisition[ CC1101_SPECTRUM_BINS ];
    uint32_t acquisition_frequency_khz[ CC1101_SPECTRUM_BINS ];

    while( true )
    {
        ulTaskNotifyTake( pdTRUE, portMAX_DELAY );
        scan_worker_active = true;

        while( scan_running )
        {
            uint32_t generation = 0U;
            const cc1101_scan_config_t snapshot = scan_config_snapshot( &generation );
            const size_t count =
                sweep_count_for( snapshot.low_khz, snapshot.high_khz, snapshot.step_khz );
            memset( acquisition, -120, sizeof( acquisition ) );
            memset( acquisition_frequency_khz, 0, sizeof( acquisition_frequency_khz ) );
            bool scan_ok = true;
            esp_err_t scan_error = ESP_OK;
            unsigned samples_since_break = 0U;

            for( size_t index = 0U; scan_running && ( index < count ); ++index )
            {
                const uint32_t frequency = frequency_for_sample( &snapshot, index );
                esp_err_t result = tune( frequency );
                if( ESP_OK != result )
                {
                    scan_ok = false;
                    scan_error = result;
                    break;
                }

                esp_rom_delay_us( CC1101_SETTLE_US );
                if( !scan_running )
                {
                    break;
                }

                uint8_t raw_rssi = 0U;
                result = read_status( CC1101_RSSI, &raw_rssi );
                if( ESP_OK != result )
                {
                    scan_ok = false;
                    scan_error = result;
                    break;
                }

                const size_t bin = display_bin_for_sample( index, count );
                const int8_t rssi = decode_rssi( raw_rssi );
                if( rssi > acquisition[ bin ] )
                {
                    acquisition[ bin ] = rssi;
                    acquisition_frequency_khz[ bin ] = frequency;
                }

                ++samples_since_break;
                if( samples_since_break >= CC1101_SAMPLES_PER_SCHEDULER_BREAK )
                {
                    samples_since_break = 0U;
                    vTaskDelay( 1U );
                }
            }

            if( !scan_running )
            {
                break;
            }
            if( !scan_ok )
            {
                bool recovered = false;
                for( unsigned attempt = 0U; scan_running && ( attempt < CC1101_SCAN_ERROR_RETRIES );
                     ++attempt )
                {
                    ESP_LOGW( TAG,
                              "discovery scan error %s; retry %u/%u",
                              esp_err_to_name( scan_error ),
                              attempt + 1U,
                              (unsigned)CC1101_SCAN_ERROR_RETRIES );
                    vTaskDelay( pdMS_TO_TICKS( 10U ) );
                    const uint32_t probe_frequency = frequency_for_sample( &snapshot, 0U );
                    scan_error = tune( probe_frequency );
                    if( ESP_OK == scan_error )
                    {
                        recovered = true;
                        break;
                    }
                }
                if( !recovered )
                {
                    ESP_LOGE( TAG,
                              "discovery scan stopped after repeated radio errors: %s",
                              esp_err_to_name( scan_error ) );
                    portENTER_CRITICAL( &state_lock );
                    detected = false;
                    scan_running = false;
                    frame_ready = false;
                    portEXIT_CRITICAL( &state_lock );
                    disable_rf_path();
                    break;
                }
                continue;
            }

            portENTER_CRITICAL( &state_lock );
            if( generation == config_generation )
            {
                memcpy( display_spectrum, acquisition, sizeof( display_spectrum ) );
                memcpy( display_frequency_khz,
                        acquisition_frequency_khz,
                        sizeof( display_frequency_khz ) );
                frame_ready = true;
            }
            portEXIT_CRITICAL( &state_lock );

            vTaskDelay( 1U );
        }

        scan_worker_active = false;
    }
}

static bool stop_scan_worker( void )
{
    const bool was_running = scan_running;
    scan_running = false;
    const TickType_t started = xTaskGetTickCount();
    const TickType_t timeout = pdMS_TO_TICKS( CC1101_STOP_TIMEOUT_MS );

    while( scan_worker_active && ( xTaskGetTickCount() - started < timeout ) )
    {
        vTaskDelay( pdMS_TO_TICKS( 1U ) );
    }

    if( scan_worker_active )
    {
        ESP_LOGE(
            TAG, "scan worker did not quiesce within %u ms", (unsigned)CC1101_STOP_TIMEOUT_MS );
        if( was_running )
        {
            scan_running = true;
            xTaskNotifyGive( scan_task );
        }
        return false;
    }
    return true;
}

static void start_scan_worker( void )
{
    portENTER_CRITICAL( &state_lock );
    frame_ready = false;
    scan_running = true;
    portEXIT_CRITICAL( &state_lock );
    xTaskNotifyGive( scan_task );
}

static esp_err_t configure_receive_chain( cc1101_receive_preset_t a_preset )
{
    const bool wide = ( CC1101_RECEIVE_OOK_650 == a_preset );

    esp_err_t result = strobe( CC1101_SIDLE );
    if( ESP_OK == result )
    {
        result = write_register( CC1101_IOCFG0, 0x0DU );
    }
    if( ESP_OK == result )
    {
        result = write_register( CC1101_FIFOTHR, wide ? 0x07U : 0x47U );
    }
    if( ESP_OK == result )
    {
        result = write_register( CC1101_PKTCTRL0, 0x32U );
    }
    if( ESP_OK == result )
    {
        result = write_register( CC1101_FSCTRL1, 0x06U );
    }
    if( ESP_OK == result )
    {
        result = write_register( CC1101_MDMCFG0, 0x00U );
    }
    if( ESP_OK == result )
    {
        result = write_register( CC1101_MDMCFG1, 0x00U );
    }
    if( ESP_OK == result )
    {
        result = write_register( CC1101_MDMCFG2, 0x30U );
    }
    if( ESP_OK == result )
    {
        result = write_register( CC1101_MDMCFG3, 0x32U );
    }
    if( ESP_OK == result )
    {
        result = write_register( CC1101_MDMCFG4, wide ? 0x17U : 0x67U );
    }
    if( ESP_OK == result )
    {
        result = write_register( CC1101_MCSM0, 0x18U );
    }
    if( ESP_OK == result )
    {
        result = write_register( CC1101_FOCCFG, 0x18U );
    }
    if( ESP_OK == result )
    {
        result = write_register( CC1101_AGCCTRL0, wide ? 0x91U : 0x40U );
    }
    if( ESP_OK == result )
    {
        result = write_register( CC1101_AGCCTRL1, 0x00U );
    }
    if( ESP_OK == result )
    {
        result = write_register( CC1101_AGCCTRL2, wide ? 0x07U : 0x03U );
    }
    if( ESP_OK == result )
    {
        result = write_register( CC1101_WORCTRL, 0xFBU );
    }
    if( ESP_OK == result )
    {
        result = write_register( CC1101_FREND0, 0x11U );
    }
    if( ESP_OK == result )
    {
        result = write_register( CC1101_FREND1, 0xB6U );
    }
    return result;
}

static esp_err_t start_receive_acquisition( void )
{
    esp_err_t result = configure_receive_chain( receive_config.preset );
    if( ESP_OK == result )
    {
        result = tune( receive_config.frequency_khz );
    }
    if( ESP_OK != result )
    {
        return result;
    }

    const rfscout_subghz_capture_provider_config_t capture_config = {
        .gpio_num = RFSCOUT_BOARD_CC1101_GDO0_PIN,
        .resolution_hz = CC1101_RMT_RESOLUTION_HZ,
        .signal_range_min_ns = CC1101_RMT_GLITCH_NS,
        .signal_range_max_ns = CC1101_RMT_IDLE_NS,
        .frequency_hz = receive_config.frequency_khz * 1000U,
        .modulation = RFSCOUT_SUBGHZ_MODULATION_OOK,
    };

    result = subghz_capture_provider_start( &capture_config );
    if( ESP_OK != result )
    {
        ESP_LOGE( TAG, "capture provider unavailable: %s", esp_err_to_name( result ) );
        (void)strobe( CC1101_SIDLE );
        return result;
    }

    return ESP_OK;
}

static bool stop_receive_acquisition( void )
{
    if( !subghz_capture_provider_stop() )
    {
        return false;
    }
    (void)strobe( CC1101_SIDLE );
    return true;
}

static esp_err_t apply_selected_mode( void )
{
    if( CC1101_ACQUISITION_RECEIVE == acquisition_mode )
    {
        return start_receive_acquisition();
    }

    const esp_err_t result = configure_receive_chain( CC1101_RECEIVE_OOK_650 );
    if( ESP_OK != result )
    {
        return result;
    }

    start_scan_worker();
    return ESP_OK;
}

esp_err_t cc1101_probe( void )
{
    if( service_started )
    {
        return detected ? ESP_OK : ESP_ERR_NOT_FOUND;
    }

    disable_rf_path();

    esp_err_t result = wake_from_power_down();
    if( ESP_OK != result )
    {
        detected = false;
        return result;
    }

    uint8_t part = 0xFFU;
    uint8_t version = 0xFFU;
    detected = false;

    for( unsigned attempt = 0U; attempt < CC1101_PROBE_ATTEMPTS; ++attempt )
    {
        result = strobe( CC1101_SRES );
        if( ESP_OK != result )
        {
            break;
        }

        esp_rom_delay_us( CC1101_PROBE_RESET_SETTLE_US );

        part = 0xFFU;
        version = 0xFFU;
        result = read_status( CC1101_PARTNUM, &part );
        if( ESP_OK == result )
        {
            result = read_status( CC1101_VERSION, &version );
        }

        detected = ( ESP_OK == result ) && ( 0x00U == part ) && ( 0x00U != version ) &&
                   ( 0xFFU != version );
        ESP_LOGD( TAG,
                  "probe attempt=%u part=0x%02x version=0x%02x detected=%d",
                  attempt + 1U,
                  part,
                  version,
                  detected );
        if( detected )
        {
            break;
        }

        esp_rom_delay_us( CC1101_PROBE_RESET_SETTLE_US );
    }

    (void)strobe( CC1101_SIDLE );
    (void)strobe( CC1101_SPWD );
    disable_rf_path();

    if( !detected )
    {
        return ESP_OK == result ? ESP_ERR_NOT_FOUND : result;
    }
    return ESP_OK;
}

esp_err_t cc1101_init( void )
{
    if( initialized )
    {
        return detected ? ESP_OK : cc1101_probe();
    }

    if( !detected )
    {
        const esp_err_t probe_result = cc1101_probe();
        if( ESP_OK != probe_result )
        {
            return probe_result;
        }
    }

    memset( display_spectrum, -120, sizeof( display_spectrum ) );
    memset( display_frequency_khz, 0, sizeof( display_frequency_khz ) );

    if( NULL == scan_task )
    {
        if( pdPASS != xTaskCreate( scan_worker,
                                   "cc1101_scan",
                                   CC1101_TASK_STACK_SIZE,
                                   NULL,
                                   CC1101_TASK_PRIORITY,
                                   &scan_task ) )
        {
            scan_task = NULL;
            return ESP_ERR_NO_MEM;
        }
    }

    initialized = true;
    return ESP_OK;
}

esp_err_t cc1101_start( void )
{
    if( !initialized )
    {
        return ESP_ERR_INVALID_STATE;
    }
    if( service_started )
    {
        return ESP_OK;
    }

    esp_err_t result = wake_from_power_down();
    if( ESP_OK != result )
    {
        return result;
    }

    result = write_register( CC1101_MCSM0, 0x18U );
    if( ESP_OK != result )
    {
        return result;
    }

    service_started = true;
    result = apply_selected_mode();
    if( ESP_OK != result )
    {
        service_started = false;
        (void)strobe( CC1101_SIDLE );
        (void)strobe( CC1101_SPWD );
        disable_rf_path();
        return result;
    }
    return ESP_OK;
}

bool cc1101_stop( void )
{
    if( !initialized || !service_started )
    {
        return true;
    }

    if( !stop_scan_worker() )
    {
        return false;
    }

    if( !stop_receive_acquisition() )
    {
        return false;
    }
    (void)strobe( CC1101_SIDLE );
    (void)strobe( CC1101_SPWD );
    disable_rf_path();
    service_started = false;
    return true;
}

void cc1101_poll( void )
{
    if( !service_started || ( CC1101_ACQUISITION_RECEIVE != acquisition_mode ) )
    {
        return;
    }

    subghz_capture_provider_poll();
}

bool cc1101_is_detected( void )
{
    return detected;
}

bool cc1101_is_active( void )
{
    return service_started;
}

cc1101_acquisition_mode_t cc1101_acquisition_mode( void )
{
    portENTER_CRITICAL( &state_lock );
    const cc1101_acquisition_mode_t mode = acquisition_mode;
    portEXIT_CRITICAL( &state_lock );
    return mode;
}

esp_err_t cc1101_set_acquisition_mode( cc1101_acquisition_mode_t a_mode )
{
    if( ( CC1101_ACQUISITION_DISCOVERY != a_mode ) && ( CC1101_ACQUISITION_RECEIVE != a_mode ) )
    {
        return ESP_ERR_INVALID_ARG;
    }

    portENTER_CRITICAL( &state_lock );
    const cc1101_acquisition_mode_t before = acquisition_mode;
    portEXIT_CRITICAL( &state_lock );
    if( before == a_mode )
    {
        return ESP_OK;
    }

    if( service_started )
    {
        if( !stop_scan_worker() )
        {
            return ESP_ERR_TIMEOUT;
        }
        if( !stop_receive_acquisition() )
        {
            return ESP_FAIL;
        }
    }

    portENTER_CRITICAL( &state_lock );
    acquisition_mode = a_mode;
    frame_ready = false;
    portEXIT_CRITICAL( &state_lock );

    if( service_started )
    {
        const esp_err_t result = apply_selected_mode();
        if( ESP_OK != result )
        {
            portENTER_CRITICAL( &state_lock );
            acquisition_mode = before;
            portEXIT_CRITICAL( &state_lock );

            const esp_err_t restore_result = apply_selected_mode();
            if( ESP_OK != restore_result )
            {
                ESP_LOGE( TAG,
                          "could not restore acquisition mode after transition failure: %s",
                          esp_err_to_name( restore_result ) );
                portENTER_CRITICAL( &state_lock );
                detected = false;
                frame_ready = false;
                portEXIT_CRITICAL( &state_lock );
                service_started = false;
                (void)strobe( CC1101_SIDLE );
                (void)strobe( CC1101_SPWD );
                disable_rf_path();
                return restore_result;
            }
            return result;
        }
    }
    return ESP_OK;
}

void cc1101_scan_config( cc1101_scan_config_t *a_output )
{
    if( NULL == a_output )
    {
        return;
    }
    portENTER_CRITICAL( &state_lock );
    *a_output = scan_config;
    portEXIT_CRITICAL( &state_lock );
}

void cc1101_set_profile( cc1101_profile_t a_profile )
{
    if( a_profile >= CC1101_PROFILE_COUNT )
    {
        return;
    }

    portENTER_CRITICAL( &state_lock );
    scan_config.profile = a_profile;

    switch( a_profile )
    {
        case CC1101_PROFILE_433:
            scan_config.low_khz = 433050U;
            scan_config.high_khz = 434790U;
            scan_config.step_khz = 15U;
            break;
        case CC1101_PROFILE_315:
            scan_config.low_khz = 300000U;
            scan_config.high_khz = 348000U;
            scan_config.step_khz = 500U;
            break;
        case CC1101_PROFILE_868:
            scan_config.low_khz = 863000U;
            scan_config.high_khz = 870000U;
            scan_config.step_khz = 100U;
            break;
        case CC1101_PROFILE_915:
            scan_config.low_khz = 902000U;
            scan_config.high_khz = 928000U;
            scan_config.step_khz = 250U;
            break;
        case CC1101_PROFILE_MANUAL:
        default:
            break;
    }

    ++config_generation;
    frame_ready = false;
    portEXIT_CRITICAL( &state_lock );
}

static uint32_t clamp_band( uint32_t a_frequency_khz )
{
    if( a_frequency_khz < 300000U )
    {
        return 300000U;
    }
    if( a_frequency_khz > 928000U )
    {
        return 928000U;
    }
    if( ( a_frequency_khz > 348000U ) && ( a_frequency_khz < 387000U ) )
    {
        return a_frequency_khz < 367500U ? 348000U : 387000U;
    }
    if( ( a_frequency_khz > 464000U ) && ( a_frequency_khz < 779000U ) )
    {
        return a_frequency_khz < 621500U ? 464000U : 779000U;
    }
    return a_frequency_khz;
}

bool cc1101_adjust_low( int a_delta )
{
    portENTER_CRITICAL( &state_lock );

    int64_t frequency = (int64_t)scan_config.low_khz + ( (int64_t)a_delta * scan_config.step_khz );
    if( frequency < 300000LL )
    {
        frequency = 300000LL;
    }

    uint32_t candidate = clamp_band( (uint32_t)frequency );
    if( candidate >= scan_config.high_khz )
    {
        candidate = scan_config.high_khz - scan_config.step_khz;
    }

    const bool valid = valid_sweep( candidate, scan_config.high_khz, scan_config.step_khz );
    if( valid )
    {
        scan_config.low_khz = candidate;
        scan_config.profile = CC1101_PROFILE_MANUAL;
        ++config_generation;
        frame_ready = false;
    }

    portEXIT_CRITICAL( &state_lock );
    return valid;
}

bool cc1101_adjust_high( int a_delta )
{
    portENTER_CRITICAL( &state_lock );

    int64_t frequency = (int64_t)scan_config.high_khz + ( (int64_t)a_delta * scan_config.step_khz );
    if( frequency > 928000LL )
    {
        frequency = 928000LL;
    }

    uint32_t candidate = clamp_band( (uint32_t)frequency );
    if( candidate <= scan_config.low_khz )
    {
        candidate = scan_config.low_khz + scan_config.step_khz;
    }

    const bool valid = valid_sweep( scan_config.low_khz, candidate, scan_config.step_khz );
    if( valid )
    {
        scan_config.high_khz = candidate;
        scan_config.profile = CC1101_PROFILE_MANUAL;
        ++config_generation;
        frame_ready = false;
    }

    portEXIT_CRITICAL( &state_lock );
    return valid;
}

bool cc1101_adjust_step( int a_delta )
{
    static const uint32_t steps[] = { 1U, 5U, 10U, 15U, 25U, 50U, 100U, 200U, 250U, 500U, 1000U };

    portENTER_CRITICAL( &state_lock );

    size_t index = 0U;
    while( ( index + 1U < ( sizeof( steps ) / sizeof( steps[ 0 ] ) ) ) &&
           ( steps[ index ] < scan_config.step_khz ) )
    {
        ++index;
    }

    size_t candidate_index = index;
    if( ( a_delta > 0 ) && ( candidate_index + 1U < ( sizeof( steps ) / sizeof( steps[ 0 ] ) ) ) )
    {
        ++candidate_index;
    }
    else if( ( a_delta < 0 ) && ( candidate_index > 0U ) )
    {
        --candidate_index;
    }
    else
    {
        portEXIT_CRITICAL( &state_lock );
        return false;
    }

    const uint32_t candidate = steps[ candidate_index ];
    const bool valid = valid_sweep( scan_config.low_khz, scan_config.high_khz, candidate );
    if( valid )
    {
        scan_config.step_khz = candidate;
        scan_config.profile = CC1101_PROFILE_MANUAL;
        ++config_generation;
        frame_ready = false;
    }

    portEXIT_CRITICAL( &state_lock );
    return valid;
}

size_t cc1101_sweep_count( void )
{
    const cc1101_scan_config_t snapshot = scan_config_snapshot( NULL );
    return sweep_count_for( snapshot.low_khz, snapshot.high_khz, snapshot.step_khz );
}

const char *cc1101_profile_name( cc1101_profile_t a_profile )
{
    static const char *names[] = { "433 ISM", "315 MHz", "868 MHz", "915 MHz", "MANUAL" };
    return a_profile < CC1101_PROFILE_COUNT ? names[ a_profile ] : "?";
}

size_t cc1101_spectrum( int8_t *a_output, uint32_t *a_frequency_khz, size_t a_capacity )
{
    if( ( NULL == a_output ) || ( NULL == a_frequency_khz ) || ( 0U == a_capacity ) )
    {
        return 0U;
    }

    const size_t count = a_capacity < CC1101_SPECTRUM_BINS ? a_capacity : CC1101_SPECTRUM_BINS;
    portENTER_CRITICAL( &state_lock );
    memcpy( a_output, display_spectrum, count );
    memcpy( a_frequency_khz, display_frequency_khz, count * sizeof( *a_frequency_khz ) );
    portEXIT_CRITICAL( &state_lock );
    return count;
}

bool cc1101_take_frame_ready( void )
{
    portENTER_CRITICAL( &state_lock );
    const bool ready = frame_ready;
    frame_ready = false;
    portEXIT_CRITICAL( &state_lock );
    return ready;
}

void cc1101_receive_config( cc1101_receive_config_t *a_output )
{
    if( NULL == a_output )
    {
        return;
    }
    portENTER_CRITICAL( &state_lock );
    *a_output = receive_config;
    portEXIT_CRITICAL( &state_lock );
}

static void recover_receive_failure_to_discovery( void )
{
    portENTER_CRITICAL( &state_lock );
    acquisition_mode = CC1101_ACQUISITION_DISCOVERY;
    frame_ready = false;
    portEXIT_CRITICAL( &state_lock );
    start_scan_worker();
    ESP_LOGW( TAG, "receive acquisition restore failed; recovered to discovery mode" );
}

esp_err_t cc1101_set_receive_frequency( uint32_t a_frequency_khz )
{
    if( !frequency_supported( a_frequency_khz ) )
    {
        return ESP_ERR_INVALID_ARG;
    }

    portENTER_CRITICAL( &state_lock );
    const uint32_t before = receive_config.frequency_khz;
    receive_config.frequency_khz = a_frequency_khz;
    portEXIT_CRITICAL( &state_lock );

    if( service_started && ( CC1101_ACQUISITION_RECEIVE == acquisition_mode ) )
    {
        if( !stop_receive_acquisition() )
        {
            portENTER_CRITICAL( &state_lock );
            receive_config.frequency_khz = before;
            portEXIT_CRITICAL( &state_lock );
            return ESP_FAIL;
        }

        const esp_err_t result = start_receive_acquisition();
        if( ESP_OK != result )
        {
            portENTER_CRITICAL( &state_lock );
            receive_config.frequency_khz = before;
            portEXIT_CRITICAL( &state_lock );
            const esp_err_t restore_result = start_receive_acquisition();
            if( ESP_OK != restore_result )
            {
                recover_receive_failure_to_discovery();
            }
            return result;
        }
    }
    return ESP_OK;
}

esp_err_t cc1101_set_receive_preset( cc1101_receive_preset_t a_preset )
{
    if( a_preset >= CC1101_RECEIVE_PRESET_COUNT )
    {
        return ESP_ERR_INVALID_ARG;
    }

    portENTER_CRITICAL( &state_lock );
    const cc1101_receive_preset_t before = receive_config.preset;
    receive_config.preset = a_preset;
    portEXIT_CRITICAL( &state_lock );

    if( service_started && ( CC1101_ACQUISITION_RECEIVE == acquisition_mode ) )
    {
        if( !stop_receive_acquisition() )
        {
            portENTER_CRITICAL( &state_lock );
            receive_config.preset = before;
            portEXIT_CRITICAL( &state_lock );
            return ESP_FAIL;
        }

        const esp_err_t result = start_receive_acquisition();
        if( ESP_OK != result )
        {
            portENTER_CRITICAL( &state_lock );
            receive_config.preset = before;
            portEXIT_CRITICAL( &state_lock );
            const esp_err_t restore_result = start_receive_acquisition();
            if( ESP_OK != restore_result )
            {
                recover_receive_failure_to_discovery();
            }
            return result;
        }
    }
    return ESP_OK;
}

const char *cc1101_receive_preset_name( cc1101_receive_preset_t a_preset )
{
    static const char *names[] = { "OOK 650", "OOK 270" };
    return a_preset < CC1101_RECEIVE_PRESET_COUNT ? names[ a_preset ] : "?";
}

bool cc1101_capture_acquire( rfscout_subghz_capture_t *a_capture, uint8_t *a_lease )
{
    return subghz_capture_provider_acquire( a_capture, a_lease );
}

void cc1101_capture_release( uint8_t a_lease )
{
    subghz_capture_provider_release( a_lease );
}

void cc1101_capture_stats( cc1101_capture_stats_t *a_output )
{
    if( NULL == a_output )
    {
        return;
    }

    rfscout_subghz_capture_stats_t stats;
    subghz_capture_provider_stats( &stats );
    a_output->captures_completed = stats.captures_completed;
    a_output->captures_truncated = stats.captures_truncated;
    a_output->capture_overruns = stats.capture_overruns;
    a_output->capture_errors = stats.capture_errors;
}
