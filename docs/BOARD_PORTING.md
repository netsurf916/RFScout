# Board profiles and porting

RFScout currently supports one hardware profile: **LILYGO T-Embed CC1101 Plus**. The codebase intentionally keeps runtime board/profile and semantic capability boundaries even with a single supported board so future hardware can be added without teaching applications about board identity.

## Runtime profile model

Board definitions live in `main/services/board/board.{h,c}`. The selected profile owns physical facts and expected capabilities, including GPIO assignments, buses, display geometry, peripheral addresses, control pins, and semantic provider capabilities. Applications depend on services and capabilities, not board names or IDs.

## Detection versus capability probing

Board-profile detection and ordinary provider probing remain separate stages:

1. perform the smallest passive hardware fingerprint needed to recognize a supported profile;
2. select one immutable runtime profile;
3. initialize only buses and pins legal for that profile; and
4. let ordinary service probes determine whether expected devices are actually responding.

The temporary detection I2C bus is deleted before normal shared-I2C initialization. A provider probe never overrides the selected profile.

If the T-Embed fingerprint is not found, RFScout leaves ordinary board GPIO/peripherals untouched and remains awake in an inert FreeRTOS delay loop for recovery flashing.

## Current supported profile

### LILYGO T-Embed CC1101 Plus

The hardware-validated T-Embed profile uses SDA GPIO8 / SCL GPIO18 for early detection and probes the onboard BQ25896/BQ27220 addresses. The profile owns the validated 320x170 ST7789 display, rotary/center input, CC1101, nRF24, PDM microphone, speaker, SD, battery/charger telemetry, internal-temperature protection, display low power, deep sleep, and physical power-off semantics.

## Input boundary

The input service exposes board-neutral semantic navigation, Select, long-Select, Back, and future character input. The current provider adapts the T-Embed rotary encoder and buttons. Applications do not consume GPIOs directly.

## Display and view boundary

The display service owns the physical LCD, framebuffer, brightness/low-power behavior, and runtime geometry. Applications receive a managed `rfscout_view_t`; the current T-Embed view is the full 320x170 display.

## Location and time

Location providers remain semantic/capability-gated. The current T-Embed profile does not advertise onboard GPS, while `device_time` remains independent and supports manual/retained wall-clock behavior.

## Battery and power boundary

The current T-Embed profile uses the BQ27220 fuel gauge and BQ25896 charger and can provide authoritative external-power state. Power actions are semantic capabilities; policy selects an action and provider code owns GPIO/PMU/sleep mechanics.

## Audio boundary

Audio capture and output remain board-neutral services. The current capture provider is the T-Embed PDM microphone on I2S0; speaker output uses the validated T-Embed path on I2S1. Applications do not own raw audio peripheral handles.

## Shared buses

T-Embed LCD, microSD, CC1101, and nRF24 share SPI. Battery/power devices and compatible external sensors share I2C. Providers must respect the selected profile before configuring chip-select, interrupt, or control pins.

## PSRAM requirement

The supported profile requires usable external PSRAM. Internal SRAM remains reserved for RTOS/system state, task stacks, DMA/peripheral-constrained buffers, and latency-sensitive allocations.

## Adding another profile

A future compatible profile should:

1. add an immutable profile in `services/board`;
2. add a distinct, minimally invasive early fingerprint;
3. leave unknown or ambiguous hardware inert;
4. expose only hardware/service capabilities RFScout can initialize safely;
5. keep applications dependent on semantic services and managed views rather than board identity;
6. add new provider implementations behind existing service boundaries where possible; and
7. validate power, shared buses, display transforms, and destructive/low-power behavior on real hardware before advertising those capabilities.

Do not add a second profile merely because hardware is electrically similar. A profile should be carried only when RFScout has a meaningful hardware use case and enough validation to justify its maintenance cost.
