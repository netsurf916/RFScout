from pathlib import Path

p = Path('main/system_events.c')
s = p.read_text()
s = s.replace('''            else if( RFSCOUT_APP_AVAILABILITY_UNAVAILABLE ==
                     (rfscout_app_availability_t)a_event->value )
            {
                snprintf( a_detail, a_detail_size, "NOT PRESENT" );
            }
            else
            {
                snprintf( a_detail, a_detail_size, "UNAVAILABLE" );
            }
''','''            else
            {
                snprintf( a_detail, a_detail_size, "UNAVAILABLE" );
            }
''',1)
s = s.replace('''        case RFSCOUT_EVENT_APP_UNAVAILABLE:
            snprintf( a_title, a_title_size, "%s", app_name( a_event->source ) );
            snprintf( a_detail, a_detail_size, "DISCONNECTED" );
            break;
            snprintf( a_title, a_title_size, "%s", app_name( a_event->source ) );
            snprintf( a_detail, a_detail_size, "PROBE ERROR" );
            break;
''','''        case RFSCOUT_EVENT_APP_UNAVAILABLE:
            snprintf( a_title, a_title_size, "%s", app_name( a_event->source ) );
            snprintf( a_detail, a_detail_size, "DISCONNECTED" );
            break;
''',1)
p.write_text(s)

p = Path('docs/ROADMAP.md')
s = p.read_text()
needle = '- [ ] Add per-event presentation controls only if practical use demonstrates a real need.\n'
replacement = needle + '- [ ] Define per-event presentation policy separately for history retention, transient overlay/display wake, and audible feedback; keep startup discovery silent unless field use demonstrates a need.\n'
if replacement not in s:
    if needle not in s:
        raise SystemExit('roadmap insertion point not found')
    s = s.replace(needle, replacement, 1)
p.write_text(s)
