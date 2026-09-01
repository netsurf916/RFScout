# Power controls

RFScout treats power-button gestures as board-neutral semantic actions. The active board advertises which actions it can perform, and Device Settings only presents choices supported by that target.

## Power button settings

Device Settings includes a **Power button** submenu. The submenu uses the direct-cycle menu semantic: rotate to select a row and press the center control to cycle the value. There is no separate edit mode for these small enumerations.

**Short press** supports, when available on the board:

- `None`
- `Screen off`
- `Sleep`

**Long press** supports, when available on the board:

- `None`
- `Power off`

The compatibility defaults preserve the original RFScout behavior: short press is `Sleep` and long press is `Power off`. The values are persisted in `/sdcard/rfscout.json` under the `power` object as `short_press` and `long_press`.

## Gesture timing

The power button deliberately has three timing regions:

1. A press released before the short-press window expires performs the configured short action.
2. Once the long-press countdown has started, releasing before the long threshold cancels the gesture and performs no action.
3. Holding through the long threshold performs the configured long action. The eventual release is consumed and cannot also produce a short action.

A configured long action of `None` has no countdown because there is no pending transition to communicate.

## Countdown presentation

The global power-transition overlay is parameterized rather than hardcoded to shutdown. For the power button, a configured long hold shows `HOLD TO POWER OFF`, followed by `POWERING OFF...` once the threshold is reached. Sleep remains a semantic power action for short press and other device policies, but it is intentionally not exposed as a long-hold action because the same physical button is also the deep-sleep wake source.

The overlay is applied by the shared presentation layer and does not become permanent application framebuffer content.

## Display wake priority

Display wake has priority over all configured power-button behavior. If the display is dimmed or off, the physical gesture that wakes it is consumed in its entirety. Holding that same gesture cannot subsequently begin a power countdown or perform a short action; the user must release and start a new gesture after the display is awake.

`Screen off` requests the normal display-idle off state through `display_idle_request_off()`. The request is serviced by the display-idle owner after the triggering physical activity has cleared, so the release that requested screen-off cannot immediately wake the panel again. Inactivity-driven and explicit screen-off therefore share the same LCD state, framebuffer behavior, and wake restoration path.

## Board capabilities

Board definitions advertise support for screen-off, deep sleep, and physical power-off. Gesture suitability is intentionally separate from board capability: a board may support deep sleep while the long-hold menu still excludes `Sleep`. The menu cycles only through actions both supported by the board and valid for that gesture. Configuration values remain semantic rather than naming PMU, GPIO, or SoC-specific mechanisms.
