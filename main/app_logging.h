/**
    app_logging.h : RFScout per-application SD logging coordinator
    Description: Declares lifecycle and periodic telemetry coordination for typed SD records.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#pragma once

#ifndef _RFSCOUT_APP_LOGGING_H_
#define _RFSCOUT_APP_LOGGING_H_

/** @brief Initialize application logging state after the SD log writer is started. */
void app_logging_init( void );

/** @brief Poll active-app lifecycle transitions and periodic application telemetry. */
void app_logging_poll( void );

#endif // _RFSCOUT_APP_LOGGING_H_
