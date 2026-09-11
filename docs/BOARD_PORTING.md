# Board profiles and porting

RFScout uses one ESP32-S3 firmware image for the supported LILYGO T-Embed CC1101 Plus and T-Deck Plus hardware family. A minimal hardware fingerprint runs before ordinary board GPIO or peripheral initialization, selects one immutable runtime profile, and then normal services probe only hardware that is legal for that profile.

## Runtime profile model

Board definitions live in `main/services/board/board.{h,c}`. The selected profile owns physical facts and expected capabilities, including:

- GPIO assignments and shared-bus wiring;
- display geometry, transform metadata, and panel offsets;
- peripheral addresses and interrupt pins;
- storage, radio, audio, input, location, and navigation expectations;
- battery-provider metadata and semantic power capabilities;
- the board/profile name and ID.

Applications must not branch on board IDs or names. They consume semantic services, provider availability, input events, and the managed application view.

A profile capability means RFScout has a provider it is prepared to use safely on that profile. Merely knowing pins or addresses for hardware is not enough to advertise a capability. Reference-derived metadata may therefore exist for a peripheral whose provider remains disabled.

## Detection versus capability probing

Board-profile detection and ordinary device probing are intentionally separate stages:

1. perform one minimal, passive I2C fingerprint using only the candidate profile wiring;
2. select exactly one immutable runtime board profile;
3. initialize only buses and pins legal for that profile;
4. let normal service probes determine whether expected devices are actually responding; and
5. let the provider's operational open validate that the advertised/present hardware can actually run.

The temporary detection I2C bus is deleted before normal shared-I2C initialization. A provider probe never overrides the selected board profile: the profile decides what RFScout is allowed to touch, while the probe reports whether that legal hardware is present/healthy.

If no supported profile is detected, RFScout does not initialize normal board GPIO, display, SD, radio, input, audio, location, or power providers. The ESP32-S3 remains awake in an inert FreeRTOS delay loop so recovery flashing remains possible.

## Current supported profiles

### LILYGO T-Embed CC1101 Plus

The T-Embed profile is the hardware-validated baseline. Existing display geometry, rotary/button input, CC1101, nRF24, PDM microphone, speaker, SD, battery/charger telemetry, and power semantics are intentionally preserved by the universal-image conversion.

### LILYGO T-Deck Plus

The T-Deck profile has received hardware validation for runtime detection, the 320x240 ST7789 orientation/transform, the 320x170 managed app view plus 320x70 system region, trackball/center input, GT911 coordinate orientation, persistent touch calibration, GPS, SD mounting/logging, standard-I2S speaker output, and ES7210 Recorder/Audio Spectrum capture.

The early discriminator uses SDA GPIO18 / SCL GPIO8 and probes the GT911 at `0x5D` or `0x14`.

Current T-Deck providers include trackball/center input, calibrated GT911 touch, SD, GPS/NMEA, internal-temperature telemetry, voltage-only battery telemetry, standard-I2S speaker output, and ES7210 microphone capture. Battery percentage is estimated from the GPIO4 ADC and a profile-owned 2:1 divider; external-power/charging state is intentionally unavailable because RFScout has no authoritative digital source for it.

ES7210 presence is probed non-invasively at its legal shared-I2C address; this presence check does not initialize I2S, start the ADC, or alter GPIO0. Operational capture setup is deferred until Recorder or Audio Spectrum actually opens `audio_capture`.

T-Deck GPIO0 is normally reserved for trackball-center/BOOT input and is not treated as an RFScout power key. The ES7210 capture path conflicts with that physical center source while the codec is active, so the input service suspends the GPIO0 Select source before capture activation and restores it when the provider closes. Calibrated touch in the managed app view feeds the same semantic Select event only during that interval. The board exposes no RFScout sleep, deep-sleep, or physical power-off capability yet. Keyboard and SX1262 remain disabled even where reference pin metadata is known.

Extended SD/shared-SPI stress behavior, GPS fix-loss behavior, and broader cross-board stress/fault-injection coverage remain release-validation work. The ES7210 microphone path, four-slot TDM MIC1/slot-0 mapping, sustained Recorder/Spectrum capture, tap-to-stop handoff, and T-Embed audio regression have been validated on hardware.

## Input boundary

The `input` service exposes board-neutral semantic input:

- navigation with signed steps;
- select and long-select;
- back;
- character input for future keyboard-capable providers.

T-Embed uses the validated rotary/button provider. T-Deck normally uses trackball/center as its physical fallback. GT911 is a separate touch service: raw controller coordinates are transformed and calibrated into logical display coordinates. The system navigation layer consumes calibrated touches in the global tab region. Ordinary app-view touch is ignored; while another legal provider has suspended the physical primary Select source, a calibrated app-view press is translated into the existing semantic Select event. This keeps #79's GPIO0 workaround narrow and leaves full touch/keyboard application input for a separate abstraction.

Keyboard support is not yet advertised as a T-Deck capability even though its reference I2C address/IRQ metadata is retained in the profile. A future keyboard provider should feed the same semantic input service rather than bypassing application input policy.

## Display and view boundary

The display service owns the physical LCD and framebuffer. Runtime physical-display metadata comes from the selected board profile.

Applications receive one managed `rfscout_view_t` presentation surface through their app session. The current canonical app view is 320x170 on both supported profiles:

- on T-Embed it maps to the full physical 320x170 display;
- on T-Deck it occupies the first 320x170 pixels of the 320x240 display;
- the remaining T-Deck 320x70 region is system-owned and contains global application tabs plus touch/calibration status.

The view carries standardized session `init`/`deinit` traits so application transitions prepare and sanitize presentation state consistently. Applications should size app-owned drawing against their assigned view rather than physical panel dimensions. The presentation layer redraws system chrome after app rendering as a defensive compatibility measure, not as permission for apps to draw outside their view.

ST7789 initialization consumes profile-owned physical width/height, native geometry, swap-XY, mirror, inversion, panel-gap, pin, and backlight metadata. T-Embed uses PWM backlight control; T-Deck uses the panel's pulse-stepped backlight semantics.

## Touch calibration

Touch calibration is system/board state, not app configuration and not SD content. It is stored through the bounded internal persistent-record service under one system-owned record.

A missing or incompatible record enters startup calibration before touch becomes authoritative. Compatibility includes board identity and display/native geometry/transform metadata. Explicit recalibration replaces the same record atomically; a failed replacement does not erase the previously persisted record.

## Location and time

The T-Deck GPS provider exposes checksum-validated NMEA state through a board-neutral snapshot with independent location and UTC-time validity. Provider initialization is distinct from receiver detection: the UI does not advertise a GPS receiver until a supported checksum-valid sentence has been observed.

The `device_time` service owns wall-clock validity/source, Auto/Manual synchronization policy, and timezone conversion. In Auto mode a fix-valid GPS UTC timestamp may synchronize the system clock; GPS itself does not own timezone policy. Time-policy changes are persisted before the new mode is published to runtime state, so a failed policy write cannot silently create a different reboot behavior.

## Battery and power boundary

Battery telemetry and destructive power behavior are intentionally separate capabilities.

T-Embed uses its hardware fuel-gauge/charger path and can establish external-power state for protective policy. T-Deck currently exposes only filtered battery voltage and estimated state-of-charge from its ADC. RFScout does not infer charging or USB state from voltage, and automatic low-battery power action is therefore disabled on a voltage-only profile because destructive/protective action requires authoritative external-power telemetry.

Board profiles advertise semantic power actions only when RFScout is prepared to perform them safely. A schematic showing a possible sleep or shutdown path is not sufficient reason to expose the capability.

Before a terminal T-Embed sleep/power-off transition, RFScout persists dirty internal state and quiesces removable-storage users. Active Recorder recording/finalization vetoes the transition using the same semantic rule as app navigation. The SD volume itself remains mounted for the lifetime of the running firmware; terminal power states do not add an unmount/remount recovery state machine.

## Audio boundary

Speaker output and microphone capture are separate semantic capabilities. The validated T-Embed I2S speaker and PDM microphone paths remain intact.

The board-neutral `audio_capture` service owns capture-provider selection, profile-legality checking, provider probing, operational open/read/close, and capture-transport lifetime. Recorder and Audio Spectrum consume normalized PCM through this service and never receive an `i2s_chan_handle_t`.

T-Embed retains its existing PDM provider behind `audio_capture`; that provider owns I2S0/PDM setup, DMA, clock/data probing, reads, and teardown. The PDM probe is necessarily active because the microphone has no separate control-plane identity.

T-Deck standard-I2S speaker output is an enabled, hardware-validated provider behind the separate semantic audio-output service. T-Deck ES7210 capture is an enabled, hardware-validated provider behind `audio_capture`. The ES7210 provider uses the shared board I2C service for codec control and Espressif's maintained codec interface for register configuration, gain, ADC enable/disable, and close. RFScout alone owns I2S0/DMA: the codec runs as a four-slot Philips-format TDM slave with all ADC slots enabled for stable framing, while the ESP32-S3 supplies MCLK/BCLK/LRCK and the provider compacts MIC1/slot 0 to mono 16-bit PCM.

No other layer is allowed to reconfigure, enable, read, or close a provider's capture transport. This single-owner invariant prevents codec-control helpers and Recorder from competing over I2S state. The provider also owns the temporary GPIO0 Select conflict and restores the physical input source during every close/failure path.

Broader calibrated Audio Input generalization remains roadmap work and is not implied complete by adding this provider.

## Shared buses

SPI and I2C buses are initialized from the active runtime profile. Providers must respect profile capability checks before adding devices or configuring active peripheral control pins.

T-Deck display, SD, and SX1262 share the SPI bus. Known chip-select pins are driven inactive during platform setup, but SX1262 remains unadvertised until its provider and shared-bus behavior are implemented and hardware-validated.

The SD service owns its mount and raw-card arbitration for the running firmware lifetime. Filesystem users do not manage the raw `sdmmc_get_status()` synchronization contract; that remains private to the storage service and its logger implementation.

The shared board I2C service is used by ordinary post-detection peripheral providers, including ES7210 codec control; the temporary fingerprint bus is always torn down first.

## PSRAM requirement

Supported profiles require usable external PSRAM. Internal SRAM remains reserved for RTOS/system state, task stacks, DMA/peripheral-constrained buffers, and latency-sensitive allocations; RFScout does not promise a reduced-capacity internal-SRAM fallback.

## Adding another profile

A new compatible ESP32-S3 profile should:

1. add an immutable runtime profile in `services/board`;
2. add the smallest safe early fingerprint needed to distinguish it from existing profiles;
3. leave unknown hardware inert if the fingerprint is ambiguous or fails;
4. expose only hardware/service capabilities that RFScout can initialize safely;
5. keep applications dependent on semantic services and managed views rather than board identity;
6. add provider implementations behind the existing service boundaries where possible;
7. validate power, shared buses, and destructive/low-power behavior on real hardware before advertising those capabilities.

Do not reintroduce per-target Kconfig/CMake firmware images for this hardware family unless a future incompatibility makes one universal image technically unsafe or impractical.