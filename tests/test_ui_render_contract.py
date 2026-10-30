"""Structural regression for the app-owned state / framework-owned rendering boundary."""

import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
APPS = (
    "diagnostics", "system_events", "battery", "home", "wifi_ble", "hotspots",
    "cc1101", "nrf24", "recorder", "audio_spectrum", "thermal", "wardrive", "gps",
)
VIEWS = (
    "apps/ota/ota_view.c",
    "apps/thermal/thermal_view.c",
    "apps/hotspots/hotspots_view.c",
    "apps/nrf24/nrf24_packet_view.c",
    "apps/recorder/recorder_view.c",
    "apps/home/home_landing.c",
    "apps/gps/gps_view.c",
    "apps/cc1101/cc1101_view.c",
    "apps/radio_common/radio_app_view.c",
    "apps/wifi_ble/wifi_settings_view.c",
    "apps/audio_spectrum/view.c",
    "apps/home/storage_view.c",
    "apps/home/device_view.c",
    "services/power/power_policy_view.c",
    "apps/system_events/system_events_view.c",
    "apps/network_common/network_credentials.c",
    "notification_settings_view.c",
)


class EventDrivenPresentationContract(unittest.TestCase):
    def test_all_app_runtimes_support_render_event_and_never_present(self):
        for name in APPS:
            with self.subTest(app=name):
                code = (ROOT / "main" / "apps" / name / "app.c").read_text()
                self.assertIn("RFSCOUT_APP_RUNTIME_RENDER", code)
                self.assertNotIn("display_present(", code)

    def test_shared_views_never_flush_display(self):
        for view in VIEWS:
            with self.subTest(view=view):
                self.assertNotIn("display_present(", (ROOT / "main" / view).read_text())

    def test_global_app_lock_invalidates_active_view(self):
        app = (ROOT / "main" / "app_manager.c").read_text()
        lock_handler = app.split("bool app_manager_set_locked( bool a_locked )", 1)[1]
        lock_handler = lock_handler.split("bool app_manager_select( rfscout_app_id_t a_app )", 1)[0]
        self.assertIn("app_manager_request_render( active_app, true );", lock_handler)

    def test_frame_scheduler_owns_render_then_present(self):
        app = (ROOT / "main" / "app_manager.c").read_text()
        loop = (ROOT / "main" / "rfscout.c").read_text()
        self.assertIn("APP_BACKGROUND_RENDER_INTERVAL_US 200000LL", app)
        self.assertIn("RFSCOUT_APP_RUNTIME_RENDER", app)
        self.assertIn("app_manager_render_poll();", loop)
        self.assertLess(loop.index("app_manager_render_poll();"),
                        loop.index("(void)display_present();", loop.index("app_manager_render_poll();")))


if __name__ == "__main__":
    unittest.main()
