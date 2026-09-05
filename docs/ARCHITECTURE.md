# Architecture

This document describes architecture implemented by the current source tree. Forward-looking architecture and feature planning belong in [ROADMAP.md](ROADMAP.md).

## Board and platform boundary

RFScout is one source tree producing board-specific firmware images. The current target is **LILYGO T-Embed CC1101 Plus**. `RFSCOUT_BOARD` selects physical definitions under `main/boards/<target>/`.

Application code depends on services, semantic input, and capabilities rather than board names. `platform_input_get()` is the board-neutral input contract. See [BOARD_PORTING.md](BOARD_PORTING.md).

The current target requires usable external PSRAM for large platform allocations such as the framebuffer; internal SRAM remains reserved for system, stack, DMA/hardware-constrained, and latency-sensitive use.

## Application modules, manager, and lifecycle

Compiled-in applications are described by `rfscout_app_descriptor_t`. Each app owns its identity, user-facing name, probe policy/function, delayed-activation policy, and platform-service dependency mask. The framework discovers these descriptors through the compiled app registry instead of maintaining duplicate app metadata.

Normal navigation/input/render invocation remains centralized in `app_manager` so application switching has one predictable control loop. Hardware lifecycle does not: descriptor service bits identify capabilities the app consumes, and `app_session` maps those capabilities to platform-owned start/stop/drain/recovery behavior.

Source layout follows that boundary:

```text
main/
  apps/<app>/           app descriptor, views, analysis, app-owned state
  services/<service>/   reusable hardware/platform capability implementation
  boards/<target>/      board-specific physical definitions
  app_*.c               core app orchestration/lifecycle/session framework
```

Top-level navigation order is:

```text
System Events -> Home / Device Settings -> WiFi / BT -> CC1101 -> nRF24 -> Recorder -> Thermal
```

Disabled or unavailable apps are skipped. CC1101 and nRF24 use startup-cached availability probes; Recorder and Thermal use hot-plug probing.

App activation is separate from probing. The lifecycle contract is documented in [APP_LIFECYCLE.md](APP_LIFECYCLE.md): enter/settle/activate are distinct phases, and leaving an active app transactionally quiesces every platform service declared by that app before its app-owned memory is reclaimed. Apps do not implement driver-specific reachability checks.

## Semantic event and audio boundaries

Physical input is translated into semantic input, interpreted by an app/menu, and only accepted actions/state changes emit `rfscout_event_emit()` events. Raw contact or encoder movement does not itself imply feedback.

RFScout distinguishes:

1. incidental interaction/general feedback;
2. application-owned detection notifications;
3. intentional application content audio.

`app_audio_policy.c/.h` describes application-native traits such as Recorder's intentionally quiet incidental behavior. `audio_policy.c/.h` independently defines the device-wide **Normal** / **Quiet** presentation policy. `feedback.c/.h` combines semantic event meaning, native app policy, and device Audio policy. `notification.c/.h` owns detection vocabulary/preferences; `alert.c/.h` applies global notification mode/volume plus device Audio policy. The audio-output service owns asynchronous speaker execution and class arbitration.

The visual theme layer no longer participates in sound resolution. The display/theme service owns only the Indoor, Outdoor, and Night palettes. A theme change therefore cannot alter audibility, and an Audio-policy change cannot alter colors.

Quiet suppresses incidental/event feedback and detection-notification presentation without mutating the underlying app notification gates, Morse mappings, global mode, or global volume. Explicit application content audio such as Recorder playback bypasses that suppression.

## WiFi / BT data path

```text
Wi-Fi monitor ---\
                  +--> normalized observation --> prefix identity --> surveillance classifier
BLE monitor -----/              |                        |
                                v                        v
                         recent device store       retained detections
                                |                        |
                                +-----------> WiFi / BT UI
```

Wi-Fi and BLE source membership age independently. Surveillance history has a third timeout. All three currently use 10–300 second values in 10-second steps, defaulting to 60 seconds.

The normalized observation collection is app-session PSRAM and grows according to available resources rather than a fixed item ceiling. Presentation code bounds display work independently from retained collection capacity.

## CC1101 and nRF24

CC1101 uses a persistent worker for bounded RSSI sweeps and conservative behavior classification. nRF24 uses RPD sweeps plus a separate ingestion/storage path for validated receive-only ESB observations. Both workers are platform services and must quiesce before their app session can be reclaimed.

## Recorder

Recorder currently owns I2S0/PDM microphone capture directly, runtime PDM-clock/effective-rate measurement, live monitor FFT, fixed input gain, async recording post-processing, SD recording catalog, and I2S1 playback through the shared speaker service.

Recorder worker/channel quiescence is a platform-session responsibility. Restoring the exact recording/monitor/playback operation after an aborted transition is semantic app state and remains app-specific.

The planned shared calibrated Audio Input service is not implemented yet.

## Thermal

MLX90640 is an optional hot-plug I2C/Qwiic peripheral exposed through the thermal platform service. Thermal acquires synchronously only while active. Literal image pixels bypass semantic theme translation while labels/chrome remain theme-aware.

## Configuration and persistence

`device_config.c/.h` owns defaults plus optional `/sdcard/rfscout.json` schema-v3 persistence. Schema v1/v2 are accepted and migrated where supported; unknown fields are ignored; future schema versions are rejected.

Canonical configuration covers:

- display brightness/idle and visual theme;
- independent device Audio policy;
- power-button actions and protective power policy;
- app enablement/startup;
- logging;
- global/app notification state;
- WiFi / BT retention;
- Recorder preferences;
- timezone and saved wall-clock continuity state.

Legacy `display.theme: "discipline"` maps to visual Night plus Quiet audio policy. Canonical v3 output writes only the independent fields (`display.theme` and `audio.policy`).

See [CONFIGURATION.md](CONFIGURATION.md).

## Power architecture

The power service owns the physical power-button gesture state machine and low-battery/high-internal-temperature protective policy. Both route transitions through common semantic power execution and the shared presentation overlay.

Battery policy deliberately distinguishes cached telemetry from fresh reads. Low-battery protection requires fresh state-of-charge and charger evidence. High-temperature policy uses the SoC internal temperature capability, not the MLX90640 app. Physical power-off is not assumed to work while external power is present; thermal policy can conservatively fall back to deep sleep.

The display service applies the global power-transition overlay before display flush. The overlay is not app framebuffer content and is shared by manual and protective transitions.

See [POWER.md](POWER.md).

## Display-idle architecture

Display dim/off is presentation policy, not application lifecycle or system sleep. The off state puts the ST7789 into hardware sleep and suppresses physical presentation while logical framebuffer/application work continues. Wake consumes the complete physical gesture before normal input or power-button handling.

See [DISPLAY_IDLE.md](DISPLAY_IDLE.md).

## Deep-sleep session state

`session_state.c/.h` retains a small RTC-backed record containing the active app and input-lock state. Menus, edit state, queued sounds, and active Recorder work are not resumed. Dirty canonical configuration is flushed before deep sleep.

## Shared hardware boundaries

On the current target:

- LCD, microSD, CC1101, and nRF24 share SPI resources;
- battery/power devices and external sensors share board I2C;
- Recorder uses I2S0 for PDM input;
- the asynchronous speaker service uses I2S1.

## Storage, UI, and releases

The storage service owns SD mount/state and queued rotating logs. Application views render their own content; the display service owns common top chrome and final global overlays/presentation.

Pull-request and `main` CI perform formatting/tests and eligible firmware packaging. Durable releases are prepared deliberately through the release workflow; see [RELEASING.md](RELEASING.md).

## System Events application

The RAM-only structured system-event ring is a platform service. The optional System Events application is a read-only view over that service and is disabled by default. Its canonical navigation slot is immediately before Home/Device Settings. Center toggles a stable browse snapshot: while browsing, rotary input scrolls retained events; outside browse mode, rotary input remains application navigation. The view owns no retained dynamic event storage and leaving it discards only its small UI snapshot.
