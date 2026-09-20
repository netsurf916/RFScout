/**
    board.h : RFScout runtime board profile
    Description: Exposes the immutable hardware profile selected during early startup.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#pragma once

#ifndef _RFSCOUT_BOARD_H_
#define _RFSCOUT_BOARD_H_

#include "esp_err.h"

#include <stdbool.h>
#include <stdint.h>

typedef enum
{
    RFSCOUT_BOARD_UNKNOWN = 0,
    RFSCOUT_BOARD_TEMBED_CC1101_PLUS,
} rfscout_board_id_t;

typedef enum
{
    RFSCOUT_BOARD_CAP_CC1101 = 1U << 0,
    RFSCOUT_BOARD_CAP_NRF24 = 1U << 1,
    RFSCOUT_BOARD_CAP_GPS = 1U << 3,
    RFSCOUT_BOARD_CAP_ROTARY_ENCODER = 1U << 7,
    RFSCOUT_BOARD_CAP_PDM_MIC = 1U << 8,
    RFSCOUT_BOARD_CAP_SPEAKER = 1U << 9,
    RFSCOUT_BOARD_CAP_SD = 1U << 10,
    RFSCOUT_BOARD_CAP_SCREEN_OFF = 1U << 11,
    RFSCOUT_BOARD_CAP_DEEP_SLEEP = 1U << 12,
    RFSCOUT_BOARD_CAP_POWER_OFF = 1U << 13,
    RFSCOUT_BOARD_CAP_INTERNAL_TEMPERATURE = 1U << 14,
} rfscout_board_capability_t;

typedef enum
{
    RFSCOUT_BACKLIGHT_PWM = 0,
} rfscout_backlight_type_t;

typedef enum
{
    RFSCOUT_BATTERY_NONE = 0,
    RFSCOUT_BATTERY_I2C_GAUGE_CHARGER,
} rfscout_battery_type_t;

typedef struct
{
    rfscout_board_id_t id;
    const char *name;
    uint32_t capabilities;

    int power_enable_pin;
    int user_key_pin;

    int spi_mosi_pin;
    int spi_miso_pin;
    int spi_sclk_pin;

    int display_cs_pin;
    int display_dc_pin;
    int display_reset_pin;
    int display_backlight_pin;
    uint16_t display_width;
    uint16_t display_height;
    uint16_t display_native_width;
    uint16_t display_native_height;
    int16_t display_x_gap;
    int16_t display_y_gap;
    bool display_swap_xy;
    bool display_mirror_x;
    bool display_mirror_y;
    bool display_invert_color;
    rfscout_backlight_type_t backlight_type;

    int sd_cs_pin;

    int cc1101_cs_pin;
    int cc1101_gdo2_pin;
    int cc1101_gdo0_pin;
    int cc1101_switch_1_pin;
    int cc1101_switch_0_pin;
    int nrf24_cs_pin;
    int nrf24_ce_pin;

    int i2c_sda_pin;
    int i2c_scl_pin;

    int encoder_a_pin;
    int encoder_b_pin;
    int encoder_key_pin;

    int speaker_lrclk_pin;
    int speaker_bclk_pin;
    int speaker_data_pin;

    rfscout_battery_type_t battery_type;
    uint16_t pn532_i2c_address;
    uint16_t mlx90640_i2c_address;
    uint16_t bq27220_i2c_address;
    uint16_t bq25896_i2c_address;
    int pn532_reset_pin;
    int pn532_irq_pin;

    int ir_enable_pin;
    int ir_rx_pin;
    int mic_data_pin;
    int mic_clock_pin;
    int gps_tx_pin;
    int gps_rx_pin;
} rfscout_board_profile_t;

esp_err_t board_profile_detect( void );
const rfscout_board_profile_t *board_profile( void );
bool board_profile_selected( void );
bool board_profile_has( rfscout_board_capability_t a_capability );

#define RFSCOUT_BOARD_NAME ( board_profile()->name )
#define RFSCOUT_BOARD_HAS_CC1101 ( board_profile_has( RFSCOUT_BOARD_CAP_CC1101 ) )
#define RFSCOUT_BOARD_HAS_NRF24 ( board_profile_has( RFSCOUT_BOARD_CAP_NRF24 ) )
#define RFSCOUT_BOARD_HAS_GPS ( board_profile_has( RFSCOUT_BOARD_CAP_GPS ) )
#define RFSCOUT_BOARD_HAS_ROTARY_ENCODER ( board_profile_has( RFSCOUT_BOARD_CAP_ROTARY_ENCODER ) )
#define RFSCOUT_BOARD_HAS_PDM_MIC ( board_profile_has( RFSCOUT_BOARD_CAP_PDM_MIC ) )
#define RFSCOUT_BOARD_HAS_SPEAKER ( board_profile_has( RFSCOUT_BOARD_CAP_SPEAKER ) )
#define RFSCOUT_BOARD_HAS_INTERNAL_TEMPERATURE                                                     \
    ( board_profile_has( RFSCOUT_BOARD_CAP_INTERNAL_TEMPERATURE ) )
#define RFSCOUT_BOARD_HAS_SD ( board_profile_has( RFSCOUT_BOARD_CAP_SD ) )
#define RFSCOUT_BOARD_CAN_SCREEN_OFF ( board_profile_has( RFSCOUT_BOARD_CAP_SCREEN_OFF ) )
#define RFSCOUT_BOARD_CAN_DEEP_SLEEP ( board_profile_has( RFSCOUT_BOARD_CAP_DEEP_SLEEP ) )
#define RFSCOUT_BOARD_CAN_POWER_OFF ( board_profile_has( RFSCOUT_BOARD_CAP_POWER_OFF ) )

#define RFSCOUT_BOARD_POWER_ENABLE_PIN ( board_profile()->power_enable_pin )
#define RFSCOUT_BOARD_USER_KEY_PIN ( board_profile()->user_key_pin )
#define RFSCOUT_BOARD_SPI_MOSI_PIN ( board_profile()->spi_mosi_pin )
#define RFSCOUT_BOARD_SPI_MISO_PIN ( board_profile()->spi_miso_pin )
#define RFSCOUT_BOARD_SPI_SCLK_PIN ( board_profile()->spi_sclk_pin )
#define RFSCOUT_BOARD_DISPLAY_CS_PIN ( board_profile()->display_cs_pin )
#define RFSCOUT_BOARD_DISPLAY_DC_PIN ( board_profile()->display_dc_pin )
#define RFSCOUT_BOARD_DISPLAY_RESET_PIN ( board_profile()->display_reset_pin )
#define RFSCOUT_BOARD_DISPLAY_BACKLIGHT_PIN ( board_profile()->display_backlight_pin )
#define RFSCOUT_BOARD_DISPLAY_WIDTH ( board_profile()->display_width )
#define RFSCOUT_BOARD_DISPLAY_HEIGHT ( board_profile()->display_height )
#define RFSCOUT_BOARD_DISPLAY_NATIVE_WIDTH ( board_profile()->display_native_width )
#define RFSCOUT_BOARD_DISPLAY_NATIVE_HEIGHT ( board_profile()->display_native_height )
#define RFSCOUT_BOARD_DISPLAY_X_GAP ( board_profile()->display_x_gap )
#define RFSCOUT_BOARD_DISPLAY_Y_GAP ( board_profile()->display_y_gap )
#define RFSCOUT_BOARD_DISPLAY_SWAP_XY ( board_profile()->display_swap_xy )
#define RFSCOUT_BOARD_DISPLAY_MIRROR_X ( board_profile()->display_mirror_x )
#define RFSCOUT_BOARD_DISPLAY_MIRROR_Y ( board_profile()->display_mirror_y )
#define RFSCOUT_BOARD_DISPLAY_INVERT_COLOR ( board_profile()->display_invert_color )
#define RFSCOUT_BOARD_BACKLIGHT_TYPE ( board_profile()->backlight_type )
#define RFSCOUT_BOARD_SD_CS_PIN ( board_profile()->sd_cs_pin )
#define RFSCOUT_BOARD_CC1101_CS_PIN ( board_profile()->cc1101_cs_pin )
#define RFSCOUT_BOARD_CC1101_GDO2_PIN ( board_profile()->cc1101_gdo2_pin )
#define RFSCOUT_BOARD_CC1101_GDO0_PIN ( board_profile()->cc1101_gdo0_pin )
#define RFSCOUT_BOARD_CC1101_SWITCH_1_PIN ( board_profile()->cc1101_switch_1_pin )
#define RFSCOUT_BOARD_CC1101_SWITCH_0_PIN ( board_profile()->cc1101_switch_0_pin )
#define RFSCOUT_BOARD_NRF24_CS_PIN ( board_profile()->nrf24_cs_pin )
#define RFSCOUT_BOARD_NRF24_CE_PIN ( board_profile()->nrf24_ce_pin )
#define RFSCOUT_BOARD_I2C_SDA_PIN ( board_profile()->i2c_sda_pin )
#define RFSCOUT_BOARD_I2C_SCL_PIN ( board_profile()->i2c_scl_pin )
#define RFSCOUT_BOARD_ENCODER_A_PIN ( board_profile()->encoder_a_pin )
#define RFSCOUT_BOARD_ENCODER_B_PIN ( board_profile()->encoder_b_pin )
#define RFSCOUT_BOARD_ENCODER_KEY_PIN ( board_profile()->encoder_key_pin )
#define RFSCOUT_BOARD_SPEAKER_LRCLK_PIN ( board_profile()->speaker_lrclk_pin )
#define RFSCOUT_BOARD_SPEAKER_BCLK_PIN ( board_profile()->speaker_bclk_pin )
#define RFSCOUT_BOARD_SPEAKER_DATA_PIN ( board_profile()->speaker_data_pin )
#define RFSCOUT_BOARD_BATTERY_TYPE ( board_profile()->battery_type )
#define RFSCOUT_BOARD_PN532_I2C_ADDRESS ( board_profile()->pn532_i2c_address )
#define RFSCOUT_BOARD_MLX90640_I2C_ADDRESS ( board_profile()->mlx90640_i2c_address )
#define RFSCOUT_BOARD_BQ27220_I2C_ADDRESS ( board_profile()->bq27220_i2c_address )
#define RFSCOUT_BOARD_BQ25896_I2C_ADDRESS ( board_profile()->bq25896_i2c_address )
#define RFSCOUT_BOARD_PN532_RESET_PIN ( board_profile()->pn532_reset_pin )
#define RFSCOUT_BOARD_PN532_IRQ_PIN ( board_profile()->pn532_irq_pin )
#define RFSCOUT_BOARD_IR_ENABLE_PIN ( board_profile()->ir_enable_pin )
#define RFSCOUT_BOARD_IR_RX_PIN ( board_profile()->ir_rx_pin )
#define RFSCOUT_BOARD_MIC_DATA_PIN ( board_profile()->mic_data_pin )
#define RFSCOUT_BOARD_MIC_CLOCK_PIN ( board_profile()->mic_clock_pin )
#define RFSCOUT_BOARD_GPS_TX_PIN ( board_profile()->gps_tx_pin )
#define RFSCOUT_BOARD_GPS_RX_PIN ( board_profile()->gps_rx_pin )

#endif // _RFSCOUT_BOARD_H_
