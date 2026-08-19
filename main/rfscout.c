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
    if (o == NULL) return;
    const char *vendor = oui_database_lookup(o->address);
    if (vendor != NULL) strlcpy(o->vendor, vendor, sizeof(o->vendor));
    o->surveillance = surveillance_classify(o);

    bool is_new = false;
    if (!observation_store_observe(o, &is_new)) return;
    if (is_new) {
        ESP_LOGI(TAG, "%s %02X:%02X:%02X:%02X:%02X:%02X rssi=%d ch=%u%s",
                 o->vendor[0] ? o->vendor : "Unknown",
                 o->address[0], o->address[1], o->address[2], o->address[3], o->address[4], o->address[5],
                 o->rssi, o->channel,
                 o->surveillance != SURVEILLANCE_NONE ? " SURVEILLANCE" : "");
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
    ESP_LOGI(TAG, "RFScout passive situational-awareness core started");

    while (1) {
        radio_monitor_step();
        platform_poll();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
