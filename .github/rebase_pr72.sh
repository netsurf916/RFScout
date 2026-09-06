#!/usr/bin/env bash
set -euo pipefail

git fetch origin main

resolve_event_h() {
    git checkout --ours main/event.h
    python3 - <<'PY'
from pathlib import Path
p = Path('main/event.h')
s = p.read_text()
s = s.replace('    RFSCOUT_EVENT_CONFIG_DEFAULTED,\n    RFSCOUT_EVENT_APP_AVAILABLE,\n', '    RFSCOUT_EVENT_CONFIG_DEFAULTED,\n    RFSCOUT_EVENT_APP_DISCOVERED,\n    RFSCOUT_EVENT_APP_AVAILABLE,\n', 1)
s = s.replace('    RFSCOUT_EVENT_APP_UNAVAILABLE,\n    RFSCOUT_EVENT_TYPE_COUNT\n', '    RFSCOUT_EVENT_APP_UNAVAILABLE,\n    RFSCOUT_EVENT_APP_PROBE_ERROR,\n    RFSCOUT_EVENT_DISPLAY_PROVIDER_SELECTED,\n    RFSCOUT_EVENT_INPUT_PROVIDER_SELECTED,\n    RFSCOUT_EVENT_TYPE_COUNT\n', 1)
p.write_text(s)
PY
    git add main/event.h
}

resolve_event_c() {
    git checkout --ours main/event.c
    python3 - <<'PY'
from pathlib import Path
p = Path('main/event.c')
s = p.read_text()
s = s.replace('        case RFSCOUT_EVENT_CONFIG_DEFAULTED:\n        case RFSCOUT_EVENT_TYPE_COUNT:\n', '        case RFSCOUT_EVENT_CONFIG_DEFAULTED:\n        case RFSCOUT_EVENT_APP_DISCOVERED:\n        case RFSCOUT_EVENT_APP_PROBE_ERROR:\n        case RFSCOUT_EVENT_DISPLAY_PROVIDER_SELECTED:\n        case RFSCOUT_EVENT_INPUT_PROVIDER_SELECTED:\n        case RFSCOUT_EVENT_TYPE_COUNT:\n', 1)
p.write_text(s)
PY
    git add main/event.c
}

resolve_system_events() {
    git checkout --theirs main/system_events.c
    python3 - <<'PY'
from pathlib import Path
p = Path('main/system_events.c')
s = p.read_text()
# Keep PR72's generalized app/provider handling while restoring PR71's internal-persistence events.
s = s.replace('        case RFSCOUT_EVENT_STORAGE_UNMOUNTED:\n        case RFSCOUT_EVENT_APP_DISCOVERED:\n', '        case RFSCOUT_EVENT_STORAGE_UNMOUNTED:\n        case RFSCOUT_EVENT_INTERNAL_STORAGE_MOUNTED:\n        case RFSCOUT_EVENT_INTERNAL_STORAGE_FORMATTED:\n        case RFSCOUT_EVENT_INTERNAL_STORAGE_MOUNT_FAILED:\n        case RFSCOUT_EVENT_CONFIG_IMPORTED:\n        case RFSCOUT_EVENT_CONFIG_DEFAULTED:\n        case RFSCOUT_EVENT_APP_DISCOVERED:\n', 1)
s = s.replace('        RFSCOUT_EVENT_STORAGE_MOUNT_FAILED == a_event->type ||\n        RFSCOUT_EVENT_APP_PROBE_ERROR == a_event->type ||\n', '        RFSCOUT_EVENT_STORAGE_MOUNT_FAILED == a_event->type ||\n        RFSCOUT_EVENT_INTERNAL_STORAGE_MOUNT_FAILED == a_event->type ||\n        RFSCOUT_EVENT_APP_PROBE_ERROR == a_event->type ||\n', 1)
s = s.replace('        case RFSCOUT_EVENT_STORAGE_UNMOUNTED:\n        case RFSCOUT_EVENT_APP_AVAILABLE:\n', '        case RFSCOUT_EVENT_STORAGE_UNMOUNTED:\n        case RFSCOUT_EVENT_INTERNAL_STORAGE_MOUNTED:\n        case RFSCOUT_EVENT_INTERNAL_STORAGE_FORMATTED:\n        case RFSCOUT_EVENT_INTERNAL_STORAGE_MOUNT_FAILED:\n        case RFSCOUT_EVENT_CONFIG_IMPORTED:\n        case RFSCOUT_EVENT_CONFIG_DEFAULTED:\n        case RFSCOUT_EVENT_APP_AVAILABLE:\n', 1)
needle = '''        case RFSCOUT_EVENT_STORAGE_UNMOUNTED:\n            snprintf( a_title, a_title_size, "SD CARD" );\n            snprintf( a_detail, a_detail_size, "UNMOUNTED" );\n            break;\n'''
insert = needle + '''        case RFSCOUT_EVENT_INTERNAL_STORAGE_MOUNTED:\n            snprintf( a_title, a_title_size, "INTERNAL STORAGE" );\n            snprintf( a_detail, a_detail_size, "MOUNTED" );\n            break;\n        case RFSCOUT_EVENT_INTERNAL_STORAGE_FORMATTED:\n            snprintf( a_title, a_title_size, "INTERNAL STORAGE" );\n            snprintf( a_detail, a_detail_size, "FORMATTED" );\n            break;\n        case RFSCOUT_EVENT_INTERNAL_STORAGE_MOUNT_FAILED:\n            snprintf( a_title, a_title_size, "INTERNAL STORAGE" );\n            snprintf( a_detail, a_detail_size, "MOUNT FAILED" );\n            break;\n        case RFSCOUT_EVENT_CONFIG_IMPORTED:\n            snprintf( a_title, a_title_size, "CONFIG" );\n            snprintf( a_detail, a_detail_size, "IMPORTED FROM SD" );\n            break;\n        case RFSCOUT_EVENT_CONFIG_DEFAULTED:\n            snprintf( a_title, a_title_size, "CONFIG" );\n            snprintf( a_detail, a_detail_size, "DEFAULTS RESTORED" );\n            break;\n'''
if needle not in s:
    raise SystemExit('system_events render insertion point not found')
s = s.replace(needle, insert, 1)
p.write_text(s)
PY
    git add main/system_events.c
}

if ! git rebase origin/main; then
    while git rev-parse --verify REBASE_HEAD >/dev/null 2>&1; do
        conflicts="$(git diff --name-only --diff-filter=U)"
        case "$conflicts" in
            main/event.h) resolve_event_h ;;
            main/event.c) resolve_event_c ;;
            main/system_events.c) resolve_system_events ;;
            *)
                echo "Unresolved rebase conflicts:" >&2
                printf '%s\n' "$conflicts" >&2
                exit 2
                ;;
        esac
        if GIT_EDITOR=true git rebase --continue; then
            break
        fi
    done
fi

git rm .github/rebase_pr72.sh .github/workflows/rebase-pr72.yml
git commit -m "Rebase PR72 onto current main"
git push --force-with-lease origin HEAD:design/service-provider-events
