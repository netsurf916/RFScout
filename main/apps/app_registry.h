/**
    apps/app_registry.h : RFScout compiled-in application registry
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#pragma once

#ifndef _RFSCOUT_APP_REGISTRY_H_
#define _RFSCOUT_APP_REGISTRY_H_

#include "app_descriptor.h"

#include <stddef.h>

extern const rfscout_app_descriptor_t *const rfscout_app_registry[];
extern const size_t rfscout_app_registry_count;

#endif // _RFSCOUT_APP_REGISTRY_H_
