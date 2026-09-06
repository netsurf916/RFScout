from pathlib import Path


def replace(path, old, new):
    p = Path(path)
    s = p.read_text()
    if old not in s:
        raise SystemExit(f'pattern not found in {path}: {old[:80]!r}')
    p.write_text(s.replace(old, new))

# Availability is intentionally binary once known: usable or unavailable.
replace('main/app_framework.h',
'''    RFSCOUT_APP_AVAILABILITY_AVAILABLE,\n    RFSCOUT_APP_AVAILABILITY_UNAVAILABLE,\n    RFSCOUT_APP_AVAILABILITY_ERROR\n''',
'''    RFSCOUT_APP_AVAILABILITY_AVAILABLE,\n    RFSCOUT_APP_AVAILABILITY_UNAVAILABLE\n''')

p = Path('main/app_framework.c')
s = p.read_text()
s = s.replace('''    else if( RFSCOUT_APP_AVAILABILITY_ERROR == a_availability )\n    {\n        ESP_LOGW( TAG, "%s availability error", descriptor->name );\n    }\n\n''', '')
s = s.replace('''    else if( RFSCOUT_APP_AVAILABILITY_ERROR == a_availability )\n    {\n        rfscout_event_emit(\n            RFSCOUT_EVENT_APP_PROBE_ERROR, (uint8_t)a_app, (int32_t)a_availability );\n    }\n''', '')
s = s.replace('''    if( NULL == a_descriptor )\n    {\n        return RFSCOUT_APP_AVAILABILITY_ERROR;\n    }\n''', '''    if( NULL == a_descriptor )\n    {\n        return RFSCOUT_APP_AVAILABILITY_UNAVAILABLE;\n    }\n''')
s = s.replace('''    rfscout_app_availability_t current = RFSCOUT_APP_AVAILABILITY_ERROR;\n    if( ESP_OK == result )\n    {\n        current = RFSCOUT_APP_AVAILABILITY_AVAILABLE;\n    }\n    else if( ( ESP_ERR_NOT_FOUND == result ) || ( ESP_ERR_INVALID_RESPONSE == result ) ||\n             ( ESP_ERR_TIMEOUT == result ) )\n    {\n        current = RFSCOUT_APP_AVAILABILITY_UNAVAILABLE;\n    }\n\n    const rfscout_app_availability_t previous = availability[ a_descriptor->id ];\n    retain_availability( a_descriptor->id, current );\n    if( ( previous != current ) && ( RFSCOUT_APP_AVAILABILITY_ERROR == current ) )\n    {\n        ESP_LOGW( TAG,\n                  "%s availability probe failed: %s",\n                  a_descriptor->name,\n                  esp_err_to_name( result ) );\n    }\n''', '''    const rfscout_app_availability_t current =\n        ( ESP_OK == result ) ? RFSCOUT_APP_AVAILABILITY_AVAILABLE\n                             : RFSCOUT_APP_AVAILABILITY_UNAVAILABLE;\n\n    const rfscout_app_availability_t previous = availability[ a_descriptor->id ];\n    retain_availability( a_descriptor->id, current );\n    if( ( ESP_OK != result ) && ( previous != current ) &&\n        ( ESP_ERR_NOT_FOUND != result ) && ( ESP_ERR_INVALID_RESPONSE != result ) &&\n        ( ESP_ERR_TIMEOUT != result ) )\n    {\n        ESP_LOGW( TAG,\n                  "%s availability probe failed: %s",\n                  a_descriptor->name,\n                  esp_err_to_name( result ) );\n    }\n''')
s = s.replace('''    return app_id_valid( a_app ) ? availability[ a_app ] : RFSCOUT_APP_AVAILABILITY_ERROR;\n''', '''    return app_id_valid( a_app ) ? availability[ a_app ] : RFSCOUT_APP_AVAILABILITY_UNAVAILABLE;\n''')
p.write_text(s)

# Remove probe-error semantic event; diagnostics retain the esp_err_t detail.
replace('main/event.h', '    RFSCOUT_EVENT_APP_PROBE_ERROR,\n', '')
replace('main/event.c', '        case RFSCOUT_EVENT_APP_PROBE_ERROR:\n', '')

p = Path('main/system_events.c')
s = p.read_text()
s = s.replace('        case RFSCOUT_EVENT_APP_PROBE_ERROR:\n', '')
s = s.replace('''        RFSCOUT_EVENT_INTERNAL_STORAGE_MOUNT_FAILED == a_event->type ||\n        RFSCOUT_EVENT_APP_PROBE_ERROR == a_event->type ||\n        ( RFSCOUT_EVENT_APP_DISCOVERED == a_event->type &&\n          RFSCOUT_APP_AVAILABILITY_ERROR == (rfscout_app_availability_t)a_event->value ) )\n''', '''        RFSCOUT_EVENT_INTERNAL_STORAGE_MOUNT_FAILED == a_event->type )\n''')
s = s.replace('''            else\n            {\n                snprintf( a_detail, a_detail_size, "PROBE ERROR" );\n            }\n''', '''            else\n            {\n                snprintf( a_detail, a_detail_size, "UNAVAILABLE" );\n            }\n''', 1)
s = s.replace('''        case RFSCOUT_EVENT_APP_PROBE_ERROR:\n            snprintf( a_title, a_title_size, "%s", app_name( a_event->source ) );\n            snprintf( a_detail, a_detail_size, "PROBE ERROR" );\n            break;\n''', '')
p.write_text(s)

p = Path('main/apps/system_events/system_events_view.c')
s = p.read_text()
s = s.replace('''            else\n            {\n                snprintf( a_text, a_capacity, "%s PROBE ERROR", event_app_name( a_event->source ) );\n            }\n''', '''            else\n            {\n                snprintf( a_text, a_capacity, "%s UNAVAILABLE", event_app_name( a_event->source ) );\n            }\n''', 1)
s = s.replace('''        case RFSCOUT_EVENT_APP_PROBE_ERROR:\n            snprintf( a_text, a_capacity, "%s PROBE ERROR", event_app_name( a_event->source ) );\n            break;\n''', '')
p.write_text(s)

# Remove the remaining platform/input compatibility shim; power consumes the input service directly.
p = Path('main/services/power/power.c')
s = p.read_text()
if '#include "platform.h"' in s and '#include "input.h"' not in s:
    s = s.replace('#include "platform.h"', '#include "input.h"\n#include "platform.h"')
s = s.replace('platform_input_locked()', 'input_locked()')
p.write_text(s)

p = Path('main/platform.h')
s = p.read_text()
s = s.replace('#include "input.h"\n\n', '')
s = s.replace('''/* Power-state persistence still includes platform.h; keep this source-compatible alias until that\n   service is next touched, without retaining a duplicate implementation or state owner. */\n#define platform_input_locked input_locked\n\n''', '')
p.write_text(s)

# Correct the PR71 comment now inherited from main: init stages rather than consumes imports.
replace('main/config_store.h',
'/** @brief Mount internal persistence and consume a valid SD configuration import when present. */',
'/** @brief Mount internal persistence and stage a valid SD configuration import when present. */')

# Documentation: describe usability, not inferred physical presence/error state.
p = Path('docs/BOARD_PORTING.md')
s = p.read_text()
s = s.replace('''Probe state is retained centrally. The first known result is recorded in the RAM-only System Events history without interactive notification; later availability/error records are emitted only when the retained state changes. Periodic unchanged probes must not produce ring-buffer noise.''',
'''Probe state is retained centrally as usable or unavailable. The first known result is recorded in the RAM-only System Events history without interactive notification; later availability changes are emitted only when the retained state changes. Detailed probe failures remain diagnostic-log data rather than a separate application-availability state. Periodic unchanged probes must not produce ring-buffer noise.''')
p.write_text(s)

p = Path('docs/ROADMAP.md')
s = p.read_text()
s = s.replace('''- charger connect/disconnect, startup SD and internal-storage status, configuration import/default recovery, and Thermal availability events;''',
'''- charger connect/disconnect, startup SD and internal-storage status, configuration import/default recovery, and descriptor-driven application/hardware availability events;''')
s = s.replace('''- boot-state discovery retained/logged without interactive notification behavior;''',
'''- boot-state discovery retained/logged without interactive notification behavior;\n- provider-selection and first-known application/hardware usability diagnostics generalized by PR #72;''')
s = s.replace('''- [ ] Add further hot-pluggable peripheral/capability events as hardware support expands.''',
'''- [ ] Add events for future hot-pluggable peripherals/capabilities that are not represented by the descriptor availability framework.''')
p.write_text(s)
