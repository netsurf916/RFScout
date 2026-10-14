# Audio Spectrum

Audio Spectrum is RFScout's live microphone spectrum/FFT application. It uses the same board-neutral capture boundary as Recorder but does not require SD storage. Shared audio/lifecycle contracts are defined in [Platform architecture](../ARCHITECTURE.md); calibration and optional analysis improvements belong in [ROADMAP.md](../ROADMAP.md).

## Availability and lifecycle

The application uses the platform's common **hot-plug** probe policy for the microphone provider and requires delayed activation. The same retry-safe microphone probe runs during initial discovery and explicit availability checks during navigation; there is no background re-probe while the app is inactive. Skipping through the app before activation should not leave microphone capture active.

When activated, it acquires the session-managed shared audio-capture service. Recorder and Audio Spectrum therefore cannot independently own I2S0 at the same time.

Audio Spectrum has **no SD-storage dependency**. Removing/unavailable SD storage can make Recorder unavailable while Audio Spectrum remains available as long as the microphone provider itself is healthy. This has also been verified on the target hardware.

Because the SD logger is unavailable in that condition, no SD-resident application log can directly prove the no-SD run; serial/console diagnostics are the appropriate evidence source for storage-absent validation.

## Analysis and presentation

Audio Spectrum consumes raw capture PCM through the shared provider and applies its own analysis path:

- continuous DC/high-pass filtering;
- automatic dB display-range scaling;
- FFT/bin presentation; and
- up to three strong local peak-frequency labels.

The spectrum fills a centered plot below the shared header, with up to three peak-frequency labels and a nine-pixel top label strip that keeps full-height peaks clear of their labels. Redundant microphone, automatic-range, filter-status, and navigation text are omitted. Automatic fit follows the measured floor and peak without a fixed minimum/maximum dB span, retaining 3 dB peak headroom and the existing smoothing. Silent input remains finite; range telemetry uses a floating-point dB value without the former 8-bit cap. Spectrum analysis does not apply Recorder's whole-recording normalization path.

## Sample-rate interpretation

The live monitor begins with the configured capture rate, then measures delivered PCM throughput over wall time while running. Valid measurements update the monitor sample rate used by `recorder_monitor_bin_frequency_hz()`, so peak-frequency labels use the measured effective rate after the first valid measurement. PDM clock diagnostics are measured separately by the low-level capture provider.

### SD telemetry example

With SD logging present, delayed activation is visible as the Recorder service changing from idle to monitoring. This captured transition shows the app entry followed by active microphone monitoring one second later:

```json
{"type":"telemetry","time":"2026-09-23T22:39:15Z","app":"AUDIO_SPECTRUM","state":0,"range_db":52}
{"type":"telemetry","time":"2026-09-23T22:39:16Z","app":"AUDIO_SPECTRUM","state":3,"range_db":19}
```

A brief visit that ends before delayed activation is expected to remain at state 0 and should not acquire the microphone.

## Current limitations

The measured effective rate is currently maintained by the Recorder/monitor layer rather than exposed as generalized calibrated `audio_capture` telemetry. Hardware tuning of automatic range attack/decay and low-frequency cutoff may still be adjusted from field testing. Additional smoothing/averaging or peak-hold controls are intentionally deferred unless they clearly improve interpretation.
