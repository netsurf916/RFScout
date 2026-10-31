"""Host regressions for RFScout-owned Coverity boundary fixes (CIDs in PR description).

The C functions are extracted from production source and compiled with a tiny host
harness so that tests exercise actual changed logic rather than a Python rewrite.
These tests do not replace a repeat Coverity scan or ESP32 hardware testing.
"""

import pathlib
import re
import shutil
import subprocess
import tempfile
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]


def production_function(path, symbol):
    source = (ROOT / path).read_text()
    match = re.search(r"\b" + re.escape(symbol) + r"\s*\(", source)
    if match is None:
        raise AssertionError(f"{symbol} not found in {path}")
    start = source.rfind("\nstatic ", 0, match.start())
    if start < 0:
        raise AssertionError(f"{symbol} does not have a static definition")
    opening = source.find("{", match.end())
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[start + 1 : index + 1]
    raise AssertionError(f"unterminated {symbol}")


def compile_and_run(source, *args):
    compiler = shutil.which("cc")
    if compiler is None:
        raise unittest.SkipTest("host C compiler unavailable")
    with tempfile.TemporaryDirectory() as directory:
        src = pathlib.Path(directory) / "test.c"
        executable = pathlib.Path(directory) / "test"
        src.write_text(source)
        compile_result = subprocess.run(
            [compiler, "-std=c11", "-Wall", "-Wextra", "-Werror", "-o", str(executable), str(src)],
            capture_output=True,
            text=True,
        )
        if compile_result.returncode:
            raise AssertionError(compile_result.stderr)
        subprocess.run([str(executable), *map(str, args)], check=True, capture_output=True, text=True)


class CoverityBoundaryRegressions(unittest.TestCase):
    def test_trace_parser_zero_capacity_and_truncation(self):
        function = production_function(
            "main/services/wireless/connectivity_probe.c", "copy_trace_value"
        )
        compile_and_run(
            r"""
#include <assert.h>
#include <stddef.h>
#include <string.h>
"""
            + function
            + r"""
int main(void)
{
    char output[4] = { 'X', 'X', 'X', 'X' };
    copy_trace_value("ip=123456\ncolo=DFW\n", "ip", output, 0U);
    assert(output[0] == 'X');
    copy_trace_value(NULL, "ip", output, sizeof(output));
    copy_trace_value("ip=123456", NULL, output, sizeof(output));
    copy_trace_value("ip=123456", "ip", NULL, sizeof(output));
    assert(output[0] == 'X');
    copy_trace_value("ip=123456\ncolo=DFW\n", "ip", output, sizeof(output));
    assert(0 == strcmp(output, "123"));
    copy_trace_value("ip=123456\ncolo=DFW\n", "colo", output, sizeof(output));
    assert(0 == strcmp(output, "DFW"));
    return 0;
}
"""
        )

    def test_saved_bssid_last_slot_and_corrupt_count(self):
        function = production_function(
            "main/services/wireless/saved_networks.c", "append_bssid"
        )
        compile_and_run(
            r"""
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#define SAVED_NETWORK_BSSID_MAX_COUNT 3U
#define SAVED_NETWORK_BSSID_REJECTED 2U
typedef struct { uint8_t state; } saved_network_bssid_t;
typedef struct {
    size_t bssid_count;
    saved_network_bssid_t bssids[SAVED_NETWORK_BSSID_MAX_COUNT];
} saved_network_entry_t;
"""
            + function
            + r"""
int main(void)
{
    saved_network_entry_t item = { .bssid_count = 3U, .bssids = {{1}, {1}, {2}} };
    assert(append_bssid(&item) == &item.bssids[2]);
    assert(item.bssid_count == 3U);

    item = (saved_network_entry_t){ .bssid_count = 3U, .bssids = {{1}, {2}, {1}} };
    assert(append_bssid(&item) == &item.bssids[2]);
    assert(item.bssids[1].state == 1U);
    assert(item.bssid_count == 3U);

    item = (saved_network_entry_t){ .bssid_count = 3U, .bssids = {{1}, {1}, {1}} };
    assert(NULL == append_bssid(&item));
    assert(item.bssid_count == 3U);
    item.bssid_count = 4U;
    assert(NULL == append_bssid(&item));
    return 0;
}
"""
        )

    def test_cc1101_negative_rssi_saturates(self):
        function = production_function("main/services/radio/cc1101.c", "decode_rssi")
        compile_and_run(
            r"""
#include <assert.h>
#include <stdint.h>
"""
            + function
            + r"""
int main(void)
{
    assert(decode_rssi(0U) == -74);
    assert(decode_rssi(127U) == -11);
    assert(decode_rssi(128U) == -128);
    assert(decode_rssi(150U) == -127);
    assert(decode_rssi(255U) == -74);
    for(unsigned raw = 0U; raw <= 255U; ++raw)
    {
        const int8_t decoded = decode_rssi((uint8_t)raw);
        assert(decoded >= -128 && decoded <= -11);
    }
    return 0;
}
"""
        )

    def test_persistent_scope_existing_file_is_not_directory(self):
        function = production_function(
            "main/services/storage/persistent_store.c", "ensure_directory"
        )
        with tempfile.TemporaryDirectory() as directory:
            target = pathlib.Path(directory) / "created-dir"
            existing_file = pathlib.Path(directory) / "existing-file"
            existing_file.write_text("not a directory")
            compile_and_run(
                r"""
#include <assert.h>
#include <errno.h>
#include <sys/stat.h>
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_STATE 0x103
"""
                + function
                + r"""
int main(int argc, char **argv)
{
    assert(argc == 3);
    assert(ensure_directory(argv[1]) == ESP_OK);
    assert(ensure_directory(argv[1]) == ESP_OK);
    assert(ensure_directory(argv[2]) == ESP_ERR_INVALID_STATE);
    return 0;
}
""",
                target,
                existing_file,
            )

    def test_gps_movement_index_has_upper_bound_guard(self):
        source = (ROOT / "main/device_config.c").read_text()
        function = source.split("uint8_t device_config_gps_movement_step(", 1)[1]
        function = function.split("esp_err_t device_config_set_gps_timepulse(", 1)[0]
        self.assertIn("index < 0 || index >= count", function)


if __name__ == "__main__":
    unittest.main()
