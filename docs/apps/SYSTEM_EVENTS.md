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
- SD mount and unavailable state;
- display/input hardware discovery;
- internal-storage mount/recovery failures;
- configuration/default-recovery diagnostics; and
- application/system warning or error events emitted through the common event path.

Startup discovery can be retained in history without forcing unrelated audible/interactive notification.

## Presentation relationship

System-event history, transient overlays/display attention, and audible feedback are separate concerns. Common presentation is platform-owned so an event does not take over application lifecycle state.

Protective power presentation has higher priority than ordinary system-event presentation.

## Current limitations

Runtime SD removal is detected when a storage health probe observes media loss. The old volume remains unavailable until a media-store probe invalidates old handles and attempts recovery; later insertion/remount is supported. Event payload richness and any future per-event presentation controls remain roadmap work. Event payload richness and any future per-event presentation controls remain roadmap work.
