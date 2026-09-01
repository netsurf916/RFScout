# RFScout capabilities

This document summarizes behavior implemented on current `main`. Planned features and design ideas belong in [ROADMAP.md](ROADMAP.md).

## Supported hardware and application order

The only supported target is currently **LILYGO T-Embed CC1101 Plus**. RFScout uses a board-target/capability boundary so application code depends on semantic services rather than board model names.

Top-level application order is:

1. WiFi / BT
2. CC1101
3. nRF24
4. Recorder
5. Thermal, when enabled and detected
6. Home / Device Settings

Disabled/unavailable applications are skipped. Home is always available. A disabled or unavailable preferred startup target falls back to Home for the current boot without erasing the saved preference.

## WiFi / BT

WiFi / BT passively observes Wi-Fi and BLE activity, maintains recent-device state, performs IEEE longest-prefix vendor attribution, distinguishes private/random addresses from unresolved public addresses, and applies conservative surveillance-oriented classification from multiple signals.

Wi-Fi, Bluetooth, and retained surveillance history currently have independent 10–300 second retention controls in 10-second steps, defaulting to 60 seconds. Wi-Fi and BLE membership age independently inside merged observations. Ordinary Wi-Fi/BLE discovery cues can be enabled independently; classified surveillance detections use app-specific notification categories.

Leaving WiFi / BT stops both collectors.

## CC1101

CC1101 performs receive-only RSSI sweeps with configurable profiles/range/step, a live histogram, bounded 128-sample sweeps, conservative RF-behavior categories, application-owned notifications, and count reset. Classification labels are heuristics about observed RF behavior, not decoded product identity.

## nRF24

nRF24 performs receive-only 2.4 GHz RPD scanning with configurable channel bounds/dwell, a fast-decay activity histogram, conservative classification, bounded storage for validated Enhanced ShockBurst observations, packet preview/browser surfaces, app-owned notifications, and count reset.

The current scanner itself produces RPD energy observations only, so packet surfaces remain empty until receive-only ESB discovery/validation feeds the ingestion API. RFScout does not generate ACKs, inject, replay, or actively discover devices.

## Recorder

Recorder provides SD-backed 16-bit mono WAV capture/playback, runtime PDM clock and delivered PCM-rate measurement, measured-rate WAV metadata, asynchronous trim/DC-removal/normalization, recording browsing/deletion, independent playback volume, live monitor FFT, and fixed input gain from 0 through +24 dB in 3 dB steps.

Recorder has a native audio-centric policy: incidental feedback is suppressed while intentional content audio remains available. The historical `RFSCOUT_APP_AUDIO` / `audio_spectrum` compatibility naming remains internally.

## Thermal

Thermal is available when an enabled MLX90640 is detected on the external I2C/Qwiic interface. Its native 32x24 data is interpolated for the LCD. Literal thermal imagery bypasses semantic theme translation; chrome and labels remain theme-aware.

## Home / Device Settings

Home is the non-disableable Device application's branded landing state. Center opens Device Settings.

Implemented global settings include:

- brightness, display dim timeout, screen-off timeout, and visual theme;
- independent `Normal` / `Quiet` Audio policy;
- configurable short/long power-button actions;
- low-battery and high-internal-temperature protective power policy;
- 12/24-hour clock, timezone, and manual date/time;
- startup application and per-app enablement;
- SD logging;
- global notification mode and volume;
- explicit configuration save.

Small finite settings use direct center-to-cycle interaction. Per-app notification mappings and WiFi / BT retention controls remain in their owning app menus.

Visual themes are **Indoor**, **Outdoor**, and **Night**. They affect presentation only. The independent Audio policy is:

- **Normal** — permits incidental semantic feedback and enabled detection notifications subject to app/global policy;
- **Quiet** — suppresses incidental/event audio and detection-notification presentation without changing the saved notification gates, mappings, mode, or volume.

Quiet does not suppress explicitly requested application content audio such as Recorder playback. Recorder's native app policy remains a separate layer and can still suppress incidental feedback in Normal mode.

## Power and display behavior

Power-button actions are board-capability aware. Short press may be `None`, `Screen off`, `Sleep`, or `Power off`; long press may be `None` or `Power off`. A dimmed/off display consumes the complete first wake gesture before power-button semantics run.

Protective policy can respond to confirmed low battery or high ESP32 internal temperature with `None`, `Sleep`, or `Power off`, subject to board support. Low battery requires fresh fuel-gauge and charger evidence; high-temperature power-off falls back to sleep when removable power cannot be freshly confirmed. See [POWER.md](POWER.md).

Automatic display dim/off affects LCD presentation only. Applications, logging, RF acquisition, framebuffer updates, and audio behavior continue. The ST7789 enters hardware sleep for the off state. See [DISPLAY_IDLE.md](DISPLAY_IDLE.md).

## Feedback, notifications, and content audio

RFScout distinguishes semantic interaction feedback, application-owned detection notifications, and intentional application content audio. Global notification presentation is `OFF`, `TONE`, or `MORSE` plus volume; each detecting app owns its notification gate and mappings.

`feedback` combines event meaning, the source app's native audio policy, and the independent device Audio policy. `alert` applies the same device Audio policy to detection-notification presentation. Neither path consults the visual theme. Speaker execution is asynchronous and disposable UI feedback is not allowed to build a long backlog behind content audio.

## Configuration and persistence

Persistent preferences use `/sdcard/rfscout.json` schema version **3**. Loading starts from defaults and overlays recognized valid fields; schema v1/v2 are translated where supported. Unknown keys are ignored and pruned on the next canonical save. Future schema versions are rejected rather than destructively rewritten.

Legacy `Discipline` theme state migrates to **Night + Quiet**. Indoor/Outdoor/Night remain the three visual themes; `audio.policy` stores `normal` or `quiet` separately. See [CONFIGURATION.md](CONFIGURATION.md).

Deep sleep separately retains a small RTC-backed session record containing the active app and input-lock state. Open menus, edit state, queued sounds, and active Recorder operations are not resumed.

## Shared UI/status and storage

Application views use shared top chrome for title, local time/status, battery, and semantic color roles. Status information includes SD/error state and battery/charging telemetry when available.

RFScout can run without SD, but SD provides canonical configuration persistence, Recorder files, and asynchronous rotating logs. Logging uses `rfscout-00.log` through `rfscout-99.log`, up to 4 MiB per segment, with a free-space reserve policy.

## Hardware/resource boundaries

On the current target, LCD/microSD/CC1101/nRF24 share SPI resources; board power telemetry and external sensors share I2C; Recorder uses I2S0 for PDM input; the speaker service uses I2S1. The current firmware also depends on usable external PSRAM for large persistent platform allocations such as the framebuffer.

## Not currently implemented

The v0.4.0 app-scoped PSRAM memory manager, dynamic `Instant`/`Never` WiFi / BT retention, shared calibrated Audio Input service, CW sender, broader session persistence, GPS/location UI, NFC/IR tools, and active RF transmission/replay remain roadmap work.