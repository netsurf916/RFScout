# Audio Spectrum

Audio Spectrum is RFScout's live microphone spectrum/FFT application. It uses the same board-neutral capture boundary as Recorder but does not require SD storage. Shared audio/lifecycle contracts are defined in [Platform architecture](../ARCHITECTURE.md); calibration and optional analysis improvements belong in [ROADMAP.md](../ROADMAP.md).

## Availability and lifecycle

The application probes the microphone provider at startup and requires delayed activation. Skipping through the app before activation should not touch microphone hardware.

When activated, it acquires the session-managed shared audio-capture service. Recorder and Audio Spectrum therefore cannot independently own I2S0 at the same time.

## Analysis and presentation

Audio Spectrum consumes raw capture PCM through the shared provider and applies its own analysis path:

- continuous DC/high-pass filtering;
- automatic dB display-range scaling;
- FFT/bin presentation; and
- up to three strong local peak-frequency labels.

The display identifies the live microphone and current automatic dB range. Spectrum analysis does not apply Recorder's whole-recording normalization path.

## Sample-rate interpretation

The current provider measures PDM clock diagnostics but does **not** yet measure delivered PCM throughput/effective PCM sample rate. Frequency labels therefore derive from the current configured-rate interpretation. Moving FFT frequency interpretation to a measured effective rate remains roadmap work.

## Current limitations

Hardware tuning of automatic range attack/decay and low-frequency cutoff may still be adjusted from field testing. Additional smoothing/averaging or peak-hold controls are intentionally deferred unless they clearly improve interpretation.
