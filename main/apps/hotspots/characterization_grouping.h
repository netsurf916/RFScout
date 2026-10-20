/**
    characterization_grouping.h : RFScout Hotspots evidence-based network grouping
    Description: Splits one SSID/security group into networks only on conflicting evidence.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * Network-identity evidence captured by a completed characterization of one BSSID.
 *
 * Fields are only meaningful when their *_valid flag is set; absent evidence is never
 * treated as different from present evidence.
 */
typedef struct
{
    bool subnet_valid; /**< gateway/netmask came from a completed DHCP configuration */
    uint32_t gateway;  /**< IPv4 gateway, lwIP network byte order */
    uint32_t netmask;  /**< IPv4 netmask, lwIP network byte order */
    bool gateway_mac_valid;
    uint8_t gateway_mac[ 6 ]; /**< gateway hardware address from the station's ARP cache */
} characterization_network_evidence_t;

/** One BSSID participating in a grouping pass. */
typedef struct
{
    characterization_network_evidence_t evidence; /**< in */
    bool location_valid;                          /**< in */
    double latitude_deg;                          /**< in */
    double longitude_deg;                         /**< in */
    uint16_t cluster;                             /**< out: 0 is the largest network */
} characterization_group_member_t;

/** Caller-provided scratch for one grouping pass; contents are private to the helper. */
typedef struct
{
    characterization_network_evidence_t key;
    size_t members;
    size_t evidence_members;
    double latitude_sum;
    double longitude_sum;
    uint32_t location_count;
    uint16_t rank;
} characterization_group_cluster_t;

/**
 * @brief Partition the BSSIDs of one SSID/security group into networks.
 *
 * @details All members are assumed to be one network unless evidence conflicts:
 *          - different gateway/netmask between two members that both have it; or
 *          - different gateway MAC between two members that both have it.
 *          Members without evidence join the existing network whose evidence-bearing
 *          members are geographically nearest (flat-earth distance to the location
 *          centroid); without usable location they join the network with the most evidence-bearing members. A member
 *          with gateway/netmask but no gateway MAC is placed the same way among networks
 *          sharing its gateway/netmask. Cost is O(n * k) for n members and k networks.
 *
 * @param a_members  Members of exactly one SSID/security group; `cluster` is written.
 * @param a_count    Number of members.
 * @param a_scratch  Scratch array with at least a_count entries.
 * @return Number of networks (1..a_count), or 0 when a_count is 0 or arguments are invalid.
 *         Networks are numbered by descending member count.
 */
size_t characterization_grouping_partition( characterization_group_member_t *a_members,
                                            size_t a_count,
                                            characterization_group_cluster_t *a_scratch );
