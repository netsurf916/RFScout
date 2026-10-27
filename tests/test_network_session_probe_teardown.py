"""Regression for the automatic network-session failure path quiescing a live HTTP worker."""

import pathlib
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
SOURCE = ROOT / "main/services/wireless/network_session.c"


def extract_function(source: str, name: str) -> str:
    pattern = rf"(?:static\s+)?(?:void|esp_err_t)\s+{name}\s*\([^)]*\)\s*\{{"
    match = re.search(pattern, source)
    if match is None:
        raise AssertionError(f"Missing network session function: {name}")
    opening = source.index("{", match.start())
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[match.start() : index + 1]
    raise AssertionError(f"Unterminated function: {name}")


HARNESS_BEFORE = r'''
#include <assert.h>
#include <stdint.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_ERR_TIMEOUT (-1)
#define ESP_FAIL (-2)
#define RFSCOUT_NETWORK_SESSION_FAILED 4
#define ESP_LOGW(...) ((void)0)
#define esp_err_to_name(x) "error"
static const char *TAG = "network_session";
typedef struct {
    int state;
    esp_err_t error;
    int32_t disconnect_reason;
} fake_status_t;
static fake_status_t status;
static int64_t phase_started_us;
static esp_err_t probe_result;
static esp_err_t station_result;
static unsigned probe_calls;
static unsigned station_calls;
static esp_err_t connectivity_probe_stop(void) {
    ++probe_calls;
    return probe_result;
}
static esp_err_t rfscout_wifi_station_stop(void) {
    ++station_calls;
    return station_result;
}
static void reset_logical_state(void) { status.state = 0; }
'''

HARNESS_AFTER = r'''
int main(void) {
    status.state = 2;
    probe_result = ESP_ERR_TIMEOUT;
    station_result = ESP_OK;

    /* Link-loss failure must not stop Wi-Fi while a worker still runs. */
    fail_session(ESP_FAIL, 42);
    assert(probe_calls == 1 && station_calls == 0 && status.state == 2);

    /* Retry after worker quiescence, preserving original failure details. */
    probe_result = ESP_OK;
    fail_session(ESP_FAIL, 42);
    assert(probe_calls == 2 && station_calls == 1);
    assert(status.state == RFSCOUT_NETWORK_SESSION_FAILED);
    assert(status.error == ESP_FAIL && status.disconnect_reason == 42);

    /* An explicit release obeys exactly the same ordering. */
    status.state = 2;
    probe_result = ESP_ERR_TIMEOUT;
    assert(network_session_release() == ESP_ERR_TIMEOUT);
    assert(probe_calls == 3 && station_calls == 1 && status.state == 2);

    probe_result = ESP_OK;
    station_result = ESP_OK;
    assert(network_session_release() == ESP_OK);
    assert(probe_calls == 4 && station_calls == 2 && status.state == 0);

    return 0;
}
'''


class NetworkSessionProbeTeardownTests(unittest.TestCase):
    def test_link_loss_defers_station_stop_until_http_worker_is_quiescent(self):
        compiler = shutil.which("cc")
        if not compiler:
            self.skipTest("C compiler unavailable")
        source = SOURCE.read_text()
        c_program = (
            HARNESS_BEFORE
            + extract_function(source, "quiesce_probe_before_station_stop")
            + extract_function(source, "fail_session")
            + extract_function(source, "network_session_release")
            + HARNESS_AFTER
        )
        with tempfile.TemporaryDirectory() as folder:
            src = pathlib.Path(folder) / "network_teardown.c"
            executable = pathlib.Path(folder) / "network_teardown"
            src.write_text(c_program)
            completed = subprocess.run(
                [compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
                 str(src), "-o", str(executable)],
                capture_output=True, text=True,
            )
            self.assertEqual(completed.returncode, 0, completed.stderr)
            subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    unittest.main()
