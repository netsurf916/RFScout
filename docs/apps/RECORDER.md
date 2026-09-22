# Recorder

Recorder is RFScout's SD-backed microphone recording and playback application. Shared audio/service/lifecycle contracts are defined in [Platform architecture](../ARCHITECTURE.md); audio calibration/generalization work belongs in [ROADMAP.md](../ROADMAP.md).

## Availability

Recorder uses the board-neutral `audio_capture` service plus removable SD storage. It is hot-plug/dependency probed: the app becomes unavailable when required storage or microphone access is unavailable.

The current T-Embed profile supplies a PDM microphone through the shared audio-capture provider. Recorder never receives a raw I2S handle.

## Recording

Current recordings are **16 kHz / 16-bit mono PCM WAV** files under `/sdcard/audio/`.

Capture itself is not software pre-amplified. After capture, Recorder performs asynchronous post-processing that:

1. trims startup/tail artifacts;
2. removes DC/very-low-frequency content with the shared one-pole high-pass primitive; and
3. applies bounded normalization before finalizing the WAV.

The platform currently measures PDM clock diagnostics, but delivered PCM throughput/effective sample-rate calibration is **not yet implemented**. WAV/sample-rate behavior therefore remains based on the configured capture rate until that roadmap work lands.

## UI and file management

The landing view exposes current state such as READY, RECORDING, SAVING, PLAYING, missing storage, or error.

The recording browser supports:

- creating a new recording;
- browsing recordings and duration;
- playback;
- playback-volume adjustment; and
- long-select deletion with confirmation.

Playback loudness is user-facing percentage state and is persisted as device/app configuration.

## Transition safety

Recording and finalization are semantic transition blockers. While either is active:

- application navigation is blocked/vetoed;
- terminal sleep/power-off preparation is vetoed; and
- RFScout does not attempt to reconstruct or restart an interrupted recording.

Playback can be stopped normally when Recorder is left or a terminal transition is prepared.

## Current limitations

Effective PCM sample-rate measurement/calibration, generalized microphone input gain, and broader app-owned PSRAM scratch separation remain roadmap work. Recorder-specific whole-recording post-processing stays separate from the shared capture provider.
