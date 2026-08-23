/**
    board.h : LILYGO T-Embed CC1101 Plus hardware definition
    Description: Central pin and geometry definitions for the RFScout target.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#pragma once

#ifndef _RFSCOUT_BOARD_H_
#define _RFSCOUT_BOARD_H_

#define RFSCOUT_BOARD_POWER_ENABLE_PIN       15
#define RFSCOUT_BOARD_USER_KEY_PIN            6

#define RFSCOUT_BOARD_ENCODER_A_PIN            4
#define RFSCOUT_BOARD_ENCODER_B_PIN            5
#define RFSCOUT_BOARD_ENCODER_KEY_PIN          0

#define RFSCOUT_BOARD_WS2812_PIN              14
#define RFSCOUT_BOARD_WS2812_COUNT             8

#define RFSCOUT_BOARD_SPI_MOSI_PIN              9
#define RFSCOUT_BOARD_SPI_MISO_PIN             10
#define RFSCOUT_BOARD_SPI_SCLK_PIN             11

#define RFSCOUT_BOARD_DISPLAY_CS_PIN           41
#define RFSCOUT_BOARD_DISPLAY_DC_PIN           16
#define RFSCOUT_BOARD_DISPLAY_RESET_PIN        -1
#define RFSCOUT_BOARD_DISPLAY_BACKLIGHT_PIN    21
#define RFSCOUT_BOARD_DISPLAY_WIDTH           320
#define RFSCOUT_BOARD_DISPLAY_HEIGHT          170
#define RFSCOUT_BOARD_DISPLAY_NATIVE_WIDTH    170
#define RFSCOUT_BOARD_DISPLAY_NATIVE_HEIGHT   320
#define RFSCOUT_BOARD_DISPLAY_X_GAP             0
#define RFSCOUT_BOARD_DISPLAY_Y_GAP            35

#define RFSCOUT_BOARD_SD_CS_PIN                13

#define RFSCOUT_BOARD_CC1101_CS_PIN            12
#define RFSCOUT_BOARD_CC1101_GDO2_PIN          38
#define RFSCOUT_BOARD_CC1101_GDO0_PIN           3
#define RFSCOUT_BOARD_CC1101_SWITCH_1_PIN      47
#define RFSCOUT_BOARD_CC1101_SWITCH_0_PIN      48

#define RFSCOUT_BOARD_NRF24_CS_PIN             44
#define RFSCOUT_BOARD_NRF24_CE_PIN             43

#define RFSCOUT_BOARD_I2C_SDA_PIN               8
#define RFSCOUT_BOARD_I2C_SCL_PIN              18
#define RFSCOUT_BOARD_PN532_RESET_PIN          45
#define RFSCOUT_BOARD_PN532_IRQ_PIN            17

#define RFSCOUT_BOARD_IR_ENABLE_PIN             2
#define RFSCOUT_BOARD_IR_RX_PIN                 1

#define RFSCOUT_BOARD_MIC_DATA_PIN             42
#define RFSCOUT_BOARD_MIC_CLOCK_PIN            39

#define RFSCOUT_BOARD_SPEAKER_BCLK_PIN         46
#define RFSCOUT_BOARD_SPEAKER_LRCLK_PIN        40
#define RFSCOUT_BOARD_SPEAKER_DATA_PIN          7

#endif // _RFSCOUT_BOARD_H_
