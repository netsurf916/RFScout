# System Events

System Events is RFScout's optional read-only view of the platform's bounded volatile event history. Shared event/presentation rules are defined in [Platform architecture](../ARCHITECTURE.md); future diagnostics/event work belongs in [ROADMAP.md](../ROADMAP.md).

## Availability and lifecycle

System Events is disabled by default but requires no hardware probe or session-managed service when enabled. It does not own persistent storage.

The underlying history is RAM-only and bounded. Application transitions do not turn the event ring into app-owned state.

## Live and Browse modes

The normal view is **LIVE** and follows current history.

Pressing the center control enters **BROWSE**, which captures a stable snapshot of the current ring. Rotary navigation scrolls that snapshot without incoming events moving the user's selection. Center exits browse mode.

The view displays compact severity/source markers and current semantic event text.

## Current event sources

Implemented event history includes platform/application lifecycle and diagnostic events such as:

- external-power connection/disconnection;
- initial SD mount/failure, runtime SD connection loss, and subsequent successful recovery;
- display/input hardware discovery;
- internal-storage mount/recovery failures;
- configuration/default-recovery diagnostics; and
- application/system warning or error events emitted through the common event path.

Startup discovery can be retained in history without forcing unrelated audible/interactive notification.

## Presentation relationship

System-event history, transient overlays/display attention, and audible feedback are separate concerns. Common presentation is platform-owned so an event does not take over application lifecycle state.

Protective power presentation has higher priority than ordinary system-event presentation.

## Current limitations

Runtime SD removal is detected when a storage health probe or filesystem I/O operation reports media loss. A loss is reported once, not on every failed write or cooldown rejection; a subsequent successful mount produces a recovery event. A storage request after the shared three-second cooldown invalidates old file handles and attempts a remount. These ordered state transitions are staged inside the storage service and dispatched from
the main loop outside SPI/storage arbitration. A remount is reported as recovered only
after a subsequent physical card-status probe succeeds. No background card probe is performed: reinsertion without another SD request cannot produce a recovery event until the next request. Event payload richness and any future per-event presentation controls remain roadmap work.
