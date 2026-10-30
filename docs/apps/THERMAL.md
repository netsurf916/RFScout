# Thermal

Thermal is RFScout's optional MLX90640 relative-IR imaging application. Shared I2C, profile, lifecycle and rendering rules live in [Platform architecture](../ARCHITECTURE.md); future calibration, history and palette enhancements belong in [Roadmap](../ROADMAP.md).

## Availability and lifecycle

The optional MLX90640 is detected through hot-plug probing at I2C address `0x33`. Thermal has **no SD-card dependency** or separate acquisition worker. Active application polling acquires sensor data incrementally, while the `RENDER` callback composes only cached data. Input and settings changes request immediate frames; newly completed sensor frames request coalesced background frames capped by the shared framework at 5 Hz.

A shared-bus timeout cooldown or incomplete sensor subpage is not itself hardware removal. Persistent sensor failure can mark Thermal unavailable and return to Home through ordinary lifecycle handling. The app logger writes only when removable SD storage is mounted.

## Image and controls

- Native sensor resolution is **32 × 24**, presented in theme-framed image and telemetry panels with an Inferno-like relative palette.
- The UI shows frame-relative minimum, maximum and center values under **RELATIVE IR** / **CAL PENDING** labels. These readings are **not calibrated temperatures**.
- A small local spatial filter reduces raw per-pixel contrast without adding sensor traffic or claiming higher sensor resolution; no temporal averaging is applied to the sensor stream.
- The view displays `WAITING FOR FRAME` until both complementary subpages have been combined.
- Select opens **Back**, **App lock**, and **Keep screen awake**. The awake override defaults On while Thermal is foreground and is released on exit. Physical power controls retain priority.

## Incremental acquisition and I2C clocks

The MLX90640 attaches an explicit **400-kHz per-device I2C handle**. The BQ27220 fuel gauge and BQ25896 charger retain separate **100-kHz** handles; RFScout does **not** change the entire bus's speed. Board/device probing is separate from the handle's transfer rate.

On initial acquisition, the sensor's refresh-control register is set to **8 Hz alternating subpages**, preserving other control bits. The app checks for acquisition work at a **1-ms minimum interval**, and each active call transfers up to **four 32-word I2C blocks**, taking at least six calls to read the raw 768-word subpage. A frame becomes valid only after both subpages are captured. Actual delivered frame rate depends on sensor conversion, bus availability and the independent 5-Hz background presentation cap.

The shared I2C service imposes a **250-ms cooldown** after a hardware timeout. After **three consecutive failed sensor acquisitions at 400 kHz**, Thermal discards partial data, attempts to remove its existing handle, and reattaches at a **sticky 100 kHz** for the remainder of this boot. Refresh setup is repeated after downgrade. The old handle is retained if removal fails. If fallback cannot recover the sensor, it may be marked unavailable and probed again through the normal hot-plug path. Mixed-speed operation, cable/pull-up integrity and recovery remain hardware acceptance items.

See the [per-device I2C clock policy](../design/PER_DEVICE_I2C_CLOCK_POLICY.md).

## Current limitations and v0.4.0 acceptance

MLX90640 EEPROM calibration, absolute-temperature conversion, invalid-pixel compensation, user-lockable display range and retained image history are not implemented. Raw IR data and local spatial filtering must never be presented as absolute degrees Celsius.

Before v0.4.0 publication, verify sensor-present and sensor-absent navigation, SD-absent operation, incremental frame updates, lock and keep-awake behavior, app transitions, battery telemetry coexistence and fallback/hot-plug handling on representative physical hardware. Passing CI does not establish optional sensor/electrical validation.
