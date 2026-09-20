/**
    apps/ota/ota_view.h : OTA Update application presentation
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#pragma once

#ifndef _RFSCOUT_OTA_VIEW_H_
#define _RFSCOUT_OTA_VIEW_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct
{
    const char *ssid;
    int8_t rssi;
} ota_view_network_t;

void ota_view_render_main( const char *a_version,
                           const char *a_connection,
                           const ota_view_network_t *a_networks,
                           size_t a_network_count,
                           unsigned a_selected,
                           bool a_scanning,
                           bool a_can_check,
                           const char *a_update_status,
                           const char *a_error );
void ota_view_render_network_action( const char *a_ssid,
                                     int8_t a_rssi,
                                     bool a_visible,
                                     bool a_saved,
                                     bool a_open,
                                     bool a_connected,
                                     unsigned a_selected,
                                     const char *a_status );
void ota_view_render_network_list( const char *a_title,
                                   const ota_view_network_t *a_networks,
                                   size_t a_network_count,
                                   unsigned a_selected,
                                   const char *a_empty_message );
void ota_view_render_password( const char *a_ssid,
                               size_t a_length,
                               char a_character,
                               const char *a_error );

#endif // _RFSCOUT_OTA_VIEW_H_
