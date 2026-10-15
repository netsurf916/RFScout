# Thermal

Thermal is RFScout's optional MLX90640 imaging application. Shared I2C/profile/lifecycle rules are defined in [Platform architecture](../ARCHITECTURE.md); calibration/history/palette improvements belong in [ROADMAP.md](../ROADMAP.md).

## Availability and lifecycle

The application uses hot-plug probing for an MLX90640 at I2C address `0x33` on the compatible external/shared I2C path. It requires activation but currently performs synchronous acquisition rather than owning a session-managed worker.

Loss/read failure marks the application unavailable and returns to Home through the normal app framework.

Thermal has **no SD-storage dependency**. Its availability is determined by the MLX90640/I2C path, so the app is expected to remain navigable with or without an SD card when the sensor is healthy. This has been verified on the target hardware.

When SD logging is mounted, the generic app logger can record Thermal lifecycle/telemetry. With the SD card absent there is intentionally no SD log, so console diagnostics or direct on-device observation must be used for that validation.

## Sensor and display

The MLX90640 native sensor resolution is **32x24**.

Current presentation:

- displays the image in a theme-framed view beside a surface-colored telemetry box, and draws the 32x24 native cells at a fixed 6x6 scale, avoiding false interpolation detail and reducing drawing calls from 27,648 to 768 rectangles per complete frame;
- uses an Inferno-like relative palette;
- lightly smooths both sensor samples and the relative display range to balance responsiveness against color pumping;
- shows frame-relative minimum, maximum, and center values; and
- bypasses theme color translation for literal thermal image pixels while keeping chrome/labels theme-aware.

The UI deliberately labels the data **RELATIVE IR** and **CAL PENDING**.

## Temperature semantics

RFScout does **not** currently present the displayed frame values as calibrated absolute temperatures. MLX90640 calibration-data extraction and per-pixel absolute-temperature conversion remain future work and must be hardware-validated before absolute values are claimed.

## Current limitations

Current behavior does not yet include calibrated absolute temperature, invalid-pixel filtering, user-lockable range, or retained history/statistics. The image intentionally retains visible native pixels instead of implying greater sensor resolution through interpolation. Acquisition reads pixels in 32-word bounded I2C blocks (24 transfers per subpage) to lower transaction overhead on the existing shared 400 kHz bus. Acquisition is polled every 50 ms while active, but complete-frame rate remains limited by sensor subpage delivery and shared I2C transfers; the UI change does not claim additional sensor frames.

## I2C failure handling

A complete-frame read failure discards pending subpage data and allows up to three successive failed acquisitions before treating the sensor as disconnected. This reduces false hot-plug events from transient bus contention; persistent errors still trigger the existing Home fallback. The shared I2C bus, battery devices, and watchdog require device-side validation when an electrical bus hang occurs. Pixel Quest boxes reuse `ui_game_draw_panel` rather than bespoke edges.

## Incremental acquisition

The MLX90640 provider now performs at most one 32-word pixel transfer per runtime poll, yielding to other system work between transfers. A complete image is published only after both subpages are captured. The sensor's 0x800D refresh field is set to 16 Hz subpages (instead of the 2 Hz factory default) on first active acquisition, preserving other control bits. Temporal pixel averaging and display-range averaging have been removed to favor immediate scene response. The poll interval is 1 ms; achievable display frame rate remains constrained by sensor conversion and shared-bus timing. These changes do not constitute a general-purpose I2C scheduler.

## Local display settings

Press Select in Thermal to open its app-local menu: Back, App lock, and Keep screen awake (default On). The latter temporarily suppresses inactivity dim/off while Thermal is foreground; explicit power button actions remain authoritative. The override is released on exit. Audio Spectrum also retains an awake display while running; its local toggle remains follow-up work.

## Raw-pixel checkerboard

The displayed sensor values are uncalibrated raw RAM words. Correcting per-pixel offset/gain and subpage residual patterns requires MLX90640 EEPROM calibration and an actual temperature or compensated-radiance pipeline. Alternating subpage assembly alone cannot eliminate pixel-to-pixel offsets; no synthetic spatial averaging is applied in this change.

## Frame acquisition pacing

Each runtime call performs up to four 32-word I2C transfers, rather than one; that requires six polls per subpage instead of 24 while still yielding to other system work between bursts. The shared bus is configured for 400 kHz. Firmware build and on-device frame-rate characterization are required before claiming a sustained update rate.

## Presentation follow-up

Both live-view panels now use the same Y origin and height. Pixel Quest uses the shared clipped-corner panel primitive, while simpler styles use theme surfaces without leftover rectangular outlines. The displayed 32x24 cells use a small per-frame spatial neighborhood filter to attenuate raw alternating-cell contrast. This is visual smoothing only: it cannot substitute for per-pixel EEPROM calibration and introduces no temporal averaging or additional sensor bus activity.

## Display alignment and shared-bus protection

The shared instrument-panel top is Y=24; the panel gutter and text inset are defined in `ui_layout.h`. Thermal renders its panels and `WAITING FOR FRAME` immediately on app entry, before the MLX90640 finishes acquiring a complete frame. The two global Pixel Quest bottom-edge ornaments were removed from `ui_chrome_draw()` for every view. Thermal now targets an 8 Hz subpage refresh to reduce occupancy of the shared 400 kHz I2C bus, and waits 250 ms before retrying transient failed reads; this is a stability tradeoff with a nominal maximum of four complete two-subpage images per second. I2C hardware timeouts and the reported corrupted battery-indicator backtrace still require hardware regression testing and root-cause investigation.
