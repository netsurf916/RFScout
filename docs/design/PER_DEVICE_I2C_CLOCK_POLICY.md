# Per-device I2C clock policy (implementation and hardware acceptance)

**Status:** Implemented and merged on `main` in PR #146 before the planned **v0.4.0** candidate build. Device-specific clocks are part of the current firmware baseline, not deferred 0.5.0 work; electrical and optional-sensor hardware validation remain open.

## Ownership and clock contract

RFScout's T-Embed board uses one shared ESP-IDF I2C master bus (SDA GPIO8, SCL GPIO18). The driver creates a **separate device handle with its own `scl_speed_hz`** for each known peripheral. Device-specific speeds are configured at handle attachment; there is no runtime global 100-to-400-kHz bus switch.

| Device | Address | Configured transfer clock | Policy |
| --- | --- | --- | --- |
| BQ25896 charger | `0x6B` | **100 kHz** | Retain conservative early startup/power sequencing, charging watchdog and safe recovery |
| BQ27220 fuel gauge | `0x55` | **100 kHz** | Retain provision/unseal/configuration command ordering and retries; its data sheet imposes special command restrictions above 100 kHz |
| MLX90640 IR array (optional) | `0x33` | **400 kHz**, fall back to **100 kHz** | Fast-mode transfer handles for bulk sensor reads; downgrade after three consecutive failed frame-acquisition I2C calls |
| UART GPS | n/a | n/a | Current RFScout GNSS path uses UART, not I2C; do not create an I2C handle without a specific receiver driver |

`board_i2c_add_device_with_speed()` assigns handle-specific clock rates; `board_i2c_add_device()` remains a 100-kHz compatibility default. `board_i2c_remove_device()` only clears a handle on confirmed ESP-IDF removal, preventing accidental handle loss when a detach fails. Distinct clock configurations do not imply simultaneous transactions: the ESP-IDF master arbitrates the shared bus.

**Early discovery** is deliberately separate. The temporary board-fingerprint bus probes the charger/fuel-gauge identities before the runtime profile is selected; runtime `board_i2c_probe()` also uses ESP-IDF's `i2c_master_probe()`, which does not take an explicit per-device clock. Neither a fingerprint probe nor a generic unknown-device scan implies that every device can safely use 400 kHz. Power ICs retain 100-kHz *transfers* after attachment, including first-boot provisioning and all recovery paths.

### Optional Thermal fallback

1. Confirm the sensor acknowledges on the shared bus, then attach its own 400-kHz handle.
2. Read status, control and pixel words at the configured rate. Retain the original bounded transfer sizes (32 words/transaction; four per poll), shared timeout cooldown, and two-subpage frame assembly.
3. If three consecutive acquisition failures occur at 400 kHz, discard the incomplete subpage, **remove the old device handle**, then attach a **new 100-kHz handle** on the same bus. Keep the battery handles unchanged; reset refresh-control setup and retry normal acquisition after the shared cooldown.
4. If detach fails, do not discard its handle. If reattachment fails, report the Thermal sensor unavailable; future normal hot-plug probes may recover it at the 100-kHz fallback speed.
5. The downgrade is sticky for the boot session, avoiding repeated oscillation between rates. A normal complete frame resets the failure counter.

This is a reliability fallback, **not a measurement or proof that 400 kHz is physically safe** for the actual expansion cable/pull-ups, supply rails and board revision. The MLX90640 supports I2C Fast Mode Plus (up to 1 MHz), but the actual bus's rise time and integrity still require hardware testing. The system uses 400 kHz at most.

The BQ27220 data sheet specifies additional command restrictions above 100 kHz, supporting our choice to keep all gauge configuration writes at 100 kHz. The BQ25896 remains at the conservative baseline even if the silicon can support higher transfer clocks.

Primary references: [Melexis MLX90640 data sheet](https://www.melexis.com/en/documents/documentation/datasheets/datasheet-mlx90640), [TI BQ27220 data sheet](https://www.ti.com/product/BQ27220), [TI BQ25896 data sheet](https://www.ti.com/product/BQ25896).

## Implementation checklist

- [x] Audit currently registered I2C clients, addresses, startup sequence and transfer policy.
- [x] Assign explicit BQ25896/BQ27220 100-kHz handles for startup and future reattachment.
- [x] Assign an MLX90640 400-kHz handle with a sticky 100-kHz fallback on repeated transfer failure.
- [x] Preserve shared-bus 250-ms timeout cooldown and fail-closed handle removal.
- [x] Keep board fingerprint probes separate from per-device transaction speed.
- [x] Document owner boundaries and add host regression tests.
- [ ] Validate startup/power-management on real hardware with charger, battery and gauge present.
- [ ] Confirm SDA/SCL rise time, pull-up strength, cable length and supply voltage for 400 kHz with representative MLX90640 hardware.
- [ ] Verify MLX90640 identification, subpage acquisition, errors, hot-plug retries and fallback with battery telemetry active.
- [ ] Verify both orders of mixed-speed operations, transient NACK/timeout recovery, charging/power transitions, SD/SPI load and repeated app transitions.
- [ ] Verify fallback after removing a sensor, and verify no stale device handles or false battery-device failure when the sensor is reconnected.

**Acceptance note:** A passing firmware build and host tests verify integration and invariants, **not** electrical signal quality or successful camera-frame acquisition on a given external MLX90640.
