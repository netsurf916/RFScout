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

#include <stdbool.h>
#include <stdint.h>

#define RFSCOUT_BOARD_SPI_HOST SPI2_HOST
/** Wait indefinitely in board_spi_lock(). */
#define RFSCOUT_BOARD_SPI_WAIT_FOREVER UINT32_MAX

/**
 * @brief Initialize the shared board SPI bus exactly once.
 * @details Also creates the board SPI lock. The first call happens during single-threaded startup.
 */
esp_err_t board_spi_init( void );

/**
 * @brief Take the board-level shared SPI bus lock.
 * @details A recursive mutex held around every group of transactions any SPI2 client issues (LCD
 *          flush chunks, SD/FatFs transactions, radio register transfers, manual bus sequences).
 *          It is a defensive layer above the ESP-IDF SPI bus lock so that clients never contend
 *          inside the driver. Keep hold times short, and when a module also uses the storage I/O
 *          lock, take that lock first.
 * @param a_timeout_ms Maximum wait, or RFSCOUT_BOARD_SPI_WAIT_FOREVER.
 * @return true when the lock is held and must be released with board_spi_unlock().
 */
bool board_spi_lock( uint32_t a_timeout_ms );

/** @brief Release one level of the board-level shared SPI bus lock. */
void board_spi_unlock( void );

/**
 * @brief Exclusively reserve the shared SPI bus for a manual GPIO-level bus sequence.
 * @details Intended for device wake/reset sequences that cannot be expressed as a normal SPI
 *          transaction. The caller must pair success with board_spi_manual_release().
 */
esp_err_t board_spi_manual_acquire( void );

/** @brief Release a shared-SPI reservation obtained by board_spi_manual_acquire(). */
void board_spi_manual_release( void );

#endif // _RFSCOUT_BOARD_SPI_H_
