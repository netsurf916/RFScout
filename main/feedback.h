/**
    feedback.h : RFScout semantic feedback policy
    Description: Resolves accepted semantic events through app and theme policy into disposable UI audio.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#pragma once

#ifndef _RFSCOUT_FEEDBACK_H_
#define _RFSCOUT_FEEDBACK_H_

#include "esp_err.h"

/** @brief Subscribe the feedback policy to RFScout semantic events. */
esp_err_t feedback_init( void );

#endif // _RFSCOUT_FEEDBACK_H_
