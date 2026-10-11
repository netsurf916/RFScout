/** Wardriving capture session: platform raw callbacks enqueue copied records. */
#pragma once

#include "esp_err.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct
{
    bool wifi;
    bool ble;
    bool fixed_channel;
    uint8_t channel;
    uint32_t dwell_ms;
    uint32_t file_limit_bytes;
    uint8_t max_files;
    bool gps_metadata;
    uint8_t wifi_types; /* Bit 0 management, bit 1 control, bit 2 data. */
} wardrive_config_t;

typedef struct
{
    uint32_t received_wifi;
    uint32_t received_ble;
    uint32_t dropped;
    uint32_t written;
    uint32_t files;
    uint64_t bytes;
    bool recording;
    bool storage_error;
} wardrive_stats_t;

esp_err_t wardrive_capture_start( const wardrive_config_t *a_config );
void wardrive_capture_stop( void );
void wardrive_capture_stats( wardrive_stats_t *a_stats );
