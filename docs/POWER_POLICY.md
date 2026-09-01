# Protective power policy

RFScout can request protective power-state changes when battery reserve is low or the ESP32 internal temperature remains high. These conditions use the same board-neutral power actions and shared transition overlay as the configurable power button.

## Device settings

Device Settings exposes a **Power policy** submenu using the direct-cycle interaction model.

- **Low battery**: `Off`, `5%`, `10%`, `15%`, or `20%`. Default: `10%`.
- **Low batt action**: board-supported `None`, `Sleep`, or `Power off`. Default: `Power off`.
- **High temp**: `Off`, `70 C`, `80 C`, `90 C`, or `100 C`. Default: `90 C`.
- **High temp action**: board-supported `None`, `Sleep`, or `Power off`. Default: `Power off`.

The policy values are stored in the existing `power` object in `/sdcard/rfscout.json` as `low_battery_percent`, `low_battery_action`, `high_temperature_c`, and `high_temperature_action`.

`Screen off` is intentionally not a protective action: it leaves applications and radios running and therefore does not materially reduce the device's operating state in the way sleep or power-off does.

## Low-battery behavior

Low-battery protection evaluates the fuel-gauge state of charge once per second. The configured threshold must be met for five consecutive samples before a transition is requested, avoiding reactions to a single transient sample.

Low-battery protection is inhibited while external power is present or the battery is charging/complete. Connecting external power during the warning countdown clears the condition and cancels the pending transition. After a low-battery condition has fired, it rearms only after the battery rises at least three percentage points above the configured threshold.

## High-temperature behavior

High-temperature protection uses the ESP32-S3 internal temperature sensor when the board target advertises that capability. This is the SoC's internal/die temperature and is deliberately separate from RFScout's MLX90640 Thermal application.

The configured temperature must be met for five consecutive one-second samples before a transition is requested. The condition rearms after the internal temperature falls at least 5 C below the configured threshold.

A confirmed high-temperature transition takes priority over a pending low-battery transition because it represents an actively worsening thermal condition.

## Transition warning

Protective transitions use the same global overlay implementation as the power-button hold countdown, but provide a reason and target action. For example:

```text
LOW BATTERY
POWER OFF IN 5
```

and, with the default high-temperature action:

```text
HIGH TEMPERATURE
POWER OFF IN 5
```

Both protective conditions default to `Power off`. `Sleep` remains configurable, but it is not the default because RFScout does not yet continue evaluating battery or temperature policy while in deep sleep. The warning lasts five seconds. It is not cancelled by ordinary user input. The policy that raised it may cancel it when the underlying condition clears, such as external power being connected during a low-battery warning. Once the delay expires, the overlay changes to the executing state (`SLEEPING...` or `POWERING OFF...`) and the normal semantic power-action path performs the transition.

## Board abstraction

Threshold policy is device-level, while action availability and sensor availability are board capabilities. A board does not expose a semantic action it cannot perform, and high-temperature monitoring is inactive on targets that do not advertise an internal temperature source.
