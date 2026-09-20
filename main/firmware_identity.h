/**
    firmware_identity.h : RFScout running firmware identity
    Description: Exposes update-relevant identity without leaking ESP-IDF image metadata to apps.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#pragma once

#ifndef _RFSCOUT_FIRMWARE_IDENTITY_H_
#define _RFSCOUT_FIRMWARE_IDENTITY_H_

typedef struct
{
    const char *project;
    const char *version;
    const char *hardware_target;
    const char *idf_target;
    const char *partition_layout;
} rfscout_firmware_identity_t;

/** @brief Return immutable identity for the currently running application image and hardware. */
const rfscout_firmware_identity_t *firmware_identity( void );

#endif // _RFSCOUT_FIRMWARE_IDENTITY_H_
