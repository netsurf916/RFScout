#!/usr/bin/env bash
set -euo pipefail

git fetch origin main

resolve_event_h() {
    git checkout --ours main/event.h
    python3 - <<'PY'
from pathlib import Path
p = Path('main/event.h')
s = p.read_text()
needle = '    RFSCOUT_EVENT_CONFIG_DEFAULTED,\n    RFSCOUT_EVENT_APP_AVAILABLE,\n'
replace = '    RFSCOUT_EVENT_CONFIG_DEFAULTED,\n    RFSCOUT_EVENT_APP_DISCOVERED,\n    RFSCOUT_EVENT_APP_AVAILABLE,\n'
if needle not in s:
    raise SystemExit('event.h insertion point 1 not found')
s = s.replace(needle, replace, 1)
needle = '    RFSCOUT_EVENT_APP_UNAVAILABLE,\n    RFSCOUT_EVENT_TYPE_COUNT\n'
replace = '    RFSCOUT_EVENT_APP_UNAVAILABLE,\n    RFSCOUT_EVENT_APP_PROBE_ERROR,\n    RFSCOUT_EVENT_DISPLAY_PROVIDER_SELECTED,\n    RFSCOUT_EVENT_INPUT_PROVIDER_SELECTED,\n    RFSCOUT_EVENT_TYPE_COUNT\n'
if needle not in s:
    raise SystemExit('event.h insertion point 2 not found')
s = s.replace(needle, replace, 1)
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
needle = '''        case RFSCOUT_EVENT_CONFIG_IMPORTED:\n        case RFSCOUT_EVENT_CONFIG_DEFAULTED:\n        case RFSCOUT_EVENT_TYPE_COUNT:\n'''
replace = '''        case RFSCOUT_EVENT_CONFIG_IMPORTED:\n        case RFSCOUT_EVENT_CONFIG_DEFAULTED:\n        case RFSCOUT_EVENT_APP_DISCOVERED:\n        case RFSCOUT_EVENT_APP_PROBE_ERROR:\n        case RFSCOUT_EVENT_DISPLAY_PROVIDER_SELECTED:\n        case RFSCOUT_EVENT_INPUT_PROVIDER_SELECTED:\n        case RFSCOUT_EVENT_TYPE_COUNT:\n'''
if needle not in s:
    raise SystemExit('event.c insertion point not found')
s = s.replace(needle, replace, 1)
p.write_text(s)
PY
    git add main/event.c
}

if ! git rebase origin/main; then
    while git rev-parse --verify REBASE_HEAD >/dev/null 2>&1; do
        conflicts="$(git diff --name-only --diff-filter=U)"
        case "$conflicts" in
            main/event.h)
                resolve_event_h
                ;;
            main/event.c)
                resolve_event_c
                ;;
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
