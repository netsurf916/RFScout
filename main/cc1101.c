/**
    cc1101.c : RFScout CC1101 receive-only spectrum scanner
    Description: Sweeps configured sub-GHz frequencies and reports RSSI bins.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "cc1101.h"

#include "board.h"

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_log.h"
#include "esp_timer.h"

#include <string.h>

#define CC1101_SPI_HOST SPI2_HOST
#define CC1101_READ_BURST 0xC0U
#define CC1101_SRES 0x30U
#define CC1101_SIDLE 0x36U
#define CC1101_SRX 0x34U
#define CC1101_SPWD 0x39U
#define CC1101_FREQ2 0x0DU
#define CC1101_FREQ1 0x0EU
#define CC1101_FREQ0 0x0FU
#define CC1101_MCSM0 0x18U
#define CC1101_RSSI 0x34U
#define CC1101_PARTNUM 0x30U
#define CC1101_VERSION 0x31U
#define CC1101_XOSC_HZ 26000000ULL
#define CC1101_SETTLE_US 180LL

static const char *TAG = "cc1101";
static spi_device_handle_t device;
static bool initialized;
static bool detected;
static bool running;
static int64_t tune_us;
static size_t sweep_index;
static size_t sweep_count;
static int8_t spectrum[ CC1101_SPECTRUM_BINS ];
static cc1101_scan_config_t config =
{
    CC1101_PROFILE_433,
    433050U,
    434790U,
    25U
};

/** @brief Transfer one CC1101 command/register byte pair. */
static esp_err_t transfer( uint8_t a_header,
                           uint8_t a_data,
                           uint8_t *a_result )
{
    spi_transaction_t transaction =
    {
        .length = 16,
        .rxlength = 16
    };
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

static esp_err_t write_register( uint8_t a_address, uint8_t a_value )
{
    return transfer( a_address, a_value, NULL );
}

static esp_err_t read_status( uint8_t a_address, uint8_t *a_value )
{
    return transfer( a_address | CC1101_READ_BURST, 0U, a_value );
}

/**
 * @brief Select the board RF path appropriate to the current frequency.
 * @details LILYGO's T-Embed CC1101 examples define SW1/SW0 as:
 *          1/0 = 315 MHz, 1/1 = 434 MHz, 0/1 = 868/915 MHz.
 */
static void select_rf_path( uint32_t a_frequency_khz )
{
    int sw1 = 0;
    int sw0 = 0;

    if( a_frequency_khz <= 348000U )
    {
        sw1 = 1;
        sw0 = 0;
    }
    else if( a_frequency_khz <= 464000U )
    {
        sw1 = 1;
        sw0 = 1;
    }
    else if( a_frequency_khz >= 779000U )
    {
        sw1 = 0;
        sw0 = 1;
    }

    gpio_set_level( RFSCOUT_BOARD_CC1101_SWITCH_1_PIN, sw1 );
    gpio_set_level( RFSCOUT_BOARD_CC1101_SWITCH_0_PIN, sw0 );
}

/** @brief Put the external RF switch into its inactive 0/0 state. */
static void disable_rf_path( void )
{
    gpio_set_level( RFSCOUT_BOARD_CC1101_SWITCH_1_PIN, 0 );
    gpio_set_level( RFSCOUT_BOARD_CC1101_SWITCH_0_PIN, 0 );
}

/** @brief Convert the raw CC1101 RSSI status byte to approximately dBm. */
static int8_t decode_rssi( uint8_t a_raw )
{
    const int16_t signed_raw =
        ( a_raw >= 128U ) ? ( (int16_t)a_raw - 256 ) : (int16_t)a_raw;
    return (int8_t)( signed_raw / 2 - 74 );
}

/** @brief Return the number of configured sweep samples including both ends. */
static size_t configured_sweep_count( void )
{
    if( ( 0U == config.step_khz ) ||
        ( config.high_khz <= config.low_khz ) )
    {
        return 1U;
    }

    return (size_t)( ( config.high_khz - config.low_khz ) /
                     config.step_khz ) + 1U;
}

/** @brief Return the configured frequency for one sweep sample. */
static uint32_t frequency_for_sample( size_t a_index )
{
    uint64_t frequency =
        (uint64_t)config.low_khz +
        ( (uint64_t)a_index * config.step_khz );

    if( frequency > config.high_khz )
    {
        frequency = config.high_khz;
    }
    return (uint32_t)frequency;
}

/** @brief Map an arbitrary number of sweep samples onto the fixed LCD bin count. */
static size_t display_bin_for_sample( size_t a_index )
{
    if( sweep_count <= 1U )
    {
        return 0U;
    }

    return ( a_index * ( CC1101_SPECTRUM_BINS - 1U ) ) /
           ( sweep_count - 1U );
}

/** @brief Tune the CC1101 and select the matching board RF path. */
static esp_err_t tune( uint32_t a_frequency_khz )
{
    select_rf_path( a_frequency_khz );

    const uint64_t frequency_word =
        ( (uint64_t)a_frequency_khz * 1000ULL << 16 ) /
        CC1101_XOSC_HZ;

    esp_err_t result = strobe( CC1101_SIDLE );
    if( ESP_OK == result )
    {
        result = write_register( CC1101_FREQ2,
                                 (uint8_t)( ( frequency_word >> 16 ) & 0xFFU ) );
    }
    if( ESP_OK == result )
    {
        result = write_register( CC1101_FREQ1,
                                 (uint8_t)( ( frequency_word >> 8 ) & 0xFFU ) );
    }
    if( ESP_OK == result )
    {
        result = write_register( CC1101_FREQ0,
                                 (uint8_t)( frequency_word & 0xFFU ) );
    }
    if( ESP_OK == result )
    {
        result = strobe( CC1101_SRX );
    }
    return result;
}

/** @brief Restart the current sweep after a configuration change. */
static void restart_sweep( void )
{
    sweep_index = 0U;
    sweep_count = configured_sweep_count();
    memset( spectrum, -120, sizeof( spectrum ) );

    if( running )
    {
        const esp_err_t result = tune( frequency_for_sample( 0U ) );
        if( ESP_OK != result )
        {
            ESP_LOGW( TAG,
                      "unable to retune after configuration change: %s",
                      esp_err_to_name( result ) );
            running = false;
        }
        tune_us = esp_timer_get_time();
    }
}

esp_err_t cc1101_init( void )
{
    if( initialized )
    {
        return detected ? ESP_OK : ESP_ERR_NOT_FOUND;
    }

    /* The LCD initializes SPI2 first. CC1101 is another device on that shared
       bus, so attach only the CC1101 device here. */
    gpio_reset_pin( RFSCOUT_BOARD_CC1101_CS_PIN );

    spi_device_interface_config_t device_config =
    {
        .clock_speed_hz = 4000000,
        .mode = 0,
        .spics_io_num = RFSCOUT_BOARD_CC1101_CS_PIN,
        .queue_size = 1
    };

    esp_err_t result = spi_bus_add_device( CC1101_SPI_HOST,
                                           &device_config,
                                           &device );
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

    detected = ( 0x00U == part ) &&
               ( 0x00U != version ) &&
               ( 0xFFU != version );

    ESP_LOGI( TAG,
              "probe part=0x%02x version=0x%02x detected=%d",
              part,
              version,
              detected );

    restart_sweep();
    return detected ? ESP_OK : ESP_ERR_NOT_FOUND;
}

esp_err_t cc1101_start( void )
{
    esp_err_t result = cc1101_init();
    if( ESP_OK != result )
    {
        return result;
    }

    result = write_register( CC1101_MCSM0, 0x18U );
    if( ESP_OK != result )
    {
        return result;
    }

    running = true;
    restart_sweep();
    return running ? ESP_OK : ESP_FAIL;
}

void cc1101_stop( void )
{
    if( !initialized )
    {
        return;
    }

    running = false;
    (void)strobe( CC1101_SIDLE );
    (void)strobe( CC1101_SPWD );
    disable_rf_path();
}

void cc1101_poll( void )
{
    if( !running || !detected )
    {
        return;
    }

    const int64_t now_us = esp_timer_get_time();
    if( ( now_us - tune_us ) < CC1101_SETTLE_US )
    {
        return;
    }

    uint8_t raw_rssi;
    if( ESP_OK == read_status( CC1101_RSSI, &raw_rssi ) )
    {
        const size_t display_bin = display_bin_for_sample( sweep_index );
        const int8_t rssi = decode_rssi( raw_rssi );

        /* Wide sweeps may contain more configured frequencies than LCD bins.
           Preserve the strongest sample mapped into each visual bin. */
        if( rssi > spectrum[ display_bin ] )
        {
            spectrum[ display_bin ] = rssi;
        }
    }

    ++sweep_index;
    if( sweep_index >= sweep_count )
    {
        sweep_index = 0U;
        memset( spectrum, -120, sizeof( spectrum ) );
    }

    const esp_err_t result = tune( frequency_for_sample( sweep_index ) );
    if( ESP_OK != result )
    {
        ESP_LOGW( TAG, "CC1101 tune failed: %s", esp_err_to_name( result ) );
        running = false;
        disable_rf_path();
        return;
    }

    tune_us = now_us;
}

bool cc1101_is_detected( void )
{
    return detected;
}

const cc1101_scan_config_t *cc1101_config( void )
{
    return &config;
}

const char *cc1101_profile_name( cc1101_profile_t a_profile )
{
    static const char *names[] =
    {
        "433 ISM",
        "315 MHz",
        "868 MHz",
        "915 MHz",
        "MANUAL"
    };

    return ( a_profile < CC1101_PROFILE_COUNT ) ?
           names[ a_profile ] : "?";
}

void cc1101_set_profile( cc1101_profile_t a_profile )
{
    if( a_profile >= CC1101_PROFILE_COUNT )
    {
        return;
    }

    config.profile = a_profile;
    switch( a_profile )
    {
        case CC1101_PROFILE_433:
            config.low_khz = 433050U;
            config.high_khz = 434790U;
            config.step_khz = 25U;
            break;

        case CC1101_PROFILE_315:
            config.low_khz = 300000U;
            config.high_khz = 348000U;
            config.step_khz = 250U;
            break;

        case CC1101_PROFILE_868:
            config.low_khz = 863000U;
            config.high_khz = 870000U;
            config.step_khz = 100U;
            break;

        case CC1101_PROFILE_915:
            config.low_khz = 902000U;
            config.high_khz = 928000U;
            config.step_khz = 200U;
            break;

        case CC1101_PROFILE_MANUAL:
        default:
            break;
    }

    restart_sweep();
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
    if( ( a_frequency_khz > 348000U ) &&
        ( a_frequency_khz < 387000U ) )
    {
        return ( a_frequency_khz < 367500U ) ? 348000U : 387000U;
    }
    if( ( a_frequency_khz > 464000U ) &&
        ( a_frequency_khz < 779000U ) )
    {
        return ( a_frequency_khz < 621500U ) ? 464000U : 779000U;
    }
    return a_frequency_khz;
}

void cc1101_adjust_low( int a_delta )
{
    int64_t frequency =
        (int64_t)config.low_khz +
        ( (int64_t)a_delta * config.step_khz );

    if( frequency < 300000LL )
    {
        frequency = 300000LL;
    }

    config.low_khz = clamp_band( (uint32_t)frequency );
    if( config.low_khz >= config.high_khz )
    {
        config.low_khz = config.high_khz - config.step_khz;
    }
    config.profile = CC1101_PROFILE_MANUAL;
    restart_sweep();
}

void cc1101_adjust_high( int a_delta )
{
    int64_t frequency =
        (int64_t)config.high_khz +
        ( (int64_t)a_delta * config.step_khz );

    if( frequency > 928000LL )
    {
        frequency = 928000LL;
    }

    config.high_khz = clamp_band( (uint32_t)frequency );
    if( config.high_khz <= config.low_khz )
    {
        config.high_khz = config.low_khz + config.step_khz;
    }
    config.profile = CC1101_PROFILE_MANUAL;
    restart_sweep();
}

void cc1101_adjust_step( int a_delta )
{
    static const uint32_t steps[] =
    {
        10U,
        25U,
        50U,
        100U,
        200U,
        250U,
        500U,
        1000U
    };

    size_t index = 0U;
    while( ( index + 1U < ( sizeof( steps ) / sizeof( steps[ 0 ] ) ) ) &&
           ( steps[ index ] < config.step_khz ) )
    {
        ++index;
    }

    if( ( a_delta > 0 ) &&
        ( index + 1U < ( sizeof( steps ) / sizeof( steps[ 0 ] ) ) ) )
    {
        ++index;
    }
    else if( ( a_delta < 0 ) && ( index > 0U ) )
    {
        --index;
    }

    config.step_khz = steps[ index ];
    config.profile = CC1101_PROFILE_MANUAL;
    restart_sweep();
}

size_t cc1101_spectrum( int8_t *a_output, size_t a_capacity )
{
    if( ( NULL == a_output ) || ( 0U == a_capacity ) )
    {
        return 0U;
    }

    const size_t count =
        ( a_capacity < CC1101_SPECTRUM_BINS ) ?
        a_capacity : CC1101_SPECTRUM_BINS;

    memcpy( a_output, spectrum, count );
    return count;
}
