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

Supported targets require usable external PSRAM. Internal SRAM remains reserved for RTOS/system state, task stacks, DMA/peripheral-constrained buffers, and latency-sensitive allocations; RFScout does not promise a reduced-capacity internal-SRAM fallback.

## Input boundary

The `input` service exposes board-neutral semantic input:

- navigation with signed steps;
- select and long-select;
- back;
- character input.

The current T-Embed provider adapts its rotary encoder and buttons to this interface. Applications receive semantic input through the active-app runtime and do not depend on GPIOs or physical control layout. Unhandled navigation/Back may fall through to global policy.

Display-wake activity is separate from dispatchable input: a gesture used to wake the display is consumed before app, lock, or power handling. Future targets may compose compatible providers without changing the application contract.

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

Probe state is retained centrally as **available** or **unavailable**. The first known result is recorded silently in System Events; later records are emitted only when availability changes. Detailed probe failures stay in diagnostic logs rather than becoming a third availability state.

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