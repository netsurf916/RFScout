#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define RFSCOUT_DEVICE_CAPACITY 1000
#define RFSCOUT_VENDOR_NAME_MAX 32

typedef enum {
    RF_SOURCE_WIFI = 1,
    RF_SOURCE_BLE  = 2,
    RF_SOURCE_SUBGHZ = 3,
    RF_SOURCE_NRF24 = 4,
    RF_SOURCE_NFC = 5,
    RF_SOURCE_IR = 6,
} rf_source_t;

typedef enum {
    SURVEILLANCE_NONE = 0,
    SURVEILLANCE_FLOCK,
    SURVEILLANCE_BODY_CAMERA,
    SURVEILLANCE_DRONE,
    SURVEILLANCE_CAMERA,
    SURVEILLANCE_SMART_GLASSES,
    SURVEILLANCE_OTHER,
} surveillance_class_t;

typedef struct {
    uint8_t address[6];
    rf_source_t source;
    int8_t rssi;
    uint16_t channel;
    uint32_t first_seen_ms;
    uint32_t last_seen_ms;
    surveillance_class_t surveillance;
    char vendor[RFSCOUT_VENDOR_NAME_MAX];
} rf_device_t;

void observation_store_init(void);
bool observation_store_observe(const rf_device_t *observation, bool *is_new);
size_t observation_store_count(void);
size_t observation_store_surveillance_count(void);
size_t observation_store_snapshot(rf_device_t *out, size_t capacity);
