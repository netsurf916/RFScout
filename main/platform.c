#include "platform.h"
#include "esp_log.h"

static const char *TAG = "platform";

esp_err_t platform_init(void)
{
    ESP_LOGW(TAG, "T-Embed hardware drivers not enabled yet; verify board revision/pin map first");
    return ESP_OK;
}

void platform_poll(void)
{
    /* Encoder/display/radio service hooks will live here. */
}

void platform_set_alert_led(bool active)
{
    (void)active;
}

void platform_audio_tone(bool enabled, uint16_t frequency_hz, void *context)
{
    (void)enabled;
    (void)frequency_hz;
    (void)context;
}
