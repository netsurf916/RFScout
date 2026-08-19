#include "observation_store.h"
#include "platform.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

static const char *TAG = "rfscout";

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
    ESP_LOGI(TAG, "RFScout situational-awareness core started");

    while (1) {
        platform_poll();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
