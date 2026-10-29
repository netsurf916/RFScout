# Per-device I2C clock policy (design and implementation tracker)

**Status:** Draft PR, not implemented. This is post-v0.4.0 work. Do not treat a passing build as evidence that individual devices were exercised.

## Motivation and current baseline

The shared bus is implemented by `main/services/board/board_i2c.c` using the ESP-IDF master bus API. `board_i2c_add_device_with_speed(address, hz, &handle)` already exists, but its default wrapper `board_i2c_add_device()` registers a device at **100 kHz**. As of this draft, `main/services/power/battery.c` attaches the BQ25896 charger and BQ27220 fuel gauge with that default wrapper, as does `main/services/thermal/mlx90640.c` for the optional MLX90640. Device probing uses `i2c_master_probe()` before a device handle is registered. GPS is currently handled through its UART-facing service; do not assume that every GNSS module is an I2C bus client.

The goal is not a global bus-frequency switch at the end of startup. A shared bus can host heterogeneous device speeds, and each real device should use a frequency it supports. Early power/control operations must remain safe without reducing the normal operating speed of unrelated devices.

## Proposed scope (subject to datasheet and hardware verification)

- Inventory each existing I2C bus client, with its physical address, required power/reset sequence, documented frequency capability, timing constraints, and whether it operates during first-stage board/power setup.
- Keep conservative startup/probing interactions for charger/fuel gauge as appropriate; do not require a generic discovery scan to be permanently limited to 100 kHz.
- Use existing **per-device** ESP-IDF handles and `board_i2c_add_device_with_speed()` rather than changing every device to one global 400 kHz mode. Consider faster operation for MLX90640 where its datasheet, physical bus and real measurements permit; choose an explicit verified speed for each future I2C GNSS receiver if one is added.
- Distinguish initial probe timing from configured *transfer* speed, as `i2c_master_probe()` currently accepts no per-device speed argument. Avoid claiming arbitrary unknown devices can be detected at an individually negotiated speed before identification.
- Retain current shared-bus timeout accounting, cooldown and safe detach/retry behavior. Avoid races or handle leaks when optional devices appear/disappear.
- Keep configuration in the device owner or board-profile layer where practical, not in unrelated application menus. The framework should provide a clear default and allow specific clients to override it.

## Implementation and acceptance checklist

- [ ] Confirm component limits, bus rise time/pull-ups, and board revision under actual power-rail configurations.
- [ ] Make the minimal client changes necessary for explicit per-device clock rates; avoid introducing a global bus switch.
- [ ] Retain reliable first-boot BQ25896/BQ27220 discovery, writes and recovery at conservative timing.
- [ ] Verify MLX90640 identification, full-frame reads, error recovery and coexistence with battery telemetry at the selected device speed.
- [ ] Test mixed-speed transactions interleaved in both orders, including NACKs, timeout/cooldown, SD/SPI activity and app transitions.
- [ ] Keep GPIO/I2C detection assumptions and measured hardware results documented. If an optional peripheral does not support the chosen speed, fall back deliberately and report it.
- [ ] Add appropriate host/integration tests and update the implementation contracts when the design is accepted.

**Release constraint:** This branch is independent of the v0.4.0 release workflow and remains Draft until the owner explicitly changes its review status.
