/**
    nrf24_packet_view.h : RFScout nRF24 decoded-packet browser
    Description: Declares snapshot-only list and detail views for validated ESB packets.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#pragma once

#ifndef _RFSCOUT_NRF24_PACKET_VIEW_H_
#define _RFSCOUT_NRF24_PACKET_VIEW_H_

#include <stddef.h>

/** @brief Render newest-first validated packet snapshots and return the visible packet count. */
size_t nrf24_packet_view_list( unsigned a_selected );

/** @brief Render full metadata for one newest-first packet snapshot. */
void nrf24_packet_view_detail( unsigned a_selected );

#endif // _RFSCOUT_NRF24_PACKET_VIEW_H_
