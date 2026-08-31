# Display idle behavior

RFScout treats display power management as a global presentation concern rather than an application lifecycle event. Applications, radio acquisition, logging, Recorder state, and audible notifications continue running normally while the LCD is dimmed or off.

## Device settings

Device Settings exposes two persistent inactivity controls:

- **Dim after**: `Off`, `30s`, `1m`, `2m`, or `5m`. The default is `2m`.
- **Screen off**: `Off`, `1m`, `2m`, `5m`, `10m`, or `30m`. The default is `5m`.

Both values are measured from the most recent physical user activity. The screen-off value is therefore a total idle time, not an additional delay after dimming.

When both are enabled, the screen-off timeout must be later than the dim timeout. If increasing the dim timeout would overlap the configured screen-off timeout, RFScout advances the screen-off timeout to the next valid value.

The values are stored in the `display` section of `/sdcard/rfscout.json` as `dim_timeout_seconds` and `off_timeout_seconds`. Zero disables the corresponding stage.

## Dimmed state

After the dim timeout, RFScout reduces the LCD backlight to approximately one quarter of the configured brightness, with a minimum nonzero PWM level. No application state changes and framebuffer rendering continues normally.

## Screen-off state

After the screen-off timeout:

- the backlight is set to zero;
- ST7789 display output is disabled;
- the ST7789 enters its hardware sleep-in state, stopping panel scanning/oscillator activity while retaining display memory;
- applications continue rendering into RFScout's normal framebuffer;
- presentation calls do not transfer the framebuffer over SPI while LCD output is disabled;
- application acquisition, logging, and Tone/Morse notifications continue normally.

The screen-off state is intentionally not exposed to individual applications. Audible alerts do not wake or otherwise reset the display-idle state; only physical user activity does.

## Wake behavior

Any physical user input wakes a dimmed or off display. The wake gesture itself is consumed completely and is not delivered as ordinary input.

This means the first wake gesture does **not**:

- rotate to another application;
- move or activate a menu item;
- toggle the global input lock;
- enter system deep sleep; or
- begin the hold-to-power-off countdown.

The global input lock does not block display wake detection. A locked device still wakes from encoder movement, the center button, or the user/power button; ordinary lock rules apply again only to subsequent input after the display is awake.

When waking from screen-off, RFScout sends ST7789 sleep-out and waits for the panel driver's required settling interval, re-enables display output while the backlight remains dark, presents the latest framebuffer, and only then restores the configured brightness. This avoids briefly showing stale LCD contents from before the timeout.

## Relationship to system sleep

Automatic screen-off is not ESP32 deep sleep and does not use the physical power-off path. Applications therefore continue running while only the LCD is asleep.

The explicit user-button deep-sleep and hold-to-power-off paths use the same LCD low-power primitive before the ESP32 sleeps or the PMU is asked to remove system power: the backlight is darkened, display output is disabled, and the ST7789 is placed into hardware sleep. If a physical power-off request fails, RFScout wakes and refreshes the panel before restoring the configured backlight brightness.
