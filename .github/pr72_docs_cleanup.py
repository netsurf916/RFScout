from pathlib import Path


def replace(path, old, new):
    p = Path(path)
    s = p.read_text()
    if old not in s:
        raise SystemExit(f'pattern not found in {path}: {old[:100]!r}')
    p.write_text(s.replace(old, new, 1))

# BOARD_PORTING owns board/provider mechanics. Keep it concise and implementation-accurate.
replace('docs/BOARD_PORTING.md',
'''### PSRAM requirement

RFScout's current platform model requires usable external PSRAM. Large persistent platform allocations such as the framebuffer already depend on it, and v0.4.0 planning intentionally treats PSRAM as a supported-target requirement rather than promising a reduced-capacity internal-SRAM fallback.

Internal SRAM remains available for RTOS/system state, task stacks, DMA/peripheral-constrained buffers, and latency-sensitive allocations. A future board target should not be considered supported unless its configured memory topology satisfies RFScout's platform requirements.
''',
'''### PSRAM requirement

Supported targets require usable external PSRAM. Internal SRAM remains reserved for RTOS/system state, task stacks, DMA/peripheral-constrained buffers, and latency-sensitive allocations; RFScout does not promise a reduced-capacity internal-SRAM fallback.
''')
replace('docs/BOARD_PORTING.md',
'''The current T-Embed provider adapts its rotary encoder and buttons into this interface. Applications receive semantic input through the active-app runtime; they should not depend on encoder GPIOs or a particular control layout. Unhandled navigation/Back may fall through to global policy.

Physical activity used for display wake remains distinct from dispatchable semantic input. A complete gesture consumed to wake the display is discarded before app, lock, or power semantics can observe it. Accepted RFScout state changes use the separate shared event dispatcher; raw physical activity is not broadcast merely because it occurred.

A future target may compose multiple compatible input providers, such as keyboard plus trackball/buttons, without changing the application input contract.
''',
'''The current T-Embed provider adapts its rotary encoder and buttons to this interface. Applications receive semantic input through the active-app runtime and do not depend on GPIOs or physical control layout. Unhandled navigation/Back may fall through to global policy.

Display-wake activity is separate from dispatchable input: a gesture used to wake the display is consumed before app, lock, or power handling. Future targets may compose compatible providers without changing the application contract.
''')
replace('docs/BOARD_PORTING.md',
'''Probe state is retained centrally as usable or unavailable. The first known result is recorded in the RAM-only System Events history without interactive notification; later availability changes are emitted only when the retained state changes. Detailed probe failures remain diagnostic-log data rather than a separate application-availability state. Periodic unchanged probes must not produce ring-buffer noise.
''',
'''Probe state is retained centrally as **available** or **unavailable**. The first known result is recorded silently in System Events; later records are emitted only when availability changes. Detailed probe failures stay in diagnostic logs rather than becoming a third availability state.
''')

# ARCHITECTURE owns invariants, not board-porting detail.
replace('docs/ARCHITECTURE.md',
'''Each compiled-in application exports one `rfscout_app_descriptor_t`. The descriptor owns its identity/name, availability probe policy/function, delayed-activation policy, platform-service dependencies, and runtime handler. `app_framework` discovers descriptors through the compiled registry; normal app behavior is not implemented through app-ID switches in `app_manager`.
''',
'''Each compiled-in application exports one `rfscout_app_descriptor_t`. The descriptor owns identity/name, availability probing, delayed activation, service dependencies, and runtime handling. `app_framework` discovers descriptors through the compiled registry and retains probed capability state as unknown, available, or unavailable. Probe error detail remains diagnostic logging rather than application state.
''')
replace('docs/ARCHITECTURE.md',
'''## Semantic input and UI behavior

Physical controls are translated to semantic input before app handling. Only accepted semantic actions/state changes emit feedback events; raw encoder/contact movement does not itself imply sound.

On the current one-center-button target, completed button edges are captured independently from the main presentation loop so a click can survive a temporary synchronous LCD stall. Hold timing is part of the semantic input contract: approximately 0.8 to under 1.5 seconds produces `LONG_SELECT`, 1.5 to under 2 seconds produces `BACK`, and a two-second hold toggles the global input lock. The final release after a lock-toggle hold is consumed.
''',
'''## Semantic input and UI behavior

The `input` service translates board controls into focused semantic input before app handling. Physical activity used to wake the display is a separate path and the complete wake gesture is consumed before app, lock, or power semantics. Only accepted semantic actions/state changes emit feedback events.

On the current rotary/button provider, edge capture survives temporary main-loop/display stalls. Holds of roughly 0.8–1.5 seconds produce `LONG_SELECT`, 1.5–2 seconds produce `BACK`, and two seconds toggles the global input lock; the release that completes a lock toggle is consumed.
''')
replace('docs/ARCHITECTURE.md',
'''## Power, display idle, and sleep

Power actions are semantic and board-capability aware. Short press may be None, Screen off, Sleep, or Power off; long press may be None or Power off. Display wake has priority: a complete gesture that wakes a dim/off display is consumed before app, lock, or power-button handling.

Display dim/off is presentation policy, **not** application lifecycle or ESP32 sleep. The off state sets backlight to zero, disables ST7789 output, enters panel hardware sleep, and suppresses framebuffer SPI presentation while apps/acquisition/logging/audio continue. Wake performs panel sleep-out/settle, presents the latest framebuffer while dark, then restores brightness.
''',
'''## Power, display idle, and sleep

Power actions are semantic and board-capability aware. Short press may be None, Screen off, Sleep, or Power off; long press may be None or Power off. Display wake has priority over ordinary input and power-button handling.

The display service owns framebuffer presentation plus runtime provider metadata (provider, geometry, capabilities). Display dim/off is presentation policy, **not** application lifecycle or ESP32 sleep. The current ST7789 provider disables output and enters controller low power while apps/acquisition/logging/audio continue; wake restores the provider, presents the latest framebuffer while dark, then restores brightness.
''')

# CAPABILITIES stays user-facing and avoids architecture duplication.
replace('docs/CAPABILITIES.md',
'''The current supported target is **LILYGO T-Embed CC1101 Plus / ESP32-S3**. Application code consumes board-neutral services/capabilities rather than board names.
''',
'''The current supported target is **LILYGO T-Embed CC1101 Plus / ESP32-S3**. RFScout exposes board-neutral input/display services; applications consume semantic input and capabilities rather than board or controller identities.
''')
replace('docs/CAPABILITIES.md',
'''Automatic screen-off affects presentation only. The ST7789 enters hardware sleep and framebuffer transfers are suppressed while apps, logging, RF acquisition, and audio continue.
''',
'''Automatic screen-off affects presentation only. The display service enters the selected provider's low-power state and suppresses framebuffer transfers while apps, logging, RF acquisition, and audio continue. The current provider is ST7789.
''')

# ROADMAP records status/future work without restating production docs.
replace('docs/ROADMAP.md',
'''Implemented behavior includes:

- shared semantic system-state event IDs on the existing event dispatcher;
- fixed-size RAM-only recent-event history independent from normal diagnostics;
- charger connect/disconnect, startup SD and internal-storage status, configuration import/default recovery, and descriptor-driven application/hardware availability events;
- transient global event overlay without changing application lifecycle state;
- notification-driven display attention wake preserving the original inactivity deadline;
- common acquired/lost procedural cues under Normal/Quiet audio policy;
- boot-state discovery retained/logged without interactive notification behavior;
- provider-selection and first-known application/hardware usability diagnostics generalized by PR #72;
- optional default-disabled System Events application with stable browse snapshot.
''',
'''Implemented behavior includes the RAM-only semantic event history/UI, transient attention presentation, power/storage/configuration events, and descriptor-driven application/hardware availability. PR #72 generalizes first-known availability and input/display provider diagnostics while keeping startup discovery silent.
''')
