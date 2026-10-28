"""Regression for Hotspots preventing a gateway probe after network link-loss teardown."""

import pathlib
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
SOURCE = ROOT / "main/apps/hotspots/app.c"


def extract_function(source: str, name: str) -> str:
    pattern = rf"static (?:bool|void)\s+{name}\s*\([^)]*\)\s*\{{"
    match = re.search(pattern, source)
    if not match:
        raise AssertionError(f"Missing Hotspots function {name}")
    opening = source.index("{", match.start())
    depth = 0
    for i in range(opening, len(source)):
        if source[i] == "{":
            depth += 1
        elif source[i] == "}":
            depth -= 1
            if depth == 0:
                return source[match.start():i + 1]
    raise AssertionError(f"Unterminated Hotspots function {name}")


HARNESS_BEFORE = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
typedef int esp_err_t;
typedef int hotspot_stage_t;
typedef int hotspot_status_t;
#define ESP_OK 0
#define ESP_FAIL (-1)
#define HOTSPOT_STATUS_RED 2
#define HOTSPOT_STATUS_YELLOW 1
#define HOTSPOT_STAGE_JOIN 1
#define HOTSPOT_STAGE_HTTPS 3
#define HOTSPOT_STAGE_CAMERA 4
#define RFSCOUT_NETWORK_SESSION_READY 4
#define RFSCOUT_NETWORK_SESSION_LIMITED 3
#define RFSCOUT_NETWORK_SESSION_FAILED 5
#define HOTSPOTS_PHASE_CAMERA 7
#define HOTSPOTS_CAMERA_TIMEOUT_MS 2500U
#define HOTSPOTS_CAMERA_GUARD_US 3500000LL
typedef struct {
    int state;
    esp_err_t error;
    int32_t disconnect_reason;
} rfscout_network_session_status_t;
typedef struct {
    uint32_t gateway;
    int32_t disconnect_reason;
} fake_attempt_result_t;
static fake_attempt_result_t attempt_result;
static rfscout_network_session_status_t network;
static unsigned finished;
static unsigned camera_begins;
static unsigned capture_saves;
static bool gateway_capture_pending;
static hotspot_status_t finished_outcome;
static hotspot_stage_t finished_stage;
static esp_err_t finished_error;
static hotspot_stage_t limited_failed_stage;
static esp_err_t limited_error;
static int phase;
static int64_t phase_deadline_us;
static bool render_due;
static void network_session_status(rfscout_network_session_status_t *out) { *out = network; }
static void save_gateway_probe_result(int status, esp_err_t error) {
    (void)status;
    (void)error;
    if (gateway_capture_pending) {
        ++capture_saves;
        gateway_capture_pending = false;
    }
}
static void finish_attempt(hotspot_status_t status, hotspot_stage_t stage,
                           esp_err_t error, int64_t now) {
    (void)now;
    ++finished;
    finished_outcome = status;
    finished_stage = stage;
    finished_error = error;
}
static bool gateway_probe_eligible(const fake_attempt_result_t *result) {
    return result->gateway != 0;
}
static esp_err_t connectivity_probe_begin_camera(uint32_t gateway, uint32_t timeout) {
    (void)gateway;
    (void)timeout;
    ++camera_begins;
    return ESP_OK;
}
'''

HARNESS_AFTER = r'''
int main(void)
{
    attempt_result.gateway = 0x01020304;
    network.state = RFSCOUT_NETWORK_SESSION_FAILED;
    network.error = ESP_FAIL;
    network.disconnect_reason = 27;

    /* The network session already stopped Wi-Fi: do not launch HTTP HEAD. */
    start_limited_camera_probe(HOTSPOT_STAGE_HTTPS, ESP_FAIL, 100);
    assert(camera_begins == 0);
    assert(capture_saves == 0);
    assert(finished == 1);
    assert(finished_outcome == HOTSPOT_STATUS_RED);
    assert(finished_stage == HOTSPOT_STAGE_JOIN);
    assert(finished_error == ESP_FAIL);
    assert(attempt_result.disconnect_reason == 27);

    /* A usable LIMITED LAN may still be checked for gateway metadata. */
    finished = 0;
    network.state = RFSCOUT_NETWORK_SESSION_LIMITED;
    start_limited_camera_probe(HOTSPOT_STAGE_HTTPS, ESP_FAIL, 200);
    assert(camera_begins == 1);
    assert(gateway_capture_pending);
    assert(finished == 0);
    assert(phase == HOTSPOTS_PHASE_CAMERA);
    assert(phase_deadline_us == 200 + HOTSPOTS_CAMERA_GUARD_US);

    /* HTTPS/CAMERA polling must handle asynchronous link-loss first. */
    network.state = RFSCOUT_NETWORK_SESSION_FAILED;
    network.error = ESP_FAIL;
    network.disconnect_reason = 32;
    assert(finish_failed_network_attempt(300));
    assert(finished == 1 && finished_outcome == HOTSPOT_STATUS_RED);
    assert(capture_saves == 1 && !gateway_capture_pending);
    assert(attempt_result.disconnect_reason == 32);
    return 0;
}
'''


class HotspotsNetworkFailureTests(unittest.TestCase):
    def test_gateway_head_skipped_after_link_loss(self):
        compiler = shutil.which("cc")
        if not compiler:
            self.skipTest("C compiler unavailable")
        source = SOURCE.read_text()
        code = (
            HARNESS_BEFORE
            + extract_function(source, "finish_failed_network_attempt")
            + extract_function(source, "start_limited_camera_probe")
            + HARNESS_AFTER
        )
        with tempfile.TemporaryDirectory() as directory:
            c_path = pathlib.Path(directory) / "network_failure.c"
            exe = pathlib.Path(directory) / "network_failure"
            c_path.write_text(code)
            result = subprocess.run(
                [compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
                 str(c_path), "-o", str(exe)],
                capture_output=True, text=True,
            )
            self.assertEqual(result.returncode, 0, result.stderr)
            subprocess.run([str(exe)], check=True)

        # Verify the asynchronous state guard is present at both scheduling phases.
        for label in ("HOTSPOTS_PHASE_HTTPS", "HOTSPOTS_PHASE_CAMERA"):
            body = source.split("case " + label + ":", 1)[1].split("case HOTSPOTS_PHASE_", 1)[0]
            self.assertIn("finish_failed_network_attempt( a_now_us )", body)


if __name__ == "__main__":
    unittest.main()
