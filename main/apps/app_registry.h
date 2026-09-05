/**
    apps/app_registry.h : RFScout compiled-in application registry
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#pragma once

#ifndef _RFSCOUT_APP_REGISTRY_H_
#define _RFSCOUT_APP_REGISTRY_H_

#include "app_descriptor.h"

/** @brief Compiled-in descriptors indexed directly by rfscout_app_id_t. */
extern const rfscout_app_descriptor_t *const rfscout_app_registry[ RFSCOUT_APP_COUNT ];

#endif // _RFSCOUT_APP_REGISTRY_H_
