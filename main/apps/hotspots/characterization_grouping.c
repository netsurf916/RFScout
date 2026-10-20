/**
    characterization_grouping.c : RFScout Hotspots evidence-based network grouping
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "characterization_grouping.h"

#include <math.h>
#include <string.h>

#define GROUPING_DEG_TO_RAD 0.017453292519943295
#define GROUPING_UNASSIGNED UINT16_MAX

static bool same_subnet( const characterization_network_evidence_t *a_left,
                         const characterization_network_evidence_t *a_right )
{
    return a_left->subnet_valid && a_right->subnet_valid && a_left->gateway == a_right->gateway &&
           a_left->netmask == a_right->netmask;
}

static bool same_gateway_mac( const characterization_network_evidence_t *a_left,
                              const characterization_network_evidence_t *a_right )
{
    return a_left->gateway_mac_valid && a_right->gateway_mac_valid &&
           0 == memcmp( a_left->gateway_mac, a_right->gateway_mac, sizeof( a_left->gateway_mac ) );
}

static void add_member( characterization_group_cluster_t *a_cluster,
                        const characterization_group_member_t *a_member,
                        bool a_has_evidence )
{
    /*
     * Only evidence-bearing members shape placement (size and location centroid), so
     * the order in which unevidenced members are placed never changes the outcome.
     */
    ++a_cluster->members;
    if( !a_has_evidence )
        return;
    ++a_cluster->evidence_members;
    if( a_member->location_valid )
    {
        a_cluster->latitude_sum += a_member->latitude_deg;
        a_cluster->longitude_sum += a_member->longitude_deg;
        ++a_cluster->location_count;
    }
}

/*
 * Choose among candidate clusters: the nearest location centroid when the member has a
 * location and at least one candidate has located members, otherwise the candidate with
 * the most evidence-bearing members.
 * a_subnet_filter restricts candidates to clusters sharing that gateway/netmask.
 */
static uint16_t choose_cluster( const characterization_group_member_t *a_member,
                                const characterization_group_cluster_t *a_clusters,
                                size_t a_cluster_count,
                                const characterization_network_evidence_t *a_subnet_filter )
{
    uint16_t nearest = GROUPING_UNASSIGNED;
    double nearest_distance = 0.0;
    uint16_t largest = GROUPING_UNASSIGNED;
    const double longitude_scale =
        a_member->location_valid ? cos( a_member->latitude_deg * GROUPING_DEG_TO_RAD ) : 0.0;

    for( size_t index = 0U; index < a_cluster_count; ++index )
    {
        const characterization_group_cluster_t *cluster = &a_clusters[ index ];
        if( NULL != a_subnet_filter && !same_subnet( &cluster->key, a_subnet_filter ) )
            continue;

        if( GROUPING_UNASSIGNED == largest ||
            cluster->evidence_members > a_clusters[ largest ].evidence_members )
            largest = (uint16_t)index;

        if( !a_member->location_valid || 0U == cluster->location_count )
            continue;

        const double latitude = cluster->latitude_sum / cluster->location_count;
        const double longitude = cluster->longitude_sum / cluster->location_count;
        const double delta_y = latitude - a_member->latitude_deg;
        const double delta_x = ( longitude - a_member->longitude_deg ) * longitude_scale;
        const double distance = delta_x * delta_x + delta_y * delta_y;
        if( GROUPING_UNASSIGNED == nearest || distance < nearest_distance )
        {
            nearest = (uint16_t)index;
            nearest_distance = distance;
        }
    }

    return GROUPING_UNASSIGNED != nearest ? nearest : largest;
}

static uint16_t create_cluster( characterization_group_cluster_t *a_clusters,
                                size_t *a_cluster_count,
                                const characterization_network_evidence_t *a_key )
{
    characterization_group_cluster_t *cluster = &a_clusters[ *a_cluster_count ];
    memset( cluster, 0, sizeof( *cluster ) );
    if( NULL != a_key )
        cluster->key = *a_key;
    return (uint16_t)( ( *a_cluster_count )++ );
}

size_t characterization_grouping_partition( characterization_group_member_t *a_members,
                                            size_t a_count,
                                            characterization_group_cluster_t *a_scratch )
{
    if( NULL == a_members || NULL == a_scratch || 0U == a_count ||
        a_count >= (size_t)GROUPING_UNASSIGNED )
        return 0U;

    size_t cluster_count = 0U;
    for( size_t index = 0U; index < a_count; ++index )
        a_members[ index ].cluster = GROUPING_UNASSIGNED;

    /* 1. Full evidence (subnet + gateway MAC) defines networks exactly. */
    for( size_t index = 0U; index < a_count; ++index )
    {
        characterization_group_member_t *member = &a_members[ index ];
        if( !member->evidence.subnet_valid || !member->evidence.gateway_mac_valid )
            continue;

        uint16_t target = GROUPING_UNASSIGNED;
        for( size_t cluster = 0U; cluster < cluster_count; ++cluster )
            if( same_subnet( &a_scratch[ cluster ].key, &member->evidence ) &&
                same_gateway_mac( &a_scratch[ cluster ].key, &member->evidence ) )
            {
                target = (uint16_t)cluster;
                break;
            }
        if( GROUPING_UNASSIGNED == target )
            target = create_cluster( a_scratch, &cluster_count, &member->evidence );

        member->cluster = target;
        add_member( &a_scratch[ target ], member, true );
    }

    /*
     * 2. Subnet without gateway MAC (e.g. older history): cannot conflict on MAC, so join a
     *    network with the same subnet, or start one for that subnet.
     */
    for( size_t index = 0U; index < a_count; ++index )
    {
        characterization_group_member_t *member = &a_members[ index ];
        if( GROUPING_UNASSIGNED != member->cluster || !member->evidence.subnet_valid )
            continue;

        uint16_t target = choose_cluster( member, a_scratch, cluster_count, &member->evidence );
        if( GROUPING_UNASSIGNED == target )
        {
            characterization_network_evidence_t key = member->evidence;
            key.gateway_mac_valid = false;
            target = create_cluster( a_scratch, &cluster_count, &key );
        }

        member->cluster = target;
        add_member( &a_scratch[ target ], member, true );
    }

    /*
     * 3. Gateway-MAC-only evidence must remain meaningful even without DHCP subnet
     *    evidence. An identical MAC can join a known cluster, but a conflicting MAC
     *    must not be silently folded into another network. Missing subnet evidence
     *    is not itself a conflict.
     */
    for( size_t index = 0U; index < a_count; ++index )
    {
        characterization_group_member_t *member = &a_members[ index ];
        if( GROUPING_UNASSIGNED != member->cluster || !member->evidence.gateway_mac_valid )
            continue;

        uint16_t target = GROUPING_UNASSIGNED;
        for( size_t cluster = 0U; cluster < cluster_count; ++cluster )
        {
            if( same_gateway_mac( &a_scratch[ cluster ].key, &member->evidence ) )
            {
                target = (uint16_t)cluster;
                break;
            }
        }
        if( GROUPING_UNASSIGNED == target )
            target = create_cluster( a_scratch, &cluster_count, &member->evidence );

        member->cluster = target;
        add_member( &a_scratch[ target ], member, true );
    }

    /* 4. No evidence: together with an existing network; never a reason to split. */
    if( 0U == cluster_count )
        (void)create_cluster( a_scratch, &cluster_count, NULL );

    for( size_t index = 0U; index < a_count; ++index )
    {
        characterization_group_member_t *member = &a_members[ index ];
        if( GROUPING_UNASSIGNED != member->cluster )
            continue;

        const uint16_t target = choose_cluster( member, a_scratch, cluster_count, NULL );
        member->cluster = target;
        add_member( &a_scratch[ target ], member, false );
    }

    /* Rank networks by size (stable), so cluster 0 is the largest. k is small. */
    for( size_t cluster = 0U; cluster < cluster_count; ++cluster )
    {
        uint16_t rank = 0U;
        for( size_t other = 0U; other < cluster_count; ++other )
            if( a_scratch[ other ].members > a_scratch[ cluster ].members ||
                ( a_scratch[ other ].members == a_scratch[ cluster ].members && other < cluster ) )
                ++rank;
        a_scratch[ cluster ].rank = rank;
    }
    for( size_t index = 0U; index < a_count; ++index )
        a_members[ index ].cluster = a_scratch[ a_members[ index ].cluster ].rank;

    return cluster_count;
}
