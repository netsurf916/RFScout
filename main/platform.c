#include "platform.h"
#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "platform";

/* LILYGO T-Embed CC1101 V1.0 / Plus-compatible baseline. */
enum {
    PIN_PWR_EN = 15,
    PIN_ENCODER_A = 4,
    PIN_ENCODER_B = 5,
    PIN_ENCODER_KEY = 0,
    PIN_USER_KEY = 6,
    PIN_DISPLAY_BL = 21,
    PIN_DISPLAY_CS = 41,
    PIN_CC1101_CS = 12,
    PIN_SD_CS = 13,
    PIN_CC1101_SW1 = 47,
    PIN_CC1101_SW0 = 48,
};

esp_err_t platform_init(void)
{
    gpio_config_t out = {
        .pin_bit_mask = (1ULL << PIN_PWR_EN) | (1ULL << PIN_DISPLAY_BL) |
                        (1ULL << PIN_DISPLAY_CS) | (1ULL << PIN_CC1101_CS) |
                        (1ULL << PIN_SD_CS) | (1ULL << PIN_CC1101_SW1) |
                        (1ULL << PIN_CC1101_SW0),
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_ERROR_CHECK(gpio_config(&out));
    gpio_set_level(PIN_PWR_EN, 1);
    gpio_set_level(PIN_DISPLAY_BL, 1);
    gpio_set_level(PIN_DISPLAY_CS, 1);
    gpio_set_level(PIN_CC1101_CS, 1);
    gpio_set_level(PIN_SD_CS, 1);

    gpio_config_t in = {
        .pin_bit_mask = (1ULL << PIN_ENCODER_A) | (1ULL << PIN_ENCODER_B) |
                        (1ULL << PIN_ENCODER_KEY) | (1ULL << PIN_USER_KEY),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&in));
    ESP_LOGI(TAG, "T-Embed CC1101 Plus baseline GPIO initialized");
    return ESP_OK;
}

void platform_poll(void) { }
void platform_set_alert_led(bool active) { (void)active; }

/* Speaker is MAX98357A on I2S BCLK=46, LRCLK=40, DIN=7.  Audio playback is
   intentionally queued for the next hardware pass; keeping this callback
   silent avoids blocking radio callbacks until the I2S worker exists. */
void platform_audio_tone(bool enabled, uint16_t frequency_hz, void *context)
{
    (void)enabled; (void)frequency_hz; (void)context;
}
