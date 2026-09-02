/**
    memory_manager.h : RFScout owner-aware dynamic memory manager
    Description: Declares PSRAM-backed allocation ownership, growth, reclamation, and diagnostics.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#pragma once

#ifndef _RFSCOUT_MEMORY_MANAGER_H_
#define _RFSCOUT_MEMORY_MANAGER_H_

#include "app_manager.h"

#include "esp_err.h"

#include <stddef.h>
#include <stdint.h>

/** @brief Allocation lifetime classes tracked by the RFScout memory manager. */
typedef enum
{
    RFSCOUT_MEMORY_OWNER_PLATFORM = 0,
    RFSCOUT_MEMORY_OWNER_APPLICATION,
    RFSCOUT_MEMORY_OWNER_OPERATION
} rfscout_memory_owner_class_t;

/** @brief Identifies the owner and lifetime of one managed allocation. */
typedef struct
{
    rfscout_memory_owner_class_t owner_class;
    rfscout_app_id_t app;
    uint16_t operation;
} rfscout_memory_owner_t;

/** @brief Usage and failure counters for one application owner. */
typedef struct
{
    size_t current_bytes;
    size_t peak_bytes;
    size_t allocation_count;
    uint32_t allocation_failures;
} rfscout_memory_stats_t;

/**
 * @brief Initialize managed PSRAM allocation support.
 * @return ESP_OK when usable PSRAM is available, otherwise an error.
 */
esp_err_t memory_manager_init( void );

/** @brief Construct an application-lifetime allocation owner. */
rfscout_memory_owner_t memory_manager_app_owner( rfscout_app_id_t a_app );

/** @brief Construct an operation-lifetime owner nested beneath an application. */
rfscout_memory_owner_t memory_manager_operation_owner( rfscout_app_id_t a_app,
                                                       uint16_t a_operation );

/**
 * @brief Allocate zero-initialized managed memory from PSRAM.
 * @param a_owner Owner whose accounting/lifetime receives the allocation.
 * @param a_count Number of elements to allocate.
 * @param a_size Size of each element in bytes.
 * @return Pointer to managed memory, or NULL on invalid input/overflow/allocation failure.
 */
void *memory_manager_calloc( rfscout_memory_owner_t a_owner, size_t a_count, size_t a_size );

/**
 * @brief Grow or shrink a managed allocation while preserving ownership.
 * @details Failure leaves the original allocation and its contents unchanged.
 * @param a_owner Expected owner of the allocation.
 * @param a_pointer Existing managed pointer, or NULL to allocate a new block.
 * @param a_count Requested element capacity.
 * @param a_size Size of each element in bytes.
 * @return New pointer on success, the original pointer remains valid on failure.
 */
void *memory_manager_resize( rfscout_memory_owner_t a_owner,
                             void *a_pointer,
                             size_t a_count,
                             size_t a_size );

/**
 * @brief Free one managed allocation.
 * @param a_owner Expected owner of the allocation.
 * @param a_pointer Managed pointer to release; NULL is accepted.
 * @return true when the pointer was NULL or released by its owner, false on ownership error.
 */
bool memory_manager_free( rfscout_memory_owner_t a_owner, void *a_pointer );

/**
 * @brief Release all allocations owned by one application, including nested operations.
 * @return Number of payload bytes reclaimed.
 */
size_t memory_manager_release_app( rfscout_app_id_t a_app );

/** @brief Release all allocations belonging to one nested application operation. */
size_t memory_manager_release_operation( rfscout_app_id_t a_app, uint16_t a_operation );

/** @brief Return current accounting for one application and its nested operations. */
rfscout_memory_stats_t memory_manager_app_stats( rfscout_app_id_t a_app );

/** @brief Return the number of managed allocations still owned by an application. */
size_t memory_manager_app_allocation_count( rfscout_app_id_t a_app );

#endif // _RFSCOUT_MEMORY_MANAGER_H_
