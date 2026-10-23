/**
    apps/hotspots/hotspot_groups.c : Hotspots network grouping model
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "hotspot_groups.h"

#include <stdlib.h>
#include <string.h>

/* qsort has no context argument; grouping runs only on the application task. */
static const hotspot_group_input_t *sort_inputs;
static const size_t *sort_rank;
static const size_t *sort_order;

static int compare_key( size_t a_left, size_t a_right )
{
    const hotspot_group_input_t *left = &sort_inputs[ a_left ];
    const hotspot_group_input_t *right = &sort_inputs[ a_right ];
    const int ssid = strcmp( left->ssid, right->ssid );
    if( 0 != ssid )
    {
        return ssid;
    }
    if( left->auth_mode != right->auth_mode )
    {
        return left->auth_mode < right->auth_mode ? -1 : 1;
    }
    return (int)left->partition - (int)right->partition;
}

static int compare_members( const void *a_left, const void *a_right )
{
    const size_t left = *(const size_t *)a_left;
    const size_t right = *(const size_t *)a_right;
    const int key = compare_key( left, right );
    if( 0 != key )
    {
        return key;
    }
    return sort_rank[ left ] < sort_rank[ right ] ? -1 : 1;
}

static int compare_groups( const void *a_left, const void *a_right )
{
    const size_t left = sort_rank[ sort_order[ ( (const hotspot_group_t *)a_left )->first ] ];
    const size_t right = sort_rank[ sort_order[ ( (const hotspot_group_t *)a_right )->first ] ];
    return left < right ? -1 : ( left > right ? 1 : 0 );
}

static bool same_ssid_auth( const hotspot_group_input_t *a_left,
                            const hotspot_group_input_t *a_right )
{
    return a_left->auth_mode == a_right->auth_mode && 0 == strcmp( a_left->ssid, a_right->ssid );
}

size_t hotspot_groups_build( hotspot_group_input_t *a_inputs,
                             size_t a_count,
                             const size_t *a_preference,
                             size_t *a_order,
                             hotspot_group_t *a_groups,
                             const hotspot_group_scratch_t *a_scratch )
{
    if( NULL == a_inputs || NULL == a_preference || NULL == a_order || NULL == a_groups ||
        NULL == a_scratch || NULL == a_scratch->sorted || NULL == a_scratch->rank ||
        NULL == a_scratch->members || NULL == a_scratch->clusters || 0U == a_count )
    {
        return 0U;
    }

    for( size_t index = 0U; index < a_count; ++index )
    {
        a_scratch->rank[ a_preference[ index ] ] = index;
        a_scratch->sorted[ index ] = index;
    }
    sort_inputs = a_inputs;
    sort_rank = a_scratch->rank;
    qsort( a_scratch->sorted, a_count, sizeof( a_scratch->sorted[ 0 ] ), compare_members );

    size_t written = 0U;
    size_t group_count = 0U;
    size_t run = 0U;
    while( run < a_count )
    {
        /* One SSID/auth run, made of consecutive partitions in rank order. */
        size_t run_end = run + 1U;
        while( run_end < a_count && same_ssid_auth( &a_inputs[ a_scratch->sorted[ run ] ],
                                                    &a_inputs[ a_scratch->sorted[ run_end ] ] ) )
        {
            ++run_end;
        }

        uint16_t networks = 0U;
        size_t part = run;
        while( part < run_end )
        {
            const uint8_t partition = a_inputs[ a_scratch->sorted[ part ] ].partition;
            size_t part_end = part;
            while( part_end < run_end &&
                   a_inputs[ a_scratch->sorted[ part_end ] ].partition == partition )
            {
                a_scratch->members[ part_end - part ] =
                    a_inputs[ a_scratch->sorted[ part_end ] ].member;
                ++part_end;
            }
            const size_t clusters = characterization_grouping_partition(
                a_scratch->members, part_end - part, a_scratch->clusters );
            for( size_t index = part; index < part_end; ++index )
            {
                a_inputs[ a_scratch->sorted[ index ] ].member.cluster =
                    (uint16_t)( networks + a_scratch->members[ index - part ].cluster );
            }
            networks = (uint16_t)( networks + clusters );
            part = part_end;
        }

        for( uint16_t network = 0U; network < networks; ++network )
        {
            hotspot_group_t *group = &a_groups[ group_count++ ];
            *group = ( hotspot_group_t ){
                .first = written,
                .network = (uint16_t)( network + 1U ),
                .networks = networks,
            };
            for( size_t index = run; index < run_end; ++index )
            {
                if( network == a_inputs[ a_scratch->sorted[ index ] ].member.cluster )
                {
                    a_order[ written++ ] = a_scratch->sorted[ index ];
                    ++group->count;
                }
            }
        }
        run = run_end;
    }

    sort_order = a_order;
    qsort( a_groups, group_count, sizeof( a_groups[ 0 ] ), compare_groups );
    sort_inputs = NULL;
    sort_rank = NULL;
    sort_order = NULL;
    return group_count;
}
