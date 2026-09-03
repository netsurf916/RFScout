# Memory management

RFScout uses explicit lifetime ownership for large dynamic allocations that are eligible for external PSRAM. The implementation lives in `memory_manager.c/.h` and is intentionally separate from ordinary ESP-IDF allocations that must remain in internal or DMA-capable memory.

## Ownership model

Managed allocations have one of three lifetimes:

- **Platform/service** allocations live independently of the selected application and are not reclaimed by application exit.
- **Application** allocations belong to one `rfscout_app_id_t` and are eligible for bulk reclamation after that application has safely quiesced.
- **Operation** allocations are nested beneath an application and may be reclaimed at an earlier mode/operation boundary. Application-wide reclamation also includes these nested allocations.

The manager does not infer ownership from call sites. Callers pass an explicit owner to allocation, resize, and free operations. A mismatched owner is rejected and logged rather than freeing memory belonging to another lifetime.

## Allocation policy

`memory_manager_calloc()` and `memory_manager_resize()` request `MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT`; managed application storage does not silently fall back to internal SRAM. `memory_manager_init()` reports an unsupported-target error when no usable PSRAM heap exists.

The manager is intended for retained observations, histories, captures, and other meaningfully sized growable state. Small transient objects, task stacks, DMA buffers, driver-required internal allocations, and other hardware-constrained memory should continue to use the appropriate ESP-IDF allocation path instead of paying managed-allocation metadata overhead.

Resize operations check multiplication overflow before calculating the requested byte capacity. A failed resize preserves the original allocation and its contents. Successful growth zero-initializes the newly added tail so callers do not accidentally consume uninitialized retained state.

## Accounting and reclamation

For each application, the manager tracks:

- current managed payload bytes;
- peak managed payload bytes;
- current managed allocation count;
- allocation-failure count.

`memory_manager_release_operation()` reclaims one nested operation. `memory_manager_release_app()` reclaims every application and operation allocation associated with the selected app and returns the number of payload bytes released. `memory_manager_app_allocation_count()` provides the invariant needed by lifecycle code to verify that successful app exit leaves no managed allocations behind.

Metadata for managed allocations is intentionally small and kept in internal memory so the ownership list remains available while PSRAM payloads are being reclaimed.

## Lifecycle boundary

The memory manager provides reclamation mechanics; it does **not** decide when reclamation is safe. Application workers, callbacks, timers, DMA completion paths, and peripherals must first be quiesced according to [APP_LIFECYCLE.md](APP_LIFECYCLE.md). The v0.4.0 lifecycle integration work is responsible for placing `memory_manager_release_app()` after the final reachability/safety gate and for treating a non-zero post-release allocation count as an accounting invariant failure.

No existing application storage is migrated merely by introducing this manager. Each application will move eligible retained state to managed PSRAM in subsequent v0.4.0 work, with its own memory-pressure semantics and quiesce validation.

## Supported-target requirement

Managed storage reinforces RFScout's existing platform requirement for usable external PSRAM. A future board port must provide a working SPIRAM heap; internal SRAM is not a reduced-capacity fallback for managed application history. See [BOARD_PORTING.md](BOARD_PORTING.md) for the board-level requirement.
