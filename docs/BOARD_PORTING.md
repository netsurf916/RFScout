# Board targets and porting

RFScout is moving toward one source tree that produces board-specific firmware images. The goal is to share application and service logic without turning each image into a universal firmware containing every supported board driver.

## Current status

The only supported target remains **LILYGO T-Embed CC1101 Plus**. The architecture does not yet claim support for another board.

The target is selected through the `RFSCOUT_BOARD` Kconfig choice. Board definitions live under `main/boards/<target>/`; CMake adds only the selected board directory to the component include path. Existing source continues to include `board.h`, but that name resolves to the selected target rather than a repository-global pin map.

## Board contract

A board target owns physical facts such as:

- GPIO assignments and shared-bus wiring;
- display geometry and board-specific peripheral addresses;
- a human-readable `RFSCOUT_BOARD_NAME`;
- compile-time `RFSCOUT_BOARD_HAS_*` capability constants.

Capabilities are compile-time constants by design. As drivers are separated into target-specific source sets, CMake should use the selected board and its capabilities to avoid compiling unsupported hardware into that firmware image.

Application code should not test board model names. It should depend on a service or capability. Board-specific conditionals belong in board/platform/driver integration code.

## Input boundary

`platform_input_get()` is the board-neutral input contract. It exposes semantic input rather than physical controls:

- navigation with a signed step value;
- select and long-select;
- back;
- character input.

The T-Embed implementation adapts its rotary encoder and center button into this interface. The application manager now consumes this board-neutral path directly. Historical encoder-specific helpers may remain in platform code while lower-level cleanup continues, but new application code should not depend on them.

Application acceptance is a separate layer from board input. After a menu/application has interpreted input and committed an action or state change, it can emit an RFScout semantic event through `event.c/.h`; audible feedback is resolved from those accepted events rather than from raw controls.

A future board may map a trackball, directional keys, keyboard, touch gestures, or another control scheme into the same input vocabulary without teaching application code about that hardware.

## Audio boundary

Board support should expose speaker/audio capability through the platform/service layer rather than board checks in application code. `audio_output` owns asynchronous speaker execution; semantic feedback and notifications sit above it. A board without a speaker should be able to keep application/event behavior intact while making audible output unavailable.

Likewise, application content audio is a capability/service concern rather than evidence that every board must implement the current T-Embed I2S topology.

## Adding a board

A future board port should be introduced in stages:

1. add a new Kconfig board choice and `main/boards/<target>/board.h`;
2. select only that board's include path and board-specific sources in CMake;
3. provide platform input, display, power, storage, and shared-bus integration needed for basic UI bring-up;
4. enable hardware-backed capabilities only as their drivers are functional;
5. expose applications according to capabilities rather than board identity;
6. add the target to CI as a separate build artifact.

Do not add a runtime board detector or compile unrelated board implementations into every image unless a future hardware family creates a concrete need for that behavior.

## Configuration overlays

The repository-level `sdkconfig.defaults` remains the active default configuration while only one target exists. When a second target is added, board-specific defaults should move to target overlays selected by the build wrapper/CI so flash, PSRAM, partitioning, and peripheral configuration can legitimately differ without accumulating one board's assumptions in another board's image.
