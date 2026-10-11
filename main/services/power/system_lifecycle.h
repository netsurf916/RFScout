/** Coordinated terminal system transitions; presentation remains power.c-owned. */
#pragma once
#ifndef _RFSCOUT_SYSTEM_LIFECYCLE_H_
#define _RFSCOUT_SYSTEM_LIFECYCLE_H_
#include <stdbool.h>
typedef enum {
    SYSTEM_LIFECYCLE_DEEP_SLEEP = 0,
    SYSTEM_LIFECYCLE_POWER_OFF
} system_lifecycle_target_t;
/** Return false when an unsafe or incomplete transition must not commit. */
bool system_lifecycle_prepare( system_lifecycle_target_t a_target );
/** Recover writers after a failed hardware transition. */
void system_lifecycle_abort( system_lifecycle_target_t a_target );
#endif
