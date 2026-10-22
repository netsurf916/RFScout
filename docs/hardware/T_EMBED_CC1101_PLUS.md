# LILYGO T-Embed CC1101 Plus

**Support status:** the only runtime hardware profile supported by RFScout `main`. This page records implemented platform-specific behavior; shared resource contracts are in [Architecture](../ARCHITECTURE.md). Proposed boards and features belong only in [Roadmap](../ROADMAP.md).

## Identification and display

RFScout identifies this board through an early I²C probe of the BQ25896 charger (0x6B) or BQ27220 gauge (0x55) on SDA GPIO8 / SCL GPIO18. If no supported profile is recognized, startup leaves ordinary peripheral pins untouched for recovery flashing. The runtime profile is immutable after selection.

- MCU: ESP32-S3; display: ST7789, 320 × 170 logical pixels (170 × 320 native orientation).
- Input: rotary encoder GPIO4/GPIO5, center select GPIO0; separate user/power key GPIO6.
- Power latch: GPIO15; backlight PWM GPIO21.
- SPI bus: MOSI GPIO9, MISO GPIO10, SCLK GPIO11. Display CS GPIO41, DC GPIO16; SD CS GPIO13; CC1101 CS GPIO12; nRF24 CS GPIO44.
- CC1101 GDO2 GPIO38, GDO0 GPIO3, RF switches GPIO47/GPIO48.
- PDM microphone data GPIO42 / clock GPIO39; speaker I2S LRCLK GPIO40 / BCLK GPIO46 / data GPIO7.
- Optional MLX90640 thermal sensor at I²C 0x33. Other optional peripheral presence is validated through the respective provider's probe.

The pin values above reflect `main/services/board/board.c`. The board profile declares capabilities, not guaranteed peripheral presence.

## GPS and nRF24 UART pin sharing

The supported external GPS UART uses ESP32 GPIO43 (TX) and GPIO44 (RX), overlapping the nRF24 CE/CS control pins. GPS detection depends on valid NMEA traffic, not a position fix. The platform arbitrates access: GPS with valid NMEA has priority; an nRF24 app can use its radio when GPS is not detected. Swapping or hot-plugging these accessories while scanning is active may need manual recovery and should not be treated as lossless.

A GPS without a fix can still hold UART priority. A valid, fresh position fix is separately required before recording geographic capture metadata. The GPS driver probes supported UART baud rates and may use CASIC receiver-rate configuration when verified; see [GPS](../apps/GPS.md).

## Shared resources and operational limits

The display, SD card, CC1101 and nRF24 share SPI resources; use the board SPI arbitration rather than direct uncoordinated transfers. Wi-Fi and BLE share ESP32-S3 2.4 GHz airtime. The ESP32-S3 does not provide 5 GHz Wi-Fi reception. Storage, radio callbacks, and streaming workflows must respect the platform's session ownership and teardown rules.

The runtime supports deep sleep, screen off, and physical power-off with distinct behavior. See [Architecture](../ARCHITECTURE.md) and [Home](../apps/HOME.md). Optional hardware failure does not imply that the entire board is unsupported.

## Battery diagnostics application (implementation contract)

The optional Battery application is disabled by default and appears in navigation between System Events and Home when enabled. Existing persisted application IDs must remain stable; navigation order is independent of those IDs. Enablement follows the normal application configuration and availability mechanism. The app is read-only for the initial diagnostic release.

The application consumes a snapshot from the shared battery/power service. It must not directly access I2C, charger/gauge registers, board pins, or the power policy. The service owns the BQ27220 gauge and BQ25896 charger, including probing, recovery, rate limiting, register decoding, data freshness, and faults. Each metric must distinguish unsupported, unavailable, stale, and valid states; no cached reading may be silently presented as live telemetry.

The dashboard should show state of charge, measured voltage, signed current, charging phase, external-power state, and gauge/charger availability. Further pages should display remaining/full/design capacity, state of health, cycle count, temperature and time estimates when supported, plus charger configuration (charge enable, charge current, termination current, regulation voltage, recharge threshold, input limits, watchdog/safety timer) and decoded fault/thermal states. Unsupported metrics are shown as unavailable rather than guessed. Record a read-only raw-register snapshot with timestamps to support troubleshooting, especially discrepancies between charger termination and gauge SOC.

Use the active UI theme palette for normal, success, warning, error, and unavailable values. Battery silhouette, fill, current direction, and charging-flow illustrations must use existing shared display drawing primitives and adapt to theme changes. The app must preserve normal navigation, screen refresh, and resource ownership semantics. Do not interpret the reported gauge percentage as a charger-programmed SOC cutoff; BQ25896 regulation/termination is voltage/current based.

Do not write charging configuration or reset gauge learning data in this diagnostic release. Future charging controls require battery-specific validated limits, explicit user confirmation, safety review, and tests for watchdog reset and device power-cycle behavior. Validate gauge register definitions against the exact BQ27220 technical reference before implementing capacity/health reads.

Tests should cover decoded register values, faults, stale telemetry, I2C recovery, app enablement defaults, navigation placement, and theme-independent semantic colors. Keep application documentation consolidated with the existing Home and hardware pages.

### Battery visualization and threshold markers

The optional Battery app's primary illustration uses the shared drawing primitives and theme palette. A segmented/Krell-style battery meter may display thin threshold markers over the charge fill, with a concise legend and numerical values. The low-battery policy marker must use the existing configured threshold, distinguish a low-battery trigger from an unconditional physical power-off, and hide or annotate the marker when the policy is disabled or its external-power prerequisites cannot be confirmed. The charge-target marker must only be drawn when a real, supported charge target is configured and measurable; do not invent an 80% cutoff or imply that the BQ25896 provides a native percentage-based charge limit. Voltage regulation limits, if later exposed, must be identified as voltage rather than SOC percentages. Never let a marker obscure the numerical SOC or imply that stale telemetry is fresh.

Follow `CONTRIBUTING.md`: clang-format 18, Allman braces, `a_` argument names, Doxygen public interfaces, `-Werror`, caller-owned snapshots, shared `ui_menu` for menu rows, and `ui_icon` for reusable icons. Preserve stable app IDs, app lifecycle and framework-owned navigation; route all battery I2C through the shared battery service and board abstraction. Update existing Home/hardware/architecture documentation for established contracts; add only the Battery app's canonical app page if necessary under the existing one-page-per-app documentation pattern. Avoid duplicate service guides and speculative roadmap pages. Host preview should cover all themes, marker positions, missing/disabled thresholds, stale measurements, and 0/100% bounds.
