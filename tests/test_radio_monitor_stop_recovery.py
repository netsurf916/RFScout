"""Exercise the actual radio_monitor_stop() C implementation with a scripted Wi-Fi driver.

The host harness extracts only the stop-state functions, avoiding dependency on
ESP-IDF radio hardware while testing the production recovery/error paths.
"""
import pathlib
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
MONITOR = ROOT / "main/services/wireless/radio_monitor.c"


def extract_function(source: str, name: str) -> str:
    match = re.search(
        r"(?m)^(?:static bool|bool|esp_err_t)\s+"
        + re.escape(name)
        + r"\s*\([^;]*?\)\s*\{",
        source,
        flags=re.DOTALL,
    )
    if match is None:
        raise AssertionError(f"Missing {name} in production radio monitor")
    opening = source.index("{", match.start())
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[match.start() : index + 1]
    raise AssertionError(f"Unterminated {name}")


HARNESS_BEGIN = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

typedef int esp_err_t;
typedef unsigned TickType_t;

#define ESP_OK 0
#define ESP_FAIL (-1)
#define ESP_ERR_TIMEOUT (-2)
#define ESP_ERR_WIFI_NOT_STARTED 0x3002
#define WIFI_CALLBACK_QUIESCE_TIMEOUT_MS 1000U
#define pdMS_TO_TICKS(a) (a)
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
#define ESP_LOGI(...) ((void)0)
#define portENTER_CRITICAL(a) ((void)(a))
#define portEXIT_CRITICAL(a) ((void)(a))

static int callback_lock;
static bool running;
static bool driver_stop_pending;
static uint16_t active_ap_channel_mask;
static bool capture_policy;
static unsigned tick_count;
static unsigned callbacks_active;
static esp_err_t restore_result;
static esp_err_t stop_answers[4];
static unsigned stop_count;
static unsigned stop_index;

static esp_err_t esp_wifi_stop(void)
{
    assert(stop_index < stop_count);
    return stop_answers[stop_index++];
}
static esp_err_t esp_wifi_set_promiscuous(bool enable)
{
    return enable ? restore_result : ESP_OK;
}
static TickType_t xTaskGetTickCount(void) { return tick_count; }
static void vTaskDelay(TickType_t ticks) { tick_count += ticks; }
static unsigned observation_callbacks_active(void) { return callbacks_active; }
'''

HARNESS_END = r'''
static void reset(void)
{
    running = true;
    driver_stop_pending = true;
    active_ap_channel_mask = 0x3fff;
    capture_policy = true;
    tick_count = 0;
    callbacks_active = 0;
    restore_result = ESP_FAIL;
    stop_count = 0;
    stop_index = 0;
}

static void results(esp_err_t first, esp_err_t second, esp_err_t third)
{
    stop_answers[0] = first;
    stop_answers[1] = second;
    stop_answers[2] = third;
    stop_count = 3;
}

int main(void)
{
    /* P1 reproduction: stop, restore and emergency cleanup all fail. The next
       stop MUST call esp_wifi_stop again, not return false success. */
    reset();
    results(ESP_FAIL, ESP_FAIL, ESP_OK);
    assert(radio_monitor_stop() == ESP_FAIL);
    assert(!running && driver_stop_pending);
    assert(radio_monitor_is_active());
    assert(stop_index == 2);
    assert(radio_monitor_stop() == ESP_OK);
    assert(!driver_stop_pending && !radio_monitor_is_active());
    assert(stop_index == 3);

    /* An initial failure with successful receive restoration retains active
       capture; a later stop must still retry physical driver shutdown. */
    reset();
    results(ESP_FAIL, ESP_OK, ESP_OK);
    restore_result = ESP_OK;
    assert(radio_monitor_stop() == ESP_FAIL);
    assert(running && driver_stop_pending);
    assert(stop_index == 1);
    assert(radio_monitor_stop() == ESP_OK);
    assert(!running && !driver_stop_pending);
    assert(stop_index == 2);

    /* Emergency cleanup succeeded: despite the first error, a retry cannot
       falsely report that the still-running driver was stopped. */
    reset();
    results(ESP_FAIL, ESP_OK, ESP_OK);
    assert(radio_monitor_stop() == ESP_FAIL);
    assert(!running && !driver_stop_pending);
    assert(stop_index == 2);
    assert(radio_monitor_stop() == ESP_OK);
    assert(stop_index == 2);

    /* ESP-IDF explicitly reports a stopped driver. */
    reset();
    results(ESP_ERR_WIFI_NOT_STARTED, ESP_OK, ESP_OK);
    assert(radio_monitor_stop() == ESP_OK);
    assert(!driver_stop_pending && !radio_monitor_is_active());
    assert(stop_index == 1);

    /* A callback-quiescence timeout is independently retryable, without a
       redundant hardware stop after the first successful stop. */
    reset();
    results(ESP_OK, ESP_OK, ESP_OK);
    callbacks_active = 1;
    assert(radio_monitor_stop() == ESP_ERR_TIMEOUT);
    assert(!driver_stop_pending && stop_index == 1);
    callbacks_active = 0;
    assert(radio_monitor_stop() == ESP_OK);
    assert(stop_index == 1);
    return 0;
}
'''


class RadioMonitorStopRecoveryTests(unittest.TestCase):
    def test_failed_stop_remains_pending_until_confirmed(self):
        compiler = shutil.which("cc")
        if compiler is None:
            self.skipTest("C compiler unavailable")

        source = MONITOR.read_text()
        functions = "\n\n".join(
            extract_function(source, name)
            for name in (
                "driver_is_stopped",
                "radio_monitor_is_active",
                "radio_monitor_stop",
            )
        )
        with tempfile.TemporaryDirectory() as folder:
            binary = pathlib.Path(folder) / "monitor_stop"
            harness = pathlib.Path(folder) / "monitor_stop.c"
            harness.write_text(HARNESS_BEGIN + functions + HARNESS_END)
            compiled = subprocess.run(
                [compiler, "-std=c11", "-Wall", "-Wextra", "-Werror", "-o",
                 str(binary), str(harness)],
                capture_output=True,
                text=True,
            )
            self.assertEqual(compiled.returncode, 0, compiled.stderr)
            subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    unittest.main()
