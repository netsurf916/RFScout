# Display idle behavior

RFScout treats display idling as a device-wide presentation policy, not an application lifecycle or power-state transition. Dimming or blanking the LCD does not stop the active application, radio acquisition, Recorder activity, logging, notifications, or other background work.

## States and defaults

The global display-idle controller has three states:

- **Active** — the configured display brightness is used and normal framebuffer presentation continues.
- **Dimmed** — the backlight is reduced to 10% while normal framebuffer presentation continues.
- **Off** — the backlight is disabled and physical framebuffer transfers to LCD GRAM are deferred.

Compiled defaults are:

- **Dim after:** 2 minutes of no physical user input.
- **Screen off after:** 10 minutes of no physical user input.

Device Settings exposes these presets:

| Setting | Values |
| --- | --- |
| Dim after | Off, 30 sec, 1 min, 2 min, 5 min |
| Screen off after | Off, 1 min, 2 min, 5 min, 10 min, 30 min |

Both values are ordinary `display` preferences in `/sdcard/rfscout.json`. Missing or invalid values retain the compiled defaults and are emitted on the next canonical configuration save.

## Wake and input consumption

Any physical user input wakes a dimmed or blank display. On the current T-Embed target this includes:

- an encoder detent;
- a center-button press; or
- the user/power key.

The complete wake gesture is consumed before semantic input handling. Waking the display therefore does **not** also:

- switch applications or move a menu selection;
- activate the highlighted item;
- generate Back or long-select behavior;
- toggle the global input lock;
- enter deep sleep; or
- start the hold-to-power-off countdown.

The input lock is intentionally downstream of display wake detection. Physical input still wakes the display while RFScout is locked; subsequent input remains subject to the normal lock policy once the display is awake.

Audio notifications do not wake the display. Tone/Morse alerts and explicitly requested application audio continue according to their normal application, notification, and theme policies while the screen is dimmed or off.

## Framebuffer and LCD ownership

Applications are not told that the display is blank. They continue updating the normal logical framebuffer and calling the shared presentation API.

While the display is **Off**, the presentation layer suppresses transfers to LCD GRAM and deliberately leaves the composed framebuffer dirty. This avoids spending SPI/DMA time repeatedly updating an invisible panel while preserving the most recent application view.

On wake from **Off**:

1. physical input is consumed;
2. LCD presentation is re-enabled while the backlight remains off;
3. the latest logical framebuffer is transferred once;
4. the configured user brightness is restored.

This order avoids briefly showing stale LCD contents when the backlight comes back on.

Dimming affects only backlight PWM. LCD transfers continue normally in the Dimmed state so the visible low-brightness display remains live.

## Relationship to system sleep and power off

Display Off is not ESP32 deep sleep and is not physical power off. RFScout's existing user-key behaviors remain separate:

- a normal short user-key press while the display is already awake may enter deep sleep;
- a normal long hold while the display is already awake may request physical power off;
- when that same key is the gesture that wakes a dimmed/off display, the entire press is consumed through release and neither action occurs.

Applications should not branch on display-idle state. Any future board-specific display backend should preserve the same semantic contract even if the hardware-specific way of dimming or suppressing panel updates differs.
