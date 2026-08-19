#include "morse.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <ctype.h>

static const char *patterns[26] = {
    ".-", "-...", "-.-.", "-..", ".", "..-.", "--.", "....", "..", ".---",
    "-.-", ".-..", "--", "-.", "---", ".--.", "--.-", ".-.", "...", "-",
    "..-", "...-", ".--", "-..-", "-.--", "--.."
};

const char *morse_pattern(char letter)
{
    int c = toupper((unsigned char)letter);
    return (c >= 'A' && c <= 'Z') ? patterns[c - 'A'] : NULL;
}

void morse_play_letter(char letter, uint32_t unit_ms, uint16_t tone_hz,
                       morse_tone_fn tone, void *context)
{
    const char *pattern = morse_pattern(letter);
    if (pattern == NULL || tone == NULL || unit_ms == 0) return;

    for (const char *p = pattern; *p != '\0'; ++p) {
        uint32_t on_units = (*p == '-') ? 3U : 1U;
        tone(true, tone_hz, context);
        vTaskDelay(pdMS_TO_TICKS(on_units * unit_ms));
        tone(false, tone_hz, context);
        if (p[1] != '\0') vTaskDelay(pdMS_TO_TICKS(unit_ms));
    }
    vTaskDelay(pdMS_TO_TICKS(3U * unit_ms));
}
