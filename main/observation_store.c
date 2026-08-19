#include "observation_store.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>

static rf_device_t devices[RFSCOUT_DEVICE_CAPACITY];
static size_t device_count;
static size_t next_evict;
static SemaphoreHandle_t lock;

static bool same_address(const uint8_t a[6], const uint8_t b[6])
{
    return memcmp(a, b, 6) == 0;
}

void observation_store_init(void)
{
    memset(devices, 0, sizeof(devices));
    device_count = 0;
    next_evict = 0;
    lock = xSemaphoreCreateMutex();
}

bool observation_store_observe(const rf_device_t *observation, bool *is_new)
{
    if (observation == NULL || lock == NULL) return false;
    xSemaphoreTake(lock, portMAX_DELAY);

    for (size_t i = 0; i < device_count; ++i) {
        if (same_address(devices[i].address, observation->address)) {
            uint32_t first_seen = devices[i].first_seen_ms;
            devices[i] = *observation;
            devices[i].first_seen_ms = first_seen;
            if (is_new != NULL) *is_new = false;
            xSemaphoreGive(lock);
            return true;
        }
    }

    size_t slot;
    if (device_count < RFSCOUT_DEVICE_CAPACITY) {
        slot = device_count++;
    } else {
        slot = next_evict;
        next_evict = (next_evict + 1U) % RFSCOUT_DEVICE_CAPACITY;
    }
    devices[slot] = *observation;
    if (is_new != NULL) *is_new = true;
    xSemaphoreGive(lock);
    return true;
}

size_t observation_store_count(void)
{
    size_t result;
    xSemaphoreTake(lock, portMAX_DELAY);
    result = device_count;
    xSemaphoreGive(lock);
    return result;
}

size_t observation_store_surveillance_count(void)
{
    size_t result = 0;
    xSemaphoreTake(lock, portMAX_DELAY);
    for (size_t i = 0; i < device_count; ++i)
        if (devices[i].surveillance != SURVEILLANCE_NONE) ++result;
    xSemaphoreGive(lock);
    return result;
}

size_t observation_store_snapshot(rf_device_t *out, size_t capacity)
{
    if (out == NULL || capacity == 0) return 0;
    xSemaphoreTake(lock, portMAX_DELAY);
    size_t count = device_count < capacity ? device_count : capacity;
    memcpy(out, devices, count * sizeof(*out));
    xSemaphoreGive(lock);
    return count;
}
