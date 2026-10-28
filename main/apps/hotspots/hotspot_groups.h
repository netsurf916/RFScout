/**
    apps/hotspots/hotspot_groups.h : Hotspots network grouping model
    Description: Groups BSSIDs by SSID and auth mode, then splits each group into networks with
                 characterization_grouping. Pure model shared by the Live and History views.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#pragma once
#ifndef _RFSCOUT_HOTSPOT_GROUPS_H_
#define _RFSCOUT_HOTSPOT_GROUPS_H_

#include "characterization_grouping.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** One BSSID offered to a grouping pass; the SSID is borrowed for the call. */
typedef struct
{
    const char *ssid;
    int32_t auth_mode;
    characterization_group_member_t member; /**< evidence and location in; cluster out */
} hotspot_group_input_t;

/** One network: a run of member indices in the caller's order array. */
typedef struct
{
    size_t first;      /**< offset of the first member in the order array */
    size_t count;      /**< members in this network */
    uint16_t network;  /**< 1-based number among networks sharing this SSID and auth mode */
    uint16_t networks; /**< networks sharing this SSID and auth mode */
} hotspot_group_t;

/** Caller-provided scratch; every array needs as many entries as there are inputs. */
typedef struct
{
    size_t *sorted;
    size_t *rank;
    characterization_group_member_t *members;
    characterization_group_cluster_t *clusters;
} hotspot_group_scratch_t;

/**
 * @brief Partition inputs into networks.
 * @details @p a_preference lists every input index once, best first. Members of a network keep
 *          that order, and networks are ordered by their best member, so callers choose both
 *          orders with one ranking. Only network characterization evidence splits groups.
 * @param a_order   Receives every input index, grouped by network (a_count entries).
 * @param a_groups  Receives the networks (up to a_count entries).
 * @return Number of networks written; 0 when a_count is 0 or an argument is invalid.
 */
size_t hotspot_groups_build( hotspot_group_input_t *a_inputs,
                             size_t a_count,
                             const size_t *a_preference,
                             size_t *a_order,
                             hotspot_group_t *a_groups,
                             const hotspot_group_scratch_t *a_scratch );

#endif // _RFSCOUT_HOTSPOT_GROUPS_H_
