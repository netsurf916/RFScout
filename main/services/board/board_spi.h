/**
    board_spi.h : RFScout shared board SPI bus interface
    Description: Owns initialization of the T-Embed shared SPI2 bus used by LCD, SD, and radios.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#pragma once

#ifndef _RFSCOUT_BOARD_SPI_H_
#define _RFSCOUT_BOARD_SPI_H_

#include "driver/spi_master.h"
#include "esp_err.h"

#define RFSCOUT_BOARD_SPI_HOST SPI2_HOST

/** @brief Initialize the shared board SPI bus exactly once. */
esp_err_t board_spi_init( void );

/**
 * @brief Exclusively reserve the shared SPI bus for a manual GPIO-level bus sequence.
 * @details Intended for device wake/reset sequences that cannot be expressed as a normal SPI
 *          transaction. The caller must pair success with board_spi_manual_release().
 */
esp_err_t board_spi_manual_acquire( void );

/** @brief Release a shared-SPI reservation obtained by board_spi_manual_acquire(). */
void board_spi_manual_release( void );

#endif // _RFSCOUT_BOARD_SPI_H_
