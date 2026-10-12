/**
    surveillance_history.h : Append-only receiver observation journal
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/
#pragma once
#ifndef _RFSCOUT_SURVEILLANCE_HISTORY_H_
#define _RFSCOUT_SURVEILLANCE_HISTORY_H_

#include "observation_store.h"

/** @brief Reset pending journal state on app entry. */
void surveillance_history_open( void );

/** @brief Release session-owned journal buffers after final flush, before session reclamation. */
void surveillance_history_close( void );

/** @brief Capture one GPS-qualified packet observation without SD I/O. */
void surveillance_history_record( const observation_location_event_t *a_event );

/** @brief Append pending packet observations from the app poll, never from radio callbacks. */
void surveillance_history_flush( void );

/** @brief On app exit, attempt to drain both journal buffers without unbounded retries. */
void surveillance_history_flush_final( void );

#endif // _RFSCOUT_SURVEILLANCE_HISTORY_H_
