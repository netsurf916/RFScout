#include "ble_monitor.h"
#include "morse.h"
#include "observation_store.h"
#include "oui_database.h"
#include "platform.h"
#include "radio_monitor.h"
#include "surveillance.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include <string.h>

static const char *TAG = "rfscout";

static void on_observation(rf_device_t *o)
{
    if (!o) return;

    /* IEEE OUIs are meaningful for Wi-Fi and public BLE addresses. Random BLE
       addresses deliberately remain unbranded rather than generating false vendors. */
    if (o->last_source != RF_SOURCE_BLE || o->address_type == 0) {
        const char *vendor = oui_database_lookup(o->address);
        if (vendor) strlcpy(o->vendor, vendor, sizeof(o->vendor));
    }
    o->surveillance = surveillance_classify(o);

    bool is_new = false;
    if (!observation_store_observe(o, &is_new)) return;
    if (!is_new) return;

    ESP_LOGI(TAG, "%s %s %02X:%02X:%02X:%02X:%02X:%02X rssi=%d ch=%u%s",
             o->last_source == RF_SOURCE_BLE ? "BLE" : "WiFi",
             o->vendor[0] ? o->vendor : (o->has_name ? o->name : "Unknown"),
             o->address[0], o->address[1], o->address[2], o->address[3], o->address[4], o->address[5],
             o->rssi, o->channel,
             o->surveillance != SURVEILLANCE_NONE ? " SURVEILLANCE" : "");

    if (o->surveillance != SURVEILLANCE_NONE) {
        char letter = surveillance_morse_letter(o->surveillance);
        ESP_LOGW(TAG, "ALERT %s [%c]", surveillance_name(o->surveillance), letter);
        platform_set_alert_led(true);
        /* morse_play_letter is currently synchronous; this runs only once per
           newly observed address. It will move to the platform audio queue when
           the MAX98357A I2S worker lands. */
        morse_play_letter(letter, 100, 900, platform_audio_tone, NULL);
        platform_set_alert_led(false);
    }
}

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    observation_store_init();
    ESP_ERROR_CHECK(platform_init());
    ESP_ERROR_CHECK(radio_monitor_init(on_observation));
    ESP_ERROR_CHECK(ble_monitor_init(on_observation));
    ESP_LOGI(TAG, "RFScout started: Wi-Fi + BLE passive situational awareness, region=%s",
             radio_monitor_region_name(radio_monitor_get_region()));

    while (1) {
        radio_monitor_step();
        platform_poll();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
