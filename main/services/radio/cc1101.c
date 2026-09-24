/**
    cc1101.c : RFScout CC1101 receive-only radio service
    Description: Provides discovery sweeps and bounded fixed-frequency RMT captures.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "cc1101.h"
#include "board.h"

#include "driver/gpio.h"
#include "driver/rmt_rx.h"
#include "driver/spi_master.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <limits.h>
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

#define CC1101_RMT_RESOLUTION_HZ 1000000U
#define CC1101_RMT_GLITCH_NS 2000U
#define CC1101_RMT_IDLE_NS 20000000U
#define CC1101_RMT_ALIGNMENT 64U
#define CC1101_RECEIVE_RSSI_POLL_MS 4U

typedef enum
{
    CAPTURE_SLOT_FREE = 0,
    CAPTURE_SLOT_FILLING,
    CAPTURE_SLOT_READY,
    CAPTURE_SLOT_PROCESSING,
} capture_slot_state_t;

typedef struct
{
    rfscout_subghz_symbol_t *symbols;
    capture_slot_state_t state;
    size_t symbol_count;
    uint32_t frequency_hz;
    uint32_t resolution_hz;
    rfscout_subghz_modulation_t modulation;
    uint32_t sequence;
    uint32_t started_ms;
    uint32_t ended_ms;
    int8_t peak_rssi_dbm;
    rfscout_subghz_capture_end_t end_reason;
    bool truncated;
} capture_slot_t;

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
static cc1101_acquisition_mode_t acquisition_mode = CC1101_ACQUISITION_DISCOVERY;
static cc1101_scan_config_t scan_config = { CC1101_PROFILE_433, 433050U, 434790U, 15U };
static cc1101_receive_config_t receive_config = { 433920U, CC1101_RECEIVE_OOK_650 };

static rmt_channel_handle_t rmt_channel;
static bool rmt_enabled;
static capture_slot_t capture_pool[ CC1101_CAPTURE_POOL_SIZE ];
static int filling_slot = -1;
static bool waiting_for_capture_slot;
static uint32_t next_capture_sequence = 1U;
static cc1101_capture_stats_t capture_stats_state;
static TickType_t last_receive_rssi_tick;

static const rmt_receive_config_t rmt_receive_config = {
    .signal_range_min_ns = CC1101_RMT_GLITCH_NS,
    .signal_range_max_ns = CC1101_RMT_IDLE_NS,
    .flags.en_partial_rx = false,
};

static esp_err_t transfer( uint8_t a_header, uint8_t a_data, uint8_t *a_result )
{
    spi_transaction_t transaction = { .length = 16, .rxlength = 16 };
    uint8_t tx[ 2 ] = { a_header, a_data };
    uint8_t rx[ 2 ] = { 0U, 0U };

    transaction.tx_buffer = tx;
    transaction.rx_buffer = rx;

    const esp_err_t result = spi_device_transmit( device, &transaction );
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
    if( NULL != device )
    {
        const esp_err_t remove_result = spi_bus_remove_device( device );
        if( ESP_OK != remove_result )
        {
            return remove_result;
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
    return (int8_t)( signed_raw / 2 - 74 );
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
            bool scan_ok = true;
            unsigned samples_since_break = 0U;

            for( size_t index = 0U; scan_running && ( index < count ); ++index )
            {
                const uint32_t frequency = frequency_for_sample( &snapshot, index );
                esp_err_t result = tune( frequency );
                if( ESP_OK != result )
                {
                    scan_ok = false;
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
                    break;
                }

                const size_t bin = display_bin_for_sample( index, count );
                const int8_t rssi = decode_rssi( raw_rssi );
                if( rssi > acquisition[ bin ] )
                {
                    acquisition[ bin ] = rssi;
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
                scan_running = false;
                disable_rf_path();
                break;
            }

            portENTER_CRITICAL( &state_lock );
            if( generation == config_generation )
            {
                memcpy( display_spectrum, acquisition, sizeof( display_spectrum ) );
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
    scan_running = false;
    const TickType_t started = xTaskGetTickCount();
    const TickType_t timeout = pdMS_TO_TICKS( CC1101_STOP_TIMEOUT_MS );

    while( scan_worker_active && ( xTaskGetTickCount() - started < timeout ) )
    {
        vTaskDelay( pdMS_TO_TICKS( 1U ) );
    }

    if( scan_worker_active )
    {
        ESP_LOGE( TAG, "scan worker did not quiesce within %u ms", (unsigned)CC1101_STOP_TIMEOUT_MS );
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

static esp_err_t configure_ook_receive( cc1101_receive_preset_t a_preset )
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

static uint32_t capture_now_ms_from_isr( void )
{
    return (uint32_t)( xTaskGetTickCountFromISR() * portTICK_PERIOD_MS );
}

static uint32_t capture_now_ms( void )
{
    return (uint32_t)( xTaskGetTickCount() * portTICK_PERIOD_MS );
}

static int find_free_capture_slot_locked( void )
{
    for( size_t index = 0U; index < CC1101_CAPTURE_POOL_SIZE; ++index )
    {
        if( CAPTURE_SLOT_FREE == capture_pool[ index ].state )
        {
            return (int)index;
        }
    }
    return -1;
}

static void initialize_capture_slot_locked( int a_index, uint32_t a_started_ms )
{
    capture_slot_t *slot = &capture_pool[ a_index ];
    slot->state = CAPTURE_SLOT_FILLING;
    slot->symbol_count = 0U;
    slot->frequency_hz = receive_config.frequency_khz * 1000U;
    slot->resolution_hz = CC1101_RMT_RESOLUTION_HZ;
    slot->modulation = RFSCOUT_SUBGHZ_MODULATION_OOK;
    slot->sequence = next_capture_sequence++;
    if( 0U == next_capture_sequence )
    {
        next_capture_sequence = 1U;
    }
    slot->started_ms = a_started_ms;
    slot->ended_ms = 0U;
    slot->peak_rssi_dbm = INT8_MIN;
    slot->end_reason = RFSCOUT_SUBGHZ_CAPTURE_END_IDLE;
    slot->truncated = false;
    filling_slot = a_index;
    waiting_for_capture_slot = false;
}

static void capture_arm_failed_task( int a_index )
{
    portENTER_CRITICAL( &state_lock );
    if( ( a_index >= 0 ) && ( a_index < (int)CC1101_CAPTURE_POOL_SIZE ) &&
        ( CAPTURE_SLOT_FILLING == capture_pool[ a_index ].state ) )
    {
        capture_pool[ a_index ].state = CAPTURE_SLOT_FREE;
    }
    if( filling_slot == a_index )
    {
        filling_slot = -1;
    }
    ++capture_stats_state.capture_errors;
    portEXIT_CRITICAL( &state_lock );
}

static void capture_arm_failed_isr( int a_index )
{
    portENTER_CRITICAL_ISR( &state_lock );
    if( ( a_index >= 0 ) && ( a_index < (int)CC1101_CAPTURE_POOL_SIZE ) &&
        ( CAPTURE_SLOT_FILLING == capture_pool[ a_index ].state ) )
    {
        capture_pool[ a_index ].state = CAPTURE_SLOT_FREE;
    }
    if( filling_slot == a_index )
    {
        filling_slot = -1;
    }
    ++capture_stats_state.capture_errors;
    portEXIT_CRITICAL_ISR( &state_lock );
}

static bool arm_next_capture_task( void )
{
    if( ( NULL == rmt_channel ) || !rmt_enabled || !service_started ||
        ( CC1101_ACQUISITION_RECEIVE != acquisition_mode ) )
    {
        return false;
    }

    int slot_index = -1;
    portENTER_CRITICAL( &state_lock );
    if( filling_slot < 0 )
    {
        slot_index = find_free_capture_slot_locked();
        if( slot_index >= 0 )
        {
            initialize_capture_slot_locked( slot_index, capture_now_ms() );
        }
        else if( !waiting_for_capture_slot )
        {
            waiting_for_capture_slot = true;
            ++capture_stats_state.capture_overruns;
        }
    }
    portEXIT_CRITICAL( &state_lock );

    if( slot_index < 0 )
    {
        return false;
    }

    const esp_err_t result = rmt_receive( rmt_channel,
                                         capture_pool[ slot_index ].symbols,
                                         CC1101_CAPTURE_SYMBOL_CAPACITY *
                                             sizeof( rfscout_subghz_symbol_t ),
                                         &rmt_receive_config );
    if( ESP_OK != result )
    {
        capture_arm_failed_task( slot_index );
        ESP_LOGW( TAG, "RMT receive arm failed: %s", esp_err_to_name( result ) );
        return false;
    }
    return true;
}

static bool arm_next_capture_isr( void )
{
    if( ( NULL == rmt_channel ) || !rmt_enabled || !service_started ||
        ( CC1101_ACQUISITION_RECEIVE != acquisition_mode ) )
    {
        return false;
    }

    int slot_index = -1;
    portENTER_CRITICAL_ISR( &state_lock );
    if( filling_slot < 0 )
    {
        slot_index = find_free_capture_slot_locked();
        if( slot_index >= 0 )
        {
            initialize_capture_slot_locked( slot_index, capture_now_ms_from_isr() );
        }
        else if( !waiting_for_capture_slot )
        {
            waiting_for_capture_slot = true;
            ++capture_stats_state.capture_overruns;
        }
    }
    portEXIT_CRITICAL_ISR( &state_lock );

    if( slot_index < 0 )
    {
        return false;
    }

    const esp_err_t result = rmt_receive( rmt_channel,
                                         capture_pool[ slot_index ].symbols,
                                         CC1101_CAPTURE_SYMBOL_CAPACITY *
                                             sizeof( rfscout_subghz_symbol_t ),
                                         &rmt_receive_config );
    if( ESP_OK != result )
    {
        capture_arm_failed_isr( slot_index );
        return false;
    }
    return true;
}

static bool capture_done_callback( rmt_channel_handle_t a_channel,
                                   const rmt_rx_done_event_data_t *a_event,
                                   void *a_context )
{
    (void)a_channel;
    (void)a_context;

    if( NULL == a_event )
    {
        return false;
    }

    portENTER_CRITICAL_ISR( &state_lock );
    const int slot_index = filling_slot;
    if( ( slot_index >= 0 ) && ( slot_index < (int)CC1101_CAPTURE_POOL_SIZE ) )
    {
        capture_slot_t *slot = &capture_pool[ slot_index ];
        if( CAPTURE_SLOT_FILLING == slot->state )
        {
            slot->symbol_count = a_event->num_symbols < CC1101_CAPTURE_SYMBOL_CAPACITY
                                     ? a_event->num_symbols
                                     : CC1101_CAPTURE_SYMBOL_CAPACITY;
            slot->ended_ms = capture_now_ms_from_isr();
            slot->truncated = ( a_event->num_symbols >= CC1101_CAPTURE_SYMBOL_CAPACITY );
            slot->end_reason = slot->truncated ? RFSCOUT_SUBGHZ_CAPTURE_END_CAPACITY
                                               : RFSCOUT_SUBGHZ_CAPTURE_END_IDLE;
            slot->state = CAPTURE_SLOT_READY;
            ++capture_stats_state.captures_completed;
            if( slot->truncated )
            {
                ++capture_stats_state.captures_truncated;
            }
        }
    }
    filling_slot = -1;
    portEXIT_CRITICAL_ISR( &state_lock );

    (void)arm_next_capture_isr();
    return false;
}

static void clear_capture_pool_state( rfscout_subghz_capture_end_t a_reason )
{
    portENTER_CRITICAL( &state_lock );
    for( size_t index = 0U; index < CC1101_CAPTURE_POOL_SIZE; ++index )
    {
        capture_slot_t *slot = &capture_pool[ index ];
        if( CAPTURE_SLOT_FREE != slot->state )
        {
            slot->end_reason = a_reason;
        }
        slot->state = CAPTURE_SLOT_FREE;
        slot->symbol_count = 0U;
    }
    filling_slot = -1;
    waiting_for_capture_slot = false;
    portEXIT_CRITICAL( &state_lock );
}

static void destroy_capture_provider( void )
{
    if( NULL != rmt_channel )
    {
        if( rmt_enabled )
        {
            const esp_err_t disable_result = rmt_disable( rmt_channel );
            if( ESP_OK != disable_result )
            {
                ESP_LOGW( TAG, "RMT disable failed: %s", esp_err_to_name( disable_result ) );
            }
            rmt_enabled = false;
        }
        clear_capture_pool_state( RFSCOUT_SUBGHZ_CAPTURE_END_ABORT );
        const esp_err_t delete_result = rmt_del_channel( rmt_channel );
        if( ESP_OK != delete_result )
        {
            ESP_LOGW( TAG, "RMT channel delete failed: %s", esp_err_to_name( delete_result ) );
        }
        rmt_channel = NULL;
    }

    for( size_t index = 0U; index < CC1101_CAPTURE_POOL_SIZE; ++index )
    {
        heap_caps_free( capture_pool[ index ].symbols );
        memset( &capture_pool[ index ], 0, sizeof( capture_pool[ index ] ) );
    }
}

static esp_err_t ensure_capture_provider( void )
{
    if( NULL != rmt_channel )
    {
        return ESP_OK;
    }

    for( size_t index = 0U; index < CC1101_CAPTURE_POOL_SIZE; ++index )
    {
        capture_pool[ index ].symbols = heap_caps_aligned_calloc(
            CC1101_RMT_ALIGNMENT,
            CC1101_CAPTURE_SYMBOL_CAPACITY,
            sizeof( rfscout_subghz_symbol_t ),
            MALLOC_CAP_8BIT | MALLOC_CAP_DMA | MALLOC_CAP_SPIRAM );
        if( NULL == capture_pool[ index ].symbols )
        {
            destroy_capture_provider();
            return ESP_ERR_NO_MEM;
        }
        capture_pool[ index ].state = CAPTURE_SLOT_FREE;
    }

    rmt_rx_channel_config_t channel_config = {
        .gpio_num = RFSCOUT_BOARD_CC1101_GDO0_PIN,
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = CC1101_RMT_RESOLUTION_HZ,
        .mem_block_symbols = CC1101_CAPTURE_SYMBOL_CAPACITY,
        .intr_priority = 0,
        .flags.with_dma = true,
        .flags.allow_pd = false,
        .flags.invert_in = false,
    };

    esp_err_t result = rmt_new_rx_channel( &channel_config, &rmt_channel );
    if( ESP_OK != result )
    {
        destroy_capture_provider();
        return result;
    }

    const rmt_rx_event_callbacks_t callbacks = {
        .on_recv_done = capture_done_callback,
    };
    result = rmt_rx_register_event_callbacks( rmt_channel, &callbacks, NULL );
    if( ESP_OK != result )
    {
        destroy_capture_provider();
        return result;
    }

    result = rmt_enable( rmt_channel );
    if( ESP_OK != result )
    {
        destroy_capture_provider();
        return result;
    }
    rmt_enabled = true;
    return ESP_OK;
}

static esp_err_t start_receive_acquisition( void )
{
    esp_err_t result = ensure_capture_provider();
    if( ESP_OK != result )
    {
        ESP_LOGE( TAG, "capture provider unavailable: %s", esp_err_to_name( result ) );
        return result;
    }

    result = configure_ook_receive( receive_config.preset );
    if( ESP_OK == result )
    {
        result = tune( receive_config.frequency_khz );
    }
    if( ESP_OK != result )
    {
        destroy_capture_provider();
        return result;
    }

    last_receive_rssi_tick = 0U;
    if( !arm_next_capture_task() )
    {
        destroy_capture_provider();
        return ESP_FAIL;
    }
    return ESP_OK;
}

static void stop_receive_acquisition( void )
{
    destroy_capture_provider();
    (void)strobe( CC1101_SIDLE );
}

static esp_err_t apply_selected_mode( void )
{
    if( CC1101_ACQUISITION_RECEIVE == acquisition_mode )
    {
        return start_receive_acquisition();
    }

    start_scan_worker();
    return ESP_OK;
}

esp_err_t cc1101_init( void )
{
    if( initialized )
    {
        return detected ? ESP_OK : ESP_ERR_NOT_FOUND;
    }

    gpio_reset_pin( RFSCOUT_BOARD_CC1101_CS_PIN );

    esp_err_t result = attach_spi_device();
    if( ESP_OK != result )
    {
        return result;
    }

    initialized = true;
    disable_rf_path();
    (void)strobe( CC1101_SRES );

    uint8_t part = 0xFFU;
    uint8_t version = 0xFFU;
    (void)read_status( CC1101_PARTNUM, &part );
    (void)read_status( CC1101_VERSION, &version );

    detected = ( 0x00U == part ) && ( 0x00U != version ) && ( 0xFFU != version );
    ESP_LOGI( TAG, "probe part=0x%02x version=0x%02x detected=%d", part, version, detected );

    memset( display_spectrum, -120, sizeof( display_spectrum ) );

    if( detected && ( NULL == scan_task ) )
    {
        if( pdPASS != xTaskCreate( scan_worker,
                                   "cc1101_scan",
                                   CC1101_TASK_STACK_SIZE,
                                   NULL,
                                   CC1101_TASK_PRIORITY,
                                   &scan_task ) )
        {
            scan_task = NULL;
            (void)strobe( CC1101_SIDLE );
            (void)strobe( CC1101_SPWD );
            disable_rf_path();
            return ESP_ERR_NO_MEM;
        }
    }

    (void)strobe( CC1101_SIDLE );
    (void)strobe( CC1101_SPWD );
    disable_rf_path();
    return detected ? ESP_OK : ESP_ERR_NOT_FOUND;
}

esp_err_t cc1101_start( void )
{
    esp_err_t result = cc1101_init();
    if( ESP_OK != result )
    {
        return result;
    }
    if( service_started )
    {
        return ESP_OK;
    }

    result = wake_from_power_down();
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

    stop_receive_acquisition();
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

    const TickType_t now = xTaskGetTickCount();
    if( ( 0U == last_receive_rssi_tick ) ||
        ( now - last_receive_rssi_tick >= pdMS_TO_TICKS( CC1101_RECEIVE_RSSI_POLL_MS ) ) )
    {
        uint8_t raw_rssi = 0U;
        if( ESP_OK == read_status( CC1101_RSSI, &raw_rssi ) )
        {
            const int8_t rssi = decode_rssi( raw_rssi );
            portENTER_CRITICAL( &state_lock );
            if( ( filling_slot >= 0 ) && ( filling_slot < (int)CC1101_CAPTURE_POOL_SIZE ) )
            {
                capture_slot_t *slot = &capture_pool[ filling_slot ];
                if( ( CAPTURE_SLOT_FILLING == slot->state ) && ( rssi > slot->peak_rssi_dbm ) )
                {
                    slot->peak_rssi_dbm = rssi;
                }
            }
            portEXIT_CRITICAL( &state_lock );
        }
        last_receive_rssi_tick = now;
    }

    if( filling_slot < 0 )
    {
        (void)arm_next_capture_task();
    }
}

bool cc1101_is_detected( void )
{
    return detected;
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
        stop_receive_acquisition();
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
            (void)apply_selected_mode();
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

    int64_t frequency =
        (int64_t)scan_config.high_khz + ( (int64_t)a_delta * scan_config.step_khz );
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

size_t cc1101_spectrum( int8_t *a_output, size_t a_capacity )
{
    if( ( NULL == a_output ) || ( 0U == a_capacity ) )
    {
        return 0U;
    }

    const size_t count = a_capacity < CC1101_SPECTRUM_BINS ? a_capacity : CC1101_SPECTRUM_BINS;
    portENTER_CRITICAL( &state_lock );
    memcpy( a_output, display_spectrum, count );
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
        stop_receive_acquisition();
        const esp_err_t result = start_receive_acquisition();
        if( ESP_OK != result )
        {
            portENTER_CRITICAL( &state_lock );
            receive_config.frequency_khz = before;
            portEXIT_CRITICAL( &state_lock );
            (void)start_receive_acquisition();
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
        stop_receive_acquisition();
        const esp_err_t result = start_receive_acquisition();
        if( ESP_OK != result )
        {
            portENTER_CRITICAL( &state_lock );
            receive_config.preset = before;
            portEXIT_CRITICAL( &state_lock );
            (void)start_receive_acquisition();
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
    if( ( NULL == a_capture ) || ( NULL == a_lease ) )
    {
        return false;
    }

    int selected = -1;
    uint32_t selected_sequence = UINT32_MAX;

    portENTER_CRITICAL( &state_lock );
    for( size_t index = 0U; index < CC1101_CAPTURE_POOL_SIZE; ++index )
    {
        const capture_slot_t *slot = &capture_pool[ index ];
        if( ( CAPTURE_SLOT_READY == slot->state ) && ( slot->sequence < selected_sequence ) )
        {
            selected = (int)index;
            selected_sequence = slot->sequence;
        }
    }

    if( selected >= 0 )
    {
        capture_slot_t *slot = &capture_pool[ selected ];
        slot->state = CAPTURE_SLOT_PROCESSING;
        *a_capture = (rfscout_subghz_capture_t){
            .symbols = slot->symbols,
            .symbol_count = slot->symbol_count,
            .frequency_hz = slot->frequency_hz,
            .resolution_hz = slot->resolution_hz,
            .modulation = slot->modulation,
            .sequence = slot->sequence,
            .started_ms = slot->started_ms,
            .ended_ms = slot->ended_ms,
            .peak_rssi_dbm = slot->peak_rssi_dbm,
            .end_reason = slot->end_reason,
            .truncated = slot->truncated,
        };
        *a_lease = (uint8_t)selected;
    }
    portEXIT_CRITICAL( &state_lock );

    return selected >= 0;
}

void cc1101_capture_release( uint8_t a_lease )
{
    if( a_lease >= CC1101_CAPTURE_POOL_SIZE )
    {
        return;
    }

    portENTER_CRITICAL( &state_lock );
    capture_slot_t *slot = &capture_pool[ a_lease ];
    if( CAPTURE_SLOT_PROCESSING == slot->state )
    {
        slot->state = CAPTURE_SLOT_FREE;
        slot->symbol_count = 0U;
    }
    portEXIT_CRITICAL( &state_lock );

    (void)arm_next_capture_task();
}

void cc1101_capture_stats( cc1101_capture_stats_t *a_output )
{
    if( NULL == a_output )
    {
        return;
    }

    portENTER_CRITICAL( &state_lock );
    *a_output = capture_stats_state;
    portEXIT_CRITICAL( &state_lock );
}
