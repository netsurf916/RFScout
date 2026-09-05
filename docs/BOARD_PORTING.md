# Board targets and porting

RFScout uses one source tree to produce board-specific firmware images. Shared application/service logic should not require every image to contain every board driver.

## Current status

The only supported target is currently **LILYGO T-Embed CC1101 Plus**. `RFSCOUT_BOARD` selects the target; board definitions live under `main/boards/<target>/` and CMake includes only the selected board directory.

## Board contract

A board target owns physical facts and capabilities, including:

- GPIO assignments and shared-bus wiring;
- display geometry and peripheral addresses;
- `RFSCOUT_BOARD_NAME`;
- compile-time `RFSCOUT_BOARD_HAS_*`/`RFSCOUT_BOARD_CAN_*` capability constants;
- power/display actions and policy sensors available on that target;
- PSRAM/flash assumptions needed by the platform.

Application code should depend on services/capabilities, not board model names. Board-specific conditionals belong in board/service/driver integration code.

### PSRAM requirement

RFScout's current platform model requires usable external PSRAM. Large persistent platform allocations such as the framebuffer already depend on it, and v0.4.0 planning intentionally treats PSRAM as a supported-target requirement rather than promising a reduced-capacity internal-SRAM fallback.

Internal SRAM remains available for RTOS/system state, task stacks, DMA/peripheral-constrained buffers, and latency-sensitive allocations. A future board target should not be considered supported unless its configured memory topology satisfies RFScout's platform requirements.

## Input boundary

The `input` service exposes board-neutral semantic input:

- navigation with signed steps;
- select and long-select;
- back;
- character input.

The current T-Embed provider adapts its rotary encoder and buttons into this interface. Applications receive semantic input through the active-app runtime; they should not depend on encoder GPIOs or a particular control layout. Unhandled navigation/Back may fall through to global policy.

Physical activity used for display wake remains distinct from dispatchable semantic input. A complete gesture consumed to wake the display is discarded before app, lock, or power semantics can observe it. Accepted RFScout state changes use the separate shared event dispatcher; raw physical activity is not broadcast merely because it occurred.

A future target may compose multiple compatible input providers, such as keyboard plus trackball/buttons, without changing the application input contract.

## Display boundary

The `display` service owns framebuffer/presentation behavior and exposes runtime provider metadata through `display_info()`, including selected provider, geometry, and supported capabilities. The current T-Embed image selects the ST7789 provider and retains its existing RGB565 framebuffer implementation.

Board builds still provide compile-time geometry for legacy/static layout code. New adaptive layout code should prefer runtime display metadata so provider selection can evolve without exposing controller identity to applications. Controller-specific low-power/output behavior stays behind the display service.

## Power boundary

Board targets advertise semantic power capabilities such as screen-off, deep sleep, and physical power-off. Device policy chooses among those semantic actions; target code owns PMU/GPIO/wake-source mechanics.

Optional protective inputs are capabilities too. The current target advertises ESP32 internal-temperature support for high-temperature policy. Battery/fuel-gauge/charger availability remains a hardware/service concern rather than being assumed by application code.

A board may support an action without every gesture exposing it. For example, deep sleep is supported by the current board, but long-hold `Sleep` is intentionally excluded because the physical power button is also the deep-sleep wake source.

## Audio boundary

Speaker and microphone behavior should be exposed through services/capabilities rather than board checks in application code. A target without speaker output can retain semantic event behavior while making audible presentation unavailable. Application content audio should not imply that every board uses the current T-Embed I2S topology.

## Hardware availability versus enablement

Compile-time capability means the target can support a device; it does not guarantee every product variant populates it. Fixed optional devices such as CC1101, nRF24, and the board microphone may be probed once at startup. Hot-plug peripherals such as external I2C devices should use dynamic probing.

Probe state is retained centrally. The first known result is recorded in the RAM-only System Events history without interactive notification; later availability/error records are emitted only when the retained state changes. Periodic unchanged probes must not produce ring-buffer noise.

## Adding a board

A new board port should:

1. add a Kconfig board choice and `main/boards/<target>/board.h`;
2. provide target-specific defaults/source selection;
3. satisfy platform prerequisites including display, input, power, storage, and usable PSRAM;
4. expose only capabilities actually implemented;
5. keep applications dependent on semantic services/capabilities;
6. add a distinct CI/package target and hardware identity.

Do not add runtime board detection or compile unrelated board implementations into every image unless a future hardware family creates a concrete need. Provider selection should remain constrained to implementations plausible for the selected board image.

## Configuration overlays

The repository-level `sdkconfig.defaults` remains active while only one target exists. With multiple targets, board-specific defaults should move to selected overlays so flash, PSRAM, partitions, and peripheral configuration can differ without leaking one board's assumptions into another image.