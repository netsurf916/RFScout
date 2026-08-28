# Audio Input Service

RFScout's ESP32-S3 PDM microphone is a board resource, not a Recorder-specific peripheral. This document defines the next audio-input architecture so Recorder, live level monitoring, and spectrum analysis all consume the same measured and corrected PCM stream.

## Goals

- Configure the microphone for a desired **physical outcome**, then measure and correct the ESP-IDF clock configuration at runtime rather than depending on nominal divider behavior.
- Keep I2S0/PDM ownership in one service. Recorder and analysis consumers must not configure or delete the channel directly.
- Measure the physical PDM clock with PCNT after configuration and retain the result as service telemetry.
- Measure delivered PCM throughput and expose the effective sample rate used for timing, WAV metadata, and FFT frequency bins.
- Keep the board microphone on its deterministic LEFT slot, matching the microphone SELECT pull-down.
- Apply the ESP32-S3 PDM high-pass filter and provide a fixed digital input-gain stage shared by live monitoring and future consumers.
- Expose level, peak, clipping, requested rate, measured PDM clock, effective PCM rate, and calibration status through a small read-only metrics structure.
- Serialize ownership so recording and monitoring cannot open I2S0 concurrently.

## Runtime calibration

The service treats `RFSCOUT_RECORDER_TARGET_PDM_CLOCK_HZ` as policy rather than assuming that an ESP-IDF PCM request produces a particular physical clock.

1. Open I2S0 PDM RX with a conservative initial PCM request.
2. Enable the channel and allow the microphone to wake.
3. Measure GPIO39 with PCNT over a bounded interval.
4. Compare the measured PDM clock with the target and derive a corrected PCM request.
5. Disable the channel, apply `i2s_channel_reconfig_pdm_rx_clock()`, re-enable it, and measure again.
6. Iterate only a small bounded number of times and clamp requests to a known-safe range.
7. Reject a configuration whose measured clock falls outside the microphone's 1.0-3.25 MHz operating range.
8. During initial PCM reads, measure delivered bytes against elapsed time and publish the resulting effective PCM sample rate.

Calibration is successful when the measured clock is within a defined tolerance of the target. Failure must remain visible in metrics/logging; callers must never silently substitute a hard-coded observed mapping such as `48 kHz request -> 44.7 kHz PCM`.

## PCM processing

The shared read path returns signed 16-bit mono PCM. Fixed digital gain is expressed in dB and quantized to the supported 3 dB steps from 0 through +24 dB. Saturation is explicit and contributes to a clipping counter. Level and peak metrics are calculated from the same post-gain samples delivered to consumers.

Recorder may continue save-time DC correction and bounded normalization because those operations depend on statistics over the complete retained recording. They are intentionally separate from the real-time input service.

## Consumers

### Recorder

Recorder owns file creation, asynchronous capture, trimming/normalization, WAV finalization, enumeration, deletion, and playback. It obtains PCM and the effective sample rate from Audio Input. WAV metadata must describe the measured delivered PCM, not the nominal configuration request.

### Monitor

The monitor uses the same corrected/gain-adjusted PCM stream and metrics. The landing/monitor UI can display useful input level and clipping information without maintaining a second microphone implementation.

### FFT

Spectrum analysis consumes the shared PCM stream. Frequency labels and bin interpretation use the service's effective sample rate. A Hann-windowed FFT remains presentation/analysis logic and does not belong in the hardware service.

## Lifecycle

Audio Input has one active owner at a time. Opening fails while another owner holds the service. Closing disables and deletes I2S0 only after the current consumer has stopped reading. Power/storage shutdown first quiesces Recorder/Monitor and therefore releases Audio Input before lower-level resources disappear.

## Validation

Target-hardware validation should log requested PCM rate, each calibration iteration, measured PDM clock, final effective PCM rate, gain, level/peak, and clipping. Tests should include quiet room, speech, whistle, clap/transient, monitor-to-record transitions, and repeated app entry/exit. The same physical input should produce materially consistent calibration results across boots and supported ESP-IDF updates.