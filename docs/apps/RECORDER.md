# Recorder

Recorder is RFScout's SD-backed microphone recording and playback application. Shared audio/service/lifecycle contracts are defined in [Platform architecture](../ARCHITECTURE.md); audio calibration/generalization work belongs in [ROADMAP.md](../ROADMAP.md).

## Availability

Recorder uses the board-neutral `audio_capture` service plus removable SD storage and follows the common **hot-plug** probe path. Both required dependencies are checked through retry-safe probes; the microphone result is not privately cached after one attempt. If no card mounted at startup, later hot-plug probes retry the ordinary mount and can make Recorder available after insertion. Runtime SD removal is still latched unavailable once a mounted filesystem is lost; automatic **remount after loss** is intentionally not supported yet because filesystem/open-file lifetime must be reconciled first.

The current T-Embed profile supplies a PDM microphone through the shared audio-capture provider. Recorder never receives a raw I2S handle.

## Recording

Recorder requests a **16 kHz** capture target and writes **16-bit mono PCM WAV** files under `/sdcard/audio/`.

Capture itself is not software pre-amplified. After capture, Recorder performs asynchronous post-processing that:

1. trims startup/tail artifacts;
2. removes DC/very-low-frequency content with the shared one-pole high-pass primitive; and
3. applies bounded normalization before finalizing the WAV.

The shared Recorder service measures delivered PCM throughput over recording wall time. When that measured rate is within the accepted WAV range (8–48 kHz), it becomes the WAV header sample rate; otherwise Recorder retains the configured capture rate. PDM clock measurement is also logged for calibration diagnostics. This is a current consumer-level correction, not yet a generalized calibrated-rate contract in the low-level `audio_capture` API.

## UI and file management

The landing view exposes current state such as READY, RECORDING, SAVING, PLAYING, missing storage, or error.

The recording browser supports:

- creating a new recording;
- browsing recordings and duration;
- playback;
- playback-volume adjustment; and
- long-select deletion with confirmation.

Playback loudness is user-facing percentage state and is persisted as device/app configuration.

### Recorder state and SD telemetry

The structured app log exposes Recorder's internal state numerically:

| State | Meaning |
| ---: | --- |
| 0 | Idle / ready |
| 1 | Recording |
| 2 | Saving / asynchronous post-processing |
| 3 | Live microphone monitoring (used by Audio Spectrum) |
| 4 | Playback |
| 5 | No storage |
| 6 | Error |

When SD logging is available, Recorder telemetry also includes the current playable-file count and the service's current/most-recent PCM byte count. A hardware recording captured the expected transition into recording and then asynchronous save:

```json
{"type":"telemetry","time":"2026-09-23T22:37:16Z","app":"RECORDER","state":1,"files":0,"bytes":1162240}
{"type":"telemetry","time":"2026-09-23T22:37:17Z","app":"RECORDER","state":2,"files":0,"bytes":1163264}
{"type":"telemetry","time":"2026-09-23T22:37:32Z","app":"RECORDER","state":0,"files":1,"bytes":1076892}
{"type":"telemetry","time":"2026-09-23T22:37:34Z","app":"RECORDER","state":4,"files":1,"bytes":1076892}
```

Because Recorder itself requires mounted storage, no Recorder SD-log record can prove its behavior while the SD card is absent. In that condition the application is expected to become unavailable rather than remain navigable.

## Transition safety

Recording and finalization are semantic transition blockers. While either is active:

- application navigation is blocked/vetoed;
- terminal sleep/power-off preparation is vetoed; and
- RFScout does not attempt to reconstruct or restart an interrupted recording.

Playback can be stopped normally when Recorder is left or a terminal transition is prepared.

## Current limitations

Generalizing effective-rate/calibration telemetry into the shared `audio_capture` API, bounded clock/rate correction, generalized microphone input gain, and broader app-owned PSRAM scratch separation remain roadmap work. Recorder-specific whole-recording post-processing stays separate from the shared capture provider.
