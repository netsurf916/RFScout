/**
    session_state.c : RFScout deep-sleep session persistence
    Description: Keeps active application and input-lock state in RTC-retained memory.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "session_state.h"

#include "esp_attr.h"
#include "esp_bit_defs.h"
#include "esp_sleep.h"

#include <stddef.h>
#include <stdint.h>

#define SESSION_STATE_MAGIC 0x52534653U
#define SESSION_STATE_VERSION 3U

typedef struct
{
    uint32_t magic;
    uint16_t version;
    uint8_t active_app;
    uint8_t input_locked;
    uint32_t checksum;
} retained_session_state_t;

static RTC_DATA_ATTR retained_session_state_t retained_state;

static uint32_t session_checksum( const retained_session_state_t *a_state )
{
    return a_state->magic ^ ( (uint32_t)a_state->version << 16U ) ^
           ( (uint32_t)a_state->active_app << 8U ) ^ (uint32_t)a_state->input_locked ^ 0xA5C35A3CU;
}

void session_state_store( rfscout_app_id_t a_active_app, bool a_input_locked )
{
    retained_state.magic = SESSION_STATE_MAGIC;
    retained_state.version = SESSION_STATE_VERSION;
    retained_state.active_app =
        (uint8_t)( ( a_active_app < RFSCOUT_APP_COUNT ) ? a_active_app : RFSCOUT_APP_DEVICE );
    retained_state.input_locked = a_input_locked ? 1U : 0U;
    retained_state.checksum = session_checksum( &retained_state );
}

bool session_state_restore( rfscout_session_state_t *a_state )
{
    const uint32_t wakeup_causes = esp_sleep_get_wakeup_causes();
    if( NULL == a_state || 0U != ( wakeup_causes & BIT( ESP_SLEEP_WAKEUP_UNDEFINED ) ) )
    {
        return false;
    }
    if( SESSION_STATE_MAGIC != retained_state.magic ||
        SESSION_STATE_VERSION != retained_state.version ||
        retained_state.active_app >= RFSCOUT_APP_COUNT || retained_state.input_locked > 1U ||
        retained_state.checksum != session_checksum( &retained_state ) )
    {
        return false;
    }
    a_state->active_app = (rfscout_app_id_t)retained_state.active_app;
    a_state->input_locked = ( 0U != retained_state.input_locked );
    return true;
}
