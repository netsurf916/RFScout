/**
    subghz_capture_provider.c : RFScout bounded Sub-GHz RMT capture provider
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "subghz_capture_provider.h"

#include "driver/rmt_rx.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <limits.h>
#include <string.h>

#define SUBGHZ_RMT_ALIGNMENT 64U

typedef enum
{
    CAPTURE_SLOT_FREE = 0,
    CAPTURE_SLOT_FILLING,
    CAPTURE_SLOT_READY,
    CAPTURE_SLOT_PROCESSING,
} capture_slot_state_t;

typedef struct
{
    rfscout_subghz_symbol_t *symbols;
    capture_slot_state_t state;
    size_t symbol_count;
    uint32_t frequency_hz;
    uint32_t resolution_hz;
    rfscout_subghz_modulation_t modulation;
    uint32_t sequence;
    uint32_t started_ms;
    uint32_t ended_ms;
    int8_t peak_rssi_dbm;
    rfscout_subghz_capture_end_t end_reason;
    bool truncated;
} capture_slot_t;

static const char *TAG = "subghz_capture";
static portMUX_TYPE state_lock = portMUX_INITIALIZER_UNLOCKED;
static rmt_channel_handle_t rmt_channel;
static volatile bool rmt_enabled;
static volatile bool provider_active;
static volatile bool provider_stopping;
static capture_slot_t capture_pool[ RFSCOUT_SUBGHZ_CAPTURE_POOL_SIZE ];
static int filling_slot = -1;
static bool waiting_for_capture_slot;
static uint32_t next_capture_sequence = 1U;
static rfscout_subghz_capture_stats_t capture_stats_state;
static rfscout_subghz_capture_provider_config_t provider_config;
static rmt_receive_config_t receive_config;

static uint32_t capture_now_ms_from_isr( void )
{
    return (uint32_t)( xTaskGetTickCountFromISR() * portTICK_PERIOD_MS );
}

static uint32_t capture_now_ms( void )
{
    return (uint32_t)( xTaskGetTickCount() * portTICK_PERIOD_MS );
}

static int find_free_capture_slot_locked( void )
{
    for( size_t index = 0U; index < RFSCOUT_SUBGHZ_CAPTURE_POOL_SIZE; ++index )
    {
        if( CAPTURE_SLOT_FREE == capture_pool[ index ].state )
        {
            return (int)index;
        }
    }
    return -1;
}

static void initialize_capture_slot_locked( int a_index, uint32_t a_started_ms )
{
    capture_slot_t *slot = &capture_pool[ a_index ];
    slot->state = CAPTURE_SLOT_FILLING;
    slot->symbol_count = 0U;
    slot->frequency_hz = provider_config.frequency_hz;
    slot->resolution_hz = provider_config.resolution_hz;
    slot->modulation = provider_config.modulation;
    slot->sequence = next_capture_sequence++;
    if( 0U == next_capture_sequence )
    {
        next_capture_sequence = 1U;
    }
    slot->started_ms = a_started_ms;
    slot->ended_ms = 0U;
    slot->peak_rssi_dbm = INT8_MIN;
    slot->end_reason = RFSCOUT_SUBGHZ_CAPTURE_END_IDLE;
    slot->truncated = false;
    filling_slot = a_index;
    waiting_for_capture_slot = false;
}

static void capture_arm_failed_task( int a_index )
{
    portENTER_CRITICAL( &state_lock );
    if( ( a_index >= 0 ) && ( a_index < (int)RFSCOUT_SUBGHZ_CAPTURE_POOL_SIZE ) &&
        ( CAPTURE_SLOT_FILLING == capture_pool[ a_index ].state ) )
    {
        capture_pool[ a_index ].state = CAPTURE_SLOT_FREE;
    }
    if( filling_slot == a_index )
    {
        filling_slot = -1;
    }
    ++capture_stats_state.capture_errors;
    portEXIT_CRITICAL( &state_lock );
}

static bool arm_next_capture_task( void )
{
    if( ( NULL == rmt_channel ) || !rmt_enabled || !provider_active || provider_stopping )
    {
        return false;
    }

    int slot_index = -1;
    portENTER_CRITICAL( &state_lock );
    if( ( filling_slot < 0 ) && provider_active && !provider_stopping )
    {
        slot_index = find_free_capture_slot_locked();
        if( slot_index >= 0 )
        {
            initialize_capture_slot_locked( slot_index, capture_now_ms() );
        }
        else if( !waiting_for_capture_slot )
        {
            waiting_for_capture_slot = true;
            ++capture_stats_state.capture_overruns;
        }
    }
    portEXIT_CRITICAL( &state_lock );

    if( slot_index < 0 )
    {
        return false;
    }

    const esp_err_t result =
        rmt_receive( rmt_channel,
                     capture_pool[ slot_index ].symbols,
                     RFSCOUT_SUBGHZ_CAPTURE_SYMBOL_CAPACITY * sizeof( rfscout_subghz_symbol_t ),
                     &receive_config );
    if( ESP_OK != result )
    {
        capture_arm_failed_task( slot_index );
        ESP_LOGW( TAG, "RMT receive arm failed: %s", esp_err_to_name( result ) );
        return false;
    }
    return true;
}

static bool capture_done_callback( rmt_channel_handle_t a_channel,
                                   const rmt_rx_done_event_data_t *a_event,
                                   void *a_context )
{
    (void)a_channel;
    (void)a_context;

    if( NULL == a_event )
    {
        return false;
    }

    portENTER_CRITICAL_ISR( &state_lock );
    const int slot_index = filling_slot;
    if( ( slot_index >= 0 ) && ( slot_index < (int)RFSCOUT_SUBGHZ_CAPTURE_POOL_SIZE ) )
    {
        capture_slot_t *slot = &capture_pool[ slot_index ];
        if( CAPTURE_SLOT_FILLING == slot->state )
        {
            slot->symbol_count = a_event->num_symbols < RFSCOUT_SUBGHZ_CAPTURE_SYMBOL_CAPACITY
                                     ? a_event->num_symbols
                                     : RFSCOUT_SUBGHZ_CAPTURE_SYMBOL_CAPACITY;
            slot->ended_ms = capture_now_ms_from_isr();
            slot->truncated = a_event->num_symbols >= RFSCOUT_SUBGHZ_CAPTURE_SYMBOL_CAPACITY;
            slot->end_reason = slot->truncated ? RFSCOUT_SUBGHZ_CAPTURE_END_CAPACITY
                                               : RFSCOUT_SUBGHZ_CAPTURE_END_IDLE;
            slot->state = CAPTURE_SLOT_READY;
            ++capture_stats_state.captures_completed;
            if( slot->truncated )
            {
                ++capture_stats_state.captures_truncated;
            }
            if( a_event->received_symbols != (rmt_symbol_word_t *)slot->symbols )
            {
                ++capture_stats_state.capture_errors;
            }
        }
    }
    filling_slot = -1;
    portEXIT_CRITICAL_ISR( &state_lock );

    return false;
}

static void free_capture_pool( void )
{
    for( size_t index = 0U; index < RFSCOUT_SUBGHZ_CAPTURE_POOL_SIZE; ++index )
    {
        heap_caps_free( capture_pool[ index ].symbols );
        memset( &capture_pool[ index ], 0, sizeof( capture_pool[ index ] ) );
    }

    portENTER_CRITICAL( &state_lock );
    filling_slot = -1;
    waiting_for_capture_slot = false;
    portEXIT_CRITICAL( &state_lock );
}

static esp_err_t allocate_capture_pool( void )
{
    for( size_t index = 0U; index < RFSCOUT_SUBGHZ_CAPTURE_POOL_SIZE; ++index )
    {
        capture_pool[ index ].symbols =
            heap_caps_aligned_calloc( SUBGHZ_RMT_ALIGNMENT,
                                      RFSCOUT_SUBGHZ_CAPTURE_SYMBOL_CAPACITY,
                                      sizeof( rfscout_subghz_symbol_t ),
                                      MALLOC_CAP_8BIT | MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL );
        if( NULL == capture_pool[ index ].symbols )
        {
            free_capture_pool();
            return ESP_ERR_NO_MEM;
        }
        capture_pool[ index ].state = CAPTURE_SLOT_FREE;
    }
    return ESP_OK;
}

esp_err_t subghz_capture_provider_start( const rfscout_subghz_capture_provider_config_t *a_config )
{
    if( ( NULL == a_config ) || ( a_config->gpio_num < 0 ) || ( 0U == a_config->resolution_hz ) ||
        ( 0U == a_config->signal_range_max_ns ) )
    {
        return ESP_ERR_INVALID_ARG;
    }
    if( provider_active || ( NULL != rmt_channel ) )
    {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t result = allocate_capture_pool();
    if( ESP_OK != result )
    {
        return result;
    }

    provider_config = *a_config;
    receive_config = ( rmt_receive_config_t ){
        .signal_range_min_ns = a_config->signal_range_min_ns,
        .signal_range_max_ns = a_config->signal_range_max_ns,
        .flags.en_partial_rx = false,
    };

    rmt_rx_channel_config_t channel_config = {
        .gpio_num = a_config->gpio_num,
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = a_config->resolution_hz,
        .mem_block_symbols = RFSCOUT_SUBGHZ_CAPTURE_SYMBOL_CAPACITY,
        .intr_priority = 0,
        .flags.with_dma = true,
        .flags.allow_pd = false,
        .flags.invert_in = false,
    };

    result = rmt_new_rx_channel( &channel_config, &rmt_channel );
    if( ESP_OK != result )
    {
        free_capture_pool();
        return result;
    }

    const rmt_rx_event_callbacks_t callbacks = {
        .on_recv_done = capture_done_callback,
    };
    result = rmt_rx_register_event_callbacks( rmt_channel, &callbacks, NULL );
    if( ESP_OK != result )
    {
        (void)rmt_del_channel( rmt_channel );
        rmt_channel = NULL;
        free_capture_pool();
        return result;
    }

    result = rmt_enable( rmt_channel );
    if( ESP_OK != result )
    {
        (void)rmt_del_channel( rmt_channel );
        rmt_channel = NULL;
        free_capture_pool();
        return result;
    }

    portENTER_CRITICAL( &state_lock );
    provider_stopping = false;
    provider_active = true;
    rmt_enabled = true;
    portEXIT_CRITICAL( &state_lock );

    if( !arm_next_capture_task() )
    {
        (void)subghz_capture_provider_stop();
        return ESP_FAIL;
    }
    return ESP_OK;
}

bool subghz_capture_provider_stop( void )
{
    portENTER_CRITICAL( &state_lock );
    provider_stopping = true;
    portEXIT_CRITICAL( &state_lock );

    if( NULL != rmt_channel )
    {
        if( rmt_enabled )
        {
            const esp_err_t disable_result = rmt_disable( rmt_channel );
            if( ESP_OK != disable_result )
            {
                ESP_LOGE( TAG, "RMT disable failed: %s", esp_err_to_name( disable_result ) );
                portENTER_CRITICAL( &state_lock );
                provider_stopping = false;
                portEXIT_CRITICAL( &state_lock );
                return false;
            }
            rmt_enabled = false;
        }

        const esp_err_t delete_result = rmt_del_channel( rmt_channel );
        if( ESP_OK != delete_result )
        {
            ESP_LOGE( TAG, "RMT channel delete failed: %s", esp_err_to_name( delete_result ) );
            portENTER_CRITICAL( &state_lock );
            provider_active = false;
            portEXIT_CRITICAL( &state_lock );
            return false;
        }
        rmt_channel = NULL;
    }

    portENTER_CRITICAL( &state_lock );
    provider_active = false;
    filling_slot = -1;
    waiting_for_capture_slot = false;
    for( size_t index = 0U; index < RFSCOUT_SUBGHZ_CAPTURE_POOL_SIZE; ++index )
    {
        capture_pool[ index ].state = CAPTURE_SLOT_FREE;
        capture_pool[ index ].symbol_count = 0U;
    }
    portEXIT_CRITICAL( &state_lock );

    free_capture_pool();

    portENTER_CRITICAL( &state_lock );
    provider_stopping = false;
    portEXIT_CRITICAL( &state_lock );
    return true;
}

bool subghz_capture_provider_active( void )
{
    portENTER_CRITICAL( &state_lock );
    const bool active = provider_active;
    portEXIT_CRITICAL( &state_lock );
    return active;
}

void subghz_capture_provider_poll( void )
{
    (void)arm_next_capture_task();
}

void subghz_capture_provider_note_rssi( int8_t a_rssi_dbm )
{
    portENTER_CRITICAL( &state_lock );
    if( ( filling_slot >= 0 ) && ( filling_slot < (int)RFSCOUT_SUBGHZ_CAPTURE_POOL_SIZE ) )
    {
        capture_slot_t *slot = &capture_pool[ filling_slot ];
        if( ( CAPTURE_SLOT_FILLING == slot->state ) && ( a_rssi_dbm > slot->peak_rssi_dbm ) )
        {
            slot->peak_rssi_dbm = a_rssi_dbm;
        }
    }
    portEXIT_CRITICAL( &state_lock );
}

bool subghz_capture_provider_acquire( rfscout_subghz_capture_t *a_capture, uint8_t *a_lease )
{
    if( ( NULL == a_capture ) || ( NULL == a_lease ) )
    {
        return false;
    }

    int selected = -1;
    uint32_t selected_sequence = UINT32_MAX;

    portENTER_CRITICAL( &state_lock );
    for( size_t index = 0U; index < RFSCOUT_SUBGHZ_CAPTURE_POOL_SIZE; ++index )
    {
        const capture_slot_t *slot = &capture_pool[ index ];
        if( ( CAPTURE_SLOT_READY == slot->state ) && ( slot->sequence < selected_sequence ) )
        {
            selected = (int)index;
            selected_sequence = slot->sequence;
        }
    }

    if( selected >= 0 )
    {
        capture_slot_t *slot = &capture_pool[ selected ];
        slot->state = CAPTURE_SLOT_PROCESSING;
        *a_capture = ( rfscout_subghz_capture_t ){
            .symbols = slot->symbols,
            .symbol_count = slot->symbol_count,
            .frequency_hz = slot->frequency_hz,
            .resolution_hz = slot->resolution_hz,
            .modulation = slot->modulation,
            .sequence = slot->sequence,
            .started_ms = slot->started_ms,
            .ended_ms = slot->ended_ms,
            .peak_rssi_dbm = slot->peak_rssi_dbm,
            .end_reason = slot->end_reason,
            .truncated = slot->truncated,
        };
        *a_lease = (uint8_t)selected;
    }
    portEXIT_CRITICAL( &state_lock );

    return selected >= 0;
}

void subghz_capture_provider_release( uint8_t a_lease )
{
    if( a_lease >= RFSCOUT_SUBGHZ_CAPTURE_POOL_SIZE )
    {
        return;
    }

    portENTER_CRITICAL( &state_lock );
    capture_slot_t *slot = &capture_pool[ a_lease ];
    if( CAPTURE_SLOT_PROCESSING == slot->state )
    {
        slot->state = CAPTURE_SLOT_FREE;
        slot->symbol_count = 0U;
    }
    portEXIT_CRITICAL( &state_lock );

    (void)arm_next_capture_task();
}

void subghz_capture_provider_stats( rfscout_subghz_capture_stats_t *a_output )
{
    if( NULL == a_output )
    {
        return;
    }

    portENTER_CRITICAL( &state_lock );
    *a_output = capture_stats_state;
    portEXIT_CRITICAL( &state_lock );
}
