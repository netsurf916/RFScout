# Board profiles and porting

RFScout uses one ESP32-S3 firmware image for the supported LILYGO T-Embed CC1101 Plus and T-Deck Plus hardware family. A minimal hardware fingerprint runs before ordinary board GPIO or peripheral initialization, selects one immutable runtime profile, and then normal services probe only hardware that is legal for that profile.

## Runtime profile model

Board definitions live in `main/services/board/board.{h,c}`. The selected profile owns physical facts and expected capabilities, including:

- GPIO assignments and shared-bus wiring;
- display geometry, transform metadata, and panel offsets;
- peripheral addresses and interrupt pins;
- storage, radio, audio, input, and navigation expectations;
- semantic power capabilities;
- the board/profile name and ID.

Applications must not branch on board IDs or names. They consume semantic services, availability, input events, and the managed application view.

## Detection versus capability probing

Board-profile detection and ordinary device probing are intentionally separate stages:

1. perform one minimal, passive I2C fingerprint using only the candidate profile wiring;
2. select exactly one immutable runtime board profile;
3. initialize only buses and pins legal for that profile;
4. let normal service probes determine whether expected devices are actually responding.

The temporary detection I2C bus is deleted before normal shared-I2C initialization.

If no supported profile is detected, RFScout must not initialize normal board GPIO, display, SD, radio, input, audio, or power providers. The ESP32-S3 remains awake in an inert FreeRTOS delay loop so recovery flashing remains possible.

## Current supported profiles

### LILYGO T-Embed CC1101 Plus

The T-Embed profile is the hardware-validated baseline. Existing display geometry, rotary/button input, CC1101, nRF24, PDM microphone, speaker, SD, and power semantics should be preserved unless a hardware-verified change requires otherwise.

### LILYGO T-Deck Plus

The T-Deck profile is currently reference-derived and has not yet been hardware validated. New provider capabilities should be enabled conservatively and only when RFScout is prepared to initialize those paths safely.

The early discriminator uses SDA GPIO18 / SCL GPIO8 and probes the GT911 at `0x5D` or `0x14`.

The current bring-up profile exposes the known-safe architectural expectations needed for development, but intentionally withholds unvalidated active power and speaker capabilities. GPIO0 is presently reserved for the trackball/center input path rather than also acting as a semantic power key. T-Deck sleep/power behavior and speaker output should be enabled only after hardware validation.

## Input boundary

The `input` service exposes board-neutral semantic input:

- navigation with signed steps;
- select and long-select;
- back;
- character input;
- future calibrated pointer/touch events.

T-Embed uses the validated rotary/button provider. T-Deck currently uses a basic trackball/center provider as a non-touch fallback. GT911 touch and keyboard input should be added as semantic providers without exposing T-Deck-specific logic to applications.

## Display and view boundary

The display service owns the physical LCD and framebuffer. Runtime display metadata comes from the selected board profile.

Applications receive one managed `rfscout_view_t` presentation surface through their app session. The current canonical app view is 320x170 on both supported profiles:

- on T-Embed it maps to the full physical 320x170 display;
- on T-Deck it occupies the first 320x170 pixels of the 320x240 display;
- the remaining T-Deck 320x70 region is system-owned and reserved for future tabs/navigation chrome.

The view carries standardized session `init`/`deinit` traits so application transitions can prepare and sanitize presentation state consistently. Applications should use their assigned view rather than physical display dimensions.

## Power boundary

Board profiles advertise semantic power capabilities only when RFScout is prepared to perform them safely on that profile. Device policy chooses among those semantic actions; provider code owns PMU/GPIO/wake-source mechanics.

A reference schematic showing a possible sleep or power path is not sufficient reason to advertise the capability. T-Deck power behavior remains disabled during initial bring-up until verified on hardware.

## Audio boundary

Speaker and microphone behavior are semantic services rather than application-visible board features. The validated T-Embed I2S speaker and PDM microphone paths must remain intact.

T-Deck ES7210 microphone and speaker support should be introduced as separate providers behind the same semantic services. Reference-derived pins may be retained as implementation notes, but an unvalidated provider must not be initialized simply because the reference hardware appears to contain it.

## Shared buses

SPI and I2C buses are initialized from the active runtime profile. Providers must respect profile capability checks before adding devices or configuring chip-select/control pins.

T-Deck display, SD, and SX1262 share the SPI bus. Chip-select behavior must be validated before the SX1262 provider is enabled.

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
