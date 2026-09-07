/**
    board.h : LILYGO T-Deck hardware definition
    Description: Central pin, geometry, and capability definitions for the RFScout T-Deck target.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#pragma once

#ifndef _RFSCOUT_BOARD_H_
#define _RFSCOUT_BOARD_H_

#define RFSCOUT_BOARD_NAME "LILYGO T-Deck"

/* Initial T-Deck plumbing is intentionally conservative. Capabilities are
   enabled only where RFScout already has, or is explicitly gaining, a matching
   service implementation. The board contains additional hardware (notably the
   SX1262, keyboard/trackball, and ES7210 microphone) that later commits in this
   draft PR will connect through the existing service abstractions. */
#define RFSCOUT_BOARD_HAS_CC1101 0
#define RFSCOUT_BOARD_HAS_NRF24 0
#define RFSCOUT_BOARD_HAS_SX1262 1
#define RFSCOUT_BOARD_HAS_GPS 0
#define RFSCOUT_BOARD_HAS_KEYBOARD 1
#define RFSCOUT_BOARD_HAS_TRACKBALL 1
#define RFSCOUT_BOARD_HAS_TOUCH 0
#define RFSCOUT_BOARD_HAS_ROTARY_ENCODER 0
#define RFSCOUT_BOARD_HAS_PDM_MIC 0
#define RFSCOUT_BOARD_HAS_SPEAKER 1
#define RFSCOUT_BOARD_HAS_SD 1
#define RFSCOUT_BOARD_CAN_SCREEN_OFF 1
#define RFSCOUT_BOARD_CAN_DEEP_SLEEP 1
#define RFSCOUT_BOARD_CAN_POWER_OFF 0
#define RFSCOUT_BOARD_HAS_INTERNAL_TEMPERATURE 1

#define RFSCOUT_BOARD_POWER_ENABLE_PIN 10
#define RFSCOUT_BOARD_USER_KEY_PIN 0

#define RFSCOUT_BOARD_SPI_MOSI_PIN 41
#define RFSCOUT_BOARD_SPI_MISO_PIN 38
#define RFSCOUT_BOARD_SPI_SCLK_PIN 40

#define RFSCOUT_BOARD_DISPLAY_CS_PIN 12
#define RFSCOUT_BOARD_DISPLAY_DC_PIN 11
#define RFSCOUT_BOARD_DISPLAY_RESET_PIN -1
#define RFSCOUT_BOARD_DISPLAY_BACKLIGHT_PIN 42
#define RFSCOUT_BOARD_DISPLAY_WIDTH 320
#define RFSCOUT_BOARD_DISPLAY_HEIGHT 240
#define RFSCOUT_BOARD_DISPLAY_NATIVE_WIDTH 320
#define RFSCOUT_BOARD_DISPLAY_NATIVE_HEIGHT 240
#define RFSCOUT_BOARD_DISPLAY_X_GAP 0
#define RFSCOUT_BOARD_DISPLAY_Y_GAP 0
#define RFSCOUT_BOARD_DISPLAY_SWAP_XY 0
#define RFSCOUT_BOARD_DISPLAY_MIRROR_X 0
#define RFSCOUT_BOARD_DISPLAY_MIRROR_Y 0
#define RFSCOUT_BOARD_DISPLAY_INVERT_COLOR 1

#define RFSCOUT_BOARD_SD_CS_PIN 39

#define RFSCOUT_BOARD_SX1262_CS_PIN 9
#define RFSCOUT_BOARD_SX1262_BUSY_PIN 13
#define RFSCOUT_BOARD_SX1262_RESET_PIN 17
#define RFSCOUT_BOARD_SX1262_DIO1_PIN 45

#define RFSCOUT_BOARD_I2C_SDA_PIN 18
#define RFSCOUT_BOARD_I2C_SCL_PIN 8
#define RFSCOUT_BOARD_KEYBOARD_I2C_ADDRESS 0x55
#define RFSCOUT_BOARD_KEYBOARD_IRQ_PIN 46

#define RFSCOUT_BOARD_TRACKBALL_UP_PIN 3
#define RFSCOUT_BOARD_TRACKBALL_DOWN_PIN 15
#define RFSCOUT_BOARD_TRACKBALL_LEFT_PIN 1
#define RFSCOUT_BOARD_TRACKBALL_RIGHT_PIN 2
#define RFSCOUT_BOARD_TRACKBALL_SELECT_PIN 0

/* Temporary compile-time aliases while the input service is split into
   board-selected rotary and keyboard/trackball providers. They must not be
   interpreted as a quadrature encoder on T-Deck. */
#define RFSCOUT_BOARD_ENCODER_A_PIN RFSCOUT_BOARD_TRACKBALL_LEFT_PIN
#define RFSCOUT_BOARD_ENCODER_B_PIN RFSCOUT_BOARD_TRACKBALL_RIGHT_PIN
#define RFSCOUT_BOARD_ENCODER_KEY_PIN RFSCOUT_BOARD_TRACKBALL_SELECT_PIN

#define RFSCOUT_BOARD_ES7210_MCLK_PIN 48
#define RFSCOUT_BOARD_ES7210_LRCLK_PIN 21
#define RFSCOUT_BOARD_ES7210_SCLK_PIN 47
#define RFSCOUT_BOARD_ES7210_DATA_PIN 14

#define RFSCOUT_BOARD_SPEAKER_LRCLK_PIN 5
#define RFSCOUT_BOARD_SPEAKER_BCLK_PIN 7
#define RFSCOUT_BOARD_SPEAKER_DATA_PIN 6

/* The current CC1101/nRF24 services are not present on T-Deck. Keep sentinel
   definitions until those drivers are fully capability-compiled per target. */
#define RFSCOUT_BOARD_CC1101_CS_PIN -1
#define RFSCOUT_BOARD_CC1101_GDO2_PIN -1
#define RFSCOUT_BOARD_CC1101_GDO0_PIN -1
#define RFSCOUT_BOARD_CC1101_SWITCH_1_PIN -1
#define RFSCOUT_BOARD_CC1101_SWITCH_0_PIN -1
#define RFSCOUT_BOARD_NRF24_CS_PIN -1
#define RFSCOUT_BOARD_NRF24_CE_PIN -1

/* Existing expansion-device services probe these addresses and fail cleanly
   when the corresponding hardware is absent. */
#define RFSCOUT_BOARD_PN532_I2C_ADDRESS 0x24
#define RFSCOUT_BOARD_MLX90640_I2C_ADDRESS 0x33
#define RFSCOUT_BOARD_BQ27220_I2C_ADDRESS 0x55
#define RFSCOUT_BOARD_BQ25896_I2C_ADDRESS 0x6B
#define RFSCOUT_BOARD_PN532_RESET_PIN -1
#define RFSCOUT_BOARD_PN532_IRQ_PIN -1

#define RFSCOUT_BOARD_IR_ENABLE_PIN -1
#define RFSCOUT_BOARD_IR_RX_PIN -1

/* Standard T-Deck does not expose the T-Embed PDM microphone interface. */
#define RFSCOUT_BOARD_MIC_DATA_PIN -1
#define RFSCOUT_BOARD_MIC_CLOCK_PIN -1

/* Standard T-Deck leaves these UART pins available through the Grove header;
   GPS is not claimed as an onboard capability for this target. */
#define RFSCOUT_BOARD_GPS_TX_PIN 43
#define RFSCOUT_BOARD_GPS_RX_PIN 44

#endif // _RFSCOUT_BOARD_H_
