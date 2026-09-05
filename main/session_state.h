/**
    session_state.h : RFScout deep-sleep session persistence
    Description: Retains short-lived UI session identity in RTC memory across deep sleep only.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#pragma once

#ifndef _RFSCOUT_SESSION_STATE_H_
#define _RFSCOUT_SESSION_STATE_H_

#include "app_descriptor.h"

#include <stdbool.h>

typedef struct
{
    rfscout_app_id_t active_app;
    bool input_locked;
} rfscout_session_state_t;

/** @brief Store state immediately before entering deep sleep. */
void session_state_store( rfscout_app_id_t a_active_app, bool a_input_locked );

/**
 * @brief Restore state only when this boot is a valid deep-sleep wake.
 * @return true when retained state passed version and integrity validation.
 */
bool session_state_restore( rfscout_session_state_t *a_state );

#endif // _RFSCOUT_SESSION_STATE_H_
