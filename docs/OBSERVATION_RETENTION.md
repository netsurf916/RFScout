# Live observation retention

RFScout live device lists are intended to remain useful in intermittent RF environments without becoming permanent history.

## WiFi / BT

Wi-Fi, Bluetooth, and retained surveillance-list aging are application-owned configuration. Each has an independent timeout expressed in seconds.

- default: 60 seconds for Wi-Fi, Bluetooth, and surveillance;
- minimum: 10 seconds;
- maximum: 300 seconds;
- adjustment step: 10 seconds.

The WiFi / BT application settings menu is the user-facing location for all three controls. Wi-Fi and Bluetooth source membership age independently: if one physical observation has been associated with both sources, expiration of one source does not remove the other source from the live entry. The live observation is removed only after none of its active sources remain within their configured windows.

The surveillance timeout applies to the retained surveillance list shown by the WiFi / BT application. It is intentionally independent from ordinary Wi-Fi and Bluetooth live-list aging so a user can choose how long classified detections remain visible without forcing the general device lists to use the same retention window.

Canonical configuration stores the three values under `apps.wifi_bt.retention` as `wifi_seconds`, `bluetooth_seconds`, and `surveillance_seconds`. Missing or invalid values retain the compiled 60-second defaults, so existing schema-v2 configurations migrate through the normal defaults-plus-overlay behavior.

## nRF24 detection presentation

Wi-Fi-like wide 2.4 GHz activity and hopping/BLE-like activity are separate classifier categories and remain separate in the detection list. The UI does not combine their counters into an `Other` row. These are spectrum-shape inferences, so compact display labels retain uncertainty (`Wi-Fi?` and `Hopping RF?`).

The normal CC1101 and nRF24 spectrum screens do not show a persistent `SETTINGS` footer label; Center remains the established app-settings action. Hardware-not-detected screens continue to show `CENTER: SETTINGS` because that text is actionable guidance in an exceptional state.
