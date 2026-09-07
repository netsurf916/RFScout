/**
    storage_internal.h : RFScout storage-service internal interface
    Description: Private arbitration shared only by storage service implementation units.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#pragma once

#ifndef _RFSCOUT_STORAGE_INTERNAL_H_
#define _RFSCOUT_STORAGE_INTERNAL_H_

#include <stdbool.h>
#include <stdint.h>

#define RFSCOUT_STORAGE_WAIT_FOREVER UINT32_MAX

/** @brief Acquire storage-service arbitration for one bounded filesystem transaction. */
bool storage_io_acquire( uint32_t a_timeout_ms );

/** @brief Release storage-service arbitration after storage_io_acquire() succeeds. */
void storage_io_release( void );

#endif // _RFSCOUT_STORAGE_INTERNAL_H_
