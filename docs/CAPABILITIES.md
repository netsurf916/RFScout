# RFScout capability roadmap

## Core goal

RFScout is a **situational-awareness** instrument: build a useful, glanceable picture of nearby electronic devices and noteworthy RF activity without requiring the user to stare at raw packets or spectrum traces.

## Nearby / Brand awareness

Derived from Brandalize:

- passive Wi-Fi and BLE observations;
- global deduplication by device address where that identity is meaningful;
- rolling history of at least 1,000 recently observed unique devices;
- OUI/vendor attribution for globally administered addresses;
- brand counts and ranked brand view;
- RSSI, channel/source, first-seen and last-seen metadata;
- avoid assigning an OUI brand to locally administered/randomized addresses.

## Surveillance awareness

Derived from OUI-SPY:

- identify narrow, high-confidence surveillance signatures;
- initial classes: Flock/ALPR, body cameras, drones, cameras/doorbells and smart glasses;
- do not classify a generic product as surveillance merely because its vendor also makes surveillance products;
- highlight new unique detections visually;
- maintain a surveillance-specific recent/history view;
- allow details to show the evidence/source that caused a classification.

## Alerts

New surveillance detections should support three simultaneous alert channels:

- on-screen alert;
- RGB LED state;
- onboard-speaker Morse alert.

Proposed Morse assignments:

| Class | Letter | Morse |
| --- | --- | --- |
| Flock / ALPR | F | `..-.` |
| Body camera | B | `-...` |
| Drone | D | `-..` |
| Camera / doorbell | C | `-.-.` |
| Smart glasses | G | `--.` |
| Other surveillance | S | `...` |

Timing should use standard relative Morse timing: dit=1 unit, dah=3, intra-character gap=1. Initial configurable unit target: roughly 90-120 ms.

Alerts should occur for a **new unique detection**, not every received packet. Re-alerting should use a configurable absence/cooldown period. A mute setting must suppress audio without suppressing visual logging.

## T-Embed CC1101 Plus capabilities

Planned use of the board beyond Wi-Fi/BLE:

### Display + rotary encoder

Primary UI. Proposed top-level screens:

1. Nearby
2. Surveillance
3. RF Activity
4. Tools

Rotate to navigate/scroll, press to select, with long-press/back semantics determined during UI implementation.

### Speaker

Morse surveillance alerts and optional UI feedback. Prefer the onboard audio path over an external buzzer once the board audio implementation is verified.

### RGB LEDs

Glanceable operating/alert state. Exact colors and animations remain a UI decision; avoid constant distracting animation.

### microSD

Optional long-duration observation/event logging beyond the in-memory rolling window. Logs should be explicit/configurable because device identifiers can be privacy-sensitive.

### CC1101 sub-GHz

Future RF-activity view for supported sub-GHz ranges. Initial goal is awareness (frequency/activity/RSSI/bursts), not automatic transmission or replay.

### nRF24L01

Future 2.4 GHz proprietary-radio activity view. Keep this separate from Wi-Fi/BLE device identity because arbitrary nRF24 traffic does not carry IEEE MAC/OUI identity in the same sense.

### PN532 NFC

Selectable tool for local NFC/tag inspection. Not part of continuous background device census by default.

### IR transmit/receive

Selectable IR activity/analyzer tool. Passive receive is the awareness use case; any transmit/replay functionality should be explicit user action.

### Microphone

No baseline continuous audio recording. Potential future uses should be privacy-conscious and clearly opt-in.

### Battery / power telemetry

Show useful battery state and eventually support power-aware radio/display behavior.

## Future ideas

- per-brand drill-down;
- recent-device timeline;
- source filters (Wi-Fi/BLE/etc.);
- channel/activity summaries;
- RSSI trend/proximity indication without claiming physical distance;
- configurable alert confidence thresholds;
- SD export;
- signature/database update tooling;
- multiple hardware targets, keeping the original ESP32-C5/T-Dongle path possible where 5 GHz Wi-Fi is important.
