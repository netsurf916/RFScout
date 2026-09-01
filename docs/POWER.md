# Power controls and protective policy

RFScout treats power transitions as board-neutral semantic actions. The active board advertises which actions and sensors it supports; Device Settings presents only combinations that are valid for that target.

This document is the canonical production reference for both the physical power button and automatic low-battery/high-temperature protection. Display dimming and LCD sleep are documented separately in [DISPLAY_IDLE.md](DISPLAY_IDLE.md).

## Power button

Device Settings includes a **Power button** submenu using direct center-to-cycle controls.

**Short press** supports, when available:

- `None`
- `Screen off`
- `Sleep`
- `Power off`

**Long press** supports:

- `None`
- `Power off`

Defaults preserve the original behavior: short press is `Sleep`; long press is `Power off`. Configuration is stored under `power.short_press` and `power.long_press` in `/sdcard/rfscout.json`.

### Gesture timing

The power button has three intentional regions:

1. release before the short-press window expires -> perform the configured short action;
2. once the long-hold countdown has started, release before the long threshold -> cancel with no action;
3. hold through the long threshold -> perform the configured long action; the eventual release is consumed.

A long action of `None` has no countdown. `Sleep` is intentionally not a long-hold choice because the physical button is also the deep-sleep wake source.

`Screen off` requests the normal display-idle off state through `display_idle_request_off()`; it does not stop applications, radios, logging, or notifications.

Display wake has priority over power gestures. If the LCD is dimmed or off, the entire gesture that wakes it is consumed. The user must release and begin a new gesture before a power action can occur.

## Protective power policy

Device Settings also includes a **Power policy** submenu.

- **Low battery**: `Off`, `5%`, `10%`, `15%`, `20%`; default `10%`.
- **Low batt action**: supported `None`, `Sleep`, `Power off`; default `Power off`.
- **High temp**: `Off`, `70 C`, `80 C`, `90 C`, `100 C`; default `90 C`.
- **High temp action**: supported `None`, `Sleep`, `Power off`; default `Power off`.

The canonical configuration keys are:

```json
"power": {
  "short_press": "sleep",
  "long_press": "power_off",
  "low_battery_percent": 10,
  "low_battery_action": "power_off",
  "high_temperature_c": 90,
  "high_temperature_action": "power_off"
}
```

`Screen off` is intentionally not a protective action because it leaves the active workload running.

### Low-battery behavior

Low-battery policy evaluates state of charge once per second. The threshold must be met for five consecutive **fresh successful fuel-gauge reads** before a transition is requested. Failed reads break the confirmation run instead of reusing cached percentage data.

A protective low-battery transition additionally requires **fresh charger telemetry confirming external power is absent**. Cached charger state is acceptable for display, but not for authorizing a protective transition. Connecting external power during the warning cancels the pending transition. After firing, low-battery protection rearms only after charge rises at least three percentage points above the configured threshold.

### High-temperature behavior

High-temperature policy uses the ESP32-S3 internal temperature sensor when the board advertises that capability. This is SoC/die temperature and is unrelated to the MLX90640 Thermal application.

The threshold must be met for five consecutive one-second samples. The condition rearms after temperature falls at least 5 C below the configured threshold. A confirmed high-temperature transition takes priority over a pending low-battery transition.

If the selected high-temperature action is `Power off`, RFScout refreshes charger telemetry first. BATFET disable cannot remove power from an externally powered board, so physical power-off is used only when fresh telemetry confirms external power is absent. If external power is present, unavailable, or cannot be freshly confirmed, RFScout falls back to `Sleep` when deep sleep is supported so active execution still stops.

## Transition overlay

Power-button and protective transitions share the same global presentation layer. Examples include:

```text
HOLD TO POWER OFF
```

```text
LOW BATTERY
POWER OFF IN 5
```

```text
HIGH TEMPERATURE
SLEEP IN 5
```

Protective warnings last five seconds. Ordinary user input does not cancel them; the owning policy may cancel when the underlying condition clears. Once the delay expires, the overlay changes to the executing state and the common semantic power-action path performs the transition.

## Board capability contract

Board definitions advertise support for screen-off, deep sleep, physical power-off, and optional policy inputs such as internal temperature. Gesture suitability remains separate from raw capability: for example, a board can support deep sleep while long-hold `Sleep` remains intentionally unavailable.

Power policy is device-level. Hardware-specific PMU, GPIO, charger, temperature-sensor, and wake-source mechanics remain behind board/platform services.