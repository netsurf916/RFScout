#include "observation_store.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>

static rf_device_t devices[RFSCOUT_DEVICE_CAPACITY];
static size_t device_count;
static SemaphoreHandle_t lock;

static bool same_identity(const rf_device_t *a, const rf_device_t *b)
{
    /* Wi-Fi and public BLE MACs share an identity namespace for our purposes.
       Random BLE addresses remain scoped by address type to avoid false merges. */
    return a->address_type == b->address_type && memcmp(a->address, b->address, 6) == 0;
}

static size_t oldest_slot(void)
{
    size_t oldest = 0;
    for (size_t i = 1; i < device_count; ++i)
        if (devices[i].last_seen_ms < devices[oldest].last_seen_ms) oldest = i;
    return oldest;
}

void observation_store_init(void)
{
    memset(devices, 0, sizeof(devices));
    device_count = 0;
    lock = xSemaphoreCreateMutex();
}

bool observation_store_observe(const rf_device_t *observation, bool *is_new)
{
    if (observation == NULL || lock == NULL) return false;
    xSemaphoreTake(lock, portMAX_DELAY);

    for (size_t i = 0; i < device_count; ++i) {
        if (same_identity(&devices[i], observation)) {
            const uint32_t first_seen = devices[i].first_seen_ms;
            const uint32_t sources = devices[i].source_mask | observation->source_mask;
            const surveillance_class_t prior_class = devices[i].surveillance;
            char prior_vendor[RFSCOUT_VENDOR_NAME_MAX];
            char prior_name[RFSCOUT_DEVICE_NAME_MAX];
            memcpy(prior_vendor, devices[i].vendor, sizeof(prior_vendor));
            memcpy(prior_name, devices[i].name, sizeof(prior_name));
            const bool prior_has_name = devices[i].has_name;

            devices[i] = *observation;
            devices[i].first_seen_ms = first_seen;
            devices[i].source_mask = sources;
            if (devices[i].surveillance == SURVEILLANCE_NONE) devices[i].surveillance = prior_class;
            if (devices[i].vendor[0] == '\0') memcpy(devices[i].vendor, prior_vendor, sizeof(prior_vendor));
            if (!devices[i].has_name && prior_has_name) {
                memcpy(devices[i].name, prior_name, sizeof(prior_name));
                devices[i].has_name = true;
            }
            if (is_new != NULL) *is_new = false;
            xSemaphoreGive(lock);
            return true;
        }
    }

    const size_t slot = device_count < RFSCOUT_DEVICE_CAPACITY ? device_count++ : oldest_slot();
    devices[slot] = *observation;
    if (is_new != NULL) *is_new = true;
    xSemaphoreGive(lock);
    return true;
}

size_t observation_store_count(void)
{
    size_t result = 0;
    if (lock == NULL) return 0;
    xSemaphoreTake(lock, portMAX_DELAY);
    result = device_count;
    xSemaphoreGive(lock);
    return result;
}

size_t observation_store_surveillance_count(void)
{
    size_t result = 0;
    if (lock == NULL) return 0;
    xSemaphoreTake(lock, portMAX_DELAY);
    for (size_t i = 0; i < device_count; ++i)
        if (devices[i].surveillance != SURVEILLANCE_NONE) ++result;
    xSemaphoreGive(lock);
    return result;
}

size_t observation_store_snapshot(rf_device_t *out, size_t capacity)
{
    if (out == NULL || capacity == 0 || lock == NULL) return 0;
    xSemaphoreTake(lock, portMAX_DELAY);
    const size_t count = device_count < capacity ? device_count : capacity;
    memcpy(out, devices, count * sizeof(*out));
    xSemaphoreGive(lock);
    return count;
}
