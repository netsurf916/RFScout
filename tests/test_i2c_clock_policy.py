"""Regression checks for the board-owned per-device I2C transfer policy.

These are source-contract checks; they do not validate electrical bus timing.
"""

import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
BUS = (ROOT / "main/services/board/board_i2c.c").read_text()
HEADER = (ROOT / "main/services/board/board_i2c.h").read_text()
BATTERY = (ROOT / "main/services/power/battery.c").read_text()
THERMAL = (ROOT / "main/services/thermal/mlx90640.c").read_text()
BOARD = (ROOT / "main/services/board/board.c").read_text()


class PerDeviceClockPolicy(unittest.TestCase):
    def test_bus_has_per_device_speed_without_global_clock_change(self):
        self.assertIn("BOARD_I2C_STANDARD_SPEED_HZ 100000U", HEADER)
        self.assertIn("BOARD_I2C_FAST_SPEED_HZ 400000U", HEADER)
        self.assertIn(".scl_speed_hz = a_scl_speed_hz", BUS)
        self.assertIn("board_i2c_add_device_with_speed(", BUS)
        self.assertIn("BOARD_I2C_STANDARD_SPEED_HZ, a_device", BUS)
        self.assertNotIn("i2c_master_bus_reset(", THERMAL)

    def test_battery_startup_and_recovery_use_standard_speed(self):
        self.assertRegex(
            BATTERY,
            r"attach_optional_device\([\s\S]*?a_speed_hz[\s\S]*?"
            r"board_i2c_add_device_with_speed\( a_address, a_speed_hz, a_device \)",
        )
        for name, handle in (
            ("BQ27220 fuel gauge", "gauge_device"),
            ("BQ25896 charger", "charger_device"),
        ):
            sites = re.findall(
                rf'"{name}",\s*(BOARD_I2C_\w+),\s*&{handle}',
                BATTERY,
            )
            self.assertEqual(len(sites), 3, name)  # initial attach and both retry orders
            self.assertTrue(
                all(speed == "BOARD_I2C_STANDARD_SPEED_HZ" for speed in sites),
                (name, sites),
            )

    def test_thermal_falls_back_to_safe_speed_after_repeated_failures(self):
        self.assertIn("active_scl_speed_hz = BOARD_I2C_FAST_SPEED_HZ", THERMAL)
        self.assertIn("MLX90640_READ_FAILURE_RETRY_COUNT", THERMAL)
        self.assertIn("board_i2c_remove_device( &device )", THERMAL)
        self.assertIn("active_scl_speed_hz = BOARD_I2C_STANDARD_SPEED_HZ", THERMAL)
        self.assertIn("board_i2c_add_device_with_speed(", THERMAL)
        self.assertRegex(
            THERMAL,
            r"ESP_OK == removed[\s\S]*?active_scl_speed_hz = "
            r"BOARD_I2C_STANDARD_SPEED_HZ[\s\S]*?ESP_OK == attach",
        )
        self.assertIn("refresh_configured = false;", THERMAL)
        self.assertIn("return ESP_ERR_NOT_FINISHED;", THERMAL)

    def test_remove_preserves_handle_on_failure(self):
        fn = BUS.split("esp_err_t board_i2c_remove_device(", 1)[1]
        fn = fn.split("esp_err_t board_i2c_transmit_receive(", 1)[0]
        self.assertIn("i2c_master_bus_rm_device( *a_device )", fn)
        self.assertRegex(fn, r"if\( ESP_OK == result \)\s*\{\s*\*a_device = NULL;")

    def test_boot_fingerprint_is_not_replaced_with_global_speed_change(self):
        self.assertIn("i2c_master_probe(", BOARD)
        self.assertIn("i2c_del_master_bus( bus )", BOARD)
        self.assertIn("i2c_master_probe(", BUS)


if __name__ == "__main__":
    unittest.main()
