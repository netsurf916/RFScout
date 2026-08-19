#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define RFSCOUT_DEVICE_CAPACITY 1000
#define RFSCOUT_VENDOR_NAME_MAX 32
#define RFSCOUT_DEVICE_NAME_MAX  32
#define RFSCOUT_MFG_DATA_MAX     16

typedef enum {
    RF_SOURCE_WIFI   = 1,
    RF_SOURCE_BLE    = 2,
    RF_SOURCE_SUBGHZ = 3,
    RF_SOURCE_NRF24  = 4,
    RF_SOURCE_NFC    = 5,
    RF_SOURCE_IR     = 6,
} rf_source_t;

#define RF_SOURCE_BIT(source) (1U << ((unsigned)(source) - 1U))

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
    uint8_t address_type;
    rf_source_t last_source;
    uint32_t source_mask;
    int8_t rssi;
    uint16_t channel;
    uint32_t first_seen_ms;
    uint32_t last_seen_ms;
    surveillance_class_t surveillance;
    char vendor[RFSCOUT_VENDOR_NAME_MAX];
    char name[RFSCOUT_DEVICE_NAME_MAX];
    bool has_name;
    uint16_t company_id;
    uint8_t manufacturer_data[RFSCOUT_MFG_DATA_MAX];
    uint8_t manufacturer_data_len;
    bool has_manufacturer_data;
} rf_device_t;

void observation_store_init(void);
bool observation_store_observe(const rf_device_t *observation, bool *is_new);
size_t observation_store_count(void);
size_t observation_store_surveillance_count(void);
size_t observation_store_snapshot(rf_device_t *out, size_t capacity);
