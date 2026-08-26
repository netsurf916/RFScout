#!/usr/bin/env python3
"""Temporary cleanup helper for newly added Device/settings sources."""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def patch(path: str, old: str, new: str) -> None:
    target = ROOT / path
    text = target.read_text()
    if old not in text:
        raise SystemExit(f"pattern not found in {path}: {old[:100]!r}")
    target.write_text(text.replace(old, new, 1))


patch(
    "main/device_config.c",
    "#include <stdio.h>\n#include <string.h>",
    "#include <stdio.h>\n#include <stdlib.h>\n#include <string.h>",
)

# Preference saves retain the last hard-shutdown time; only a hard shutdown updates it.
patch(
    "main/device_config.c",
    "static cJSON *build_json( bool a_include_time, int64_t a_epoch, const char *a_time_source )\n{",
    "static cJSON *build_json( bool a_update_time, int64_t a_epoch, const char *a_time_source )\n{\n    if( a_update_time )\n    {\n        saved_time_valid = ( a_epoch >= 1577836800LL ) && ( a_epoch <= 4102444800LL );\n        saved_time_epoch = saved_time_valid ? a_epoch : 0;\n        copy_string( saved_time_source,\n                     sizeof( saved_time_source ),\n                     saved_time_valid && ( NULL != a_time_source ) ? a_time_source : \"\" );\n    }",
)
patch(
    "main/device_config.c",
    "    if( a_include_time && ( a_epoch >= 1577836800LL ) )\n    {\n        cJSON *time = cJSON_AddObjectToObject( root, \"time\" );\n        cJSON_AddNumberToObject( time, \"epoch\", (double)a_epoch );\n        cJSON_AddStringToObject( time, \"source\", ( NULL != a_time_source ) ? a_time_source : \"config\" );\n    }",
    "    if( saved_time_valid )\n    {\n        cJSON *time = cJSON_AddObjectToObject( root, \"time\" );\n        cJSON_AddNumberToObject( time, \"epoch\", (double)saved_time_epoch );\n        cJSON_AddStringToObject( time,\n                                 \"source\",\n                                 saved_time_source[ 0 ] ? saved_time_source : \"config\" );\n    }",
)
patch(
    "main/device_config.c",
    "esp_err_t device_config_save( bool a_include_time, int64_t a_epoch, const char *a_time_source )",
    "esp_err_t device_config_save( bool a_update_time, int64_t a_epoch, const char *a_time_source )",
)
patch(
    "main/device_config.c",
    "    cJSON *root = build_json( a_include_time, a_epoch, a_time_source );",
    "    cJSON *root = build_json( a_update_time, a_epoch, a_time_source );",
)

# Preserve original clock authority while recording that boot used saved configuration.
patch(
    "main/device_time.c",
    "static bool valid;",
    "static bool valid;\nstatic bool restored_from_config;",
)
patch(
    "main/device_time.c",
    "    valid = false;\n    (void)device_time_apply_timezone();",
    "    valid = false;\n    restored_from_config = false;\n    (void)device_time_apply_timezone();",
)
patch(
    "main/device_time.c",
    "    if( device_config_saved_time( &epoch, &saved_source ) )\n    {\n        copy_string:\n        snprintf( source_detail,\n                  sizeof( source_detail ),\n                  \"%s\",\n                  ( NULL != saved_source ) ? saved_source : \"config\" );\n        return device_time_set_epoch( (time_t)epoch, RFSCOUT_TIME_SOURCE_CONFIG, source_detail );\n    }",
    "    if( device_config_saved_time( &epoch, &saved_source ) )\n    {\n        rfscout_time_source_t restored_source = RFSCOUT_TIME_SOURCE_CONFIG;\n        const char *detail = ( NULL != saved_source ) ? saved_source : \"config\";\n        if( 0 == strcmp( detail, \"manual\" ) )\n        {\n            restored_source = RFSCOUT_TIME_SOURCE_MANUAL;\n        }\n        else if( 0 == strcmp( detail, \"gps\" ) )\n        {\n            restored_source = RFSCOUT_TIME_SOURCE_GPS;\n        }\n        else if( 0 == strcmp( detail, \"ntp\" ) )\n        {\n            restored_source = RFSCOUT_TIME_SOURCE_NTP;\n        }\n        else if( 0 == strncmp( detail, \"rtc:\", 4U ) || ( 0 == strcmp( detail, \"rtc\" ) ) )\n        {\n            restored_source = RFSCOUT_TIME_SOURCE_RTC;\n        }\n        const esp_err_t result = device_time_set_epoch( (time_t)epoch, restored_source, detail );\n        restored_from_config = ( ESP_OK == result );\n        return result;\n    }",
)
patch(
    "main/device_time.c",
    "    source = a_source;",
    "    source = a_source;\n    restored_from_config = false;",
)
patch(
    "main/device_time.c",
    "const char *device_time_source_label( void )\n{",
    "const char *device_time_source_label( void )\n{\n    static char label[ 32 ];\n    const char *base = NULL;",
)
patch(
    "main/device_time.c",
    "        case RFSCOUT_TIME_SOURCE_CONFIG:\n            return \"Saved\";\n        case RFSCOUT_TIME_SOURCE_MANUAL:\n            return \"Manual\";\n        case RFSCOUT_TIME_SOURCE_GPS:\n            return \"GPS\";\n        case RFSCOUT_TIME_SOURCE_RTC:\n            return source_detail[ 0 ] ? source_detail : \"RTC\";\n        case RFSCOUT_TIME_SOURCE_NTP:\n            return \"NTP\";\n        default:\n            return \"Unset\";\n    }\n}",
    "        case RFSCOUT_TIME_SOURCE_CONFIG:\n            base = \"Saved\";\n            break;\n        case RFSCOUT_TIME_SOURCE_MANUAL:\n            base = \"Manual\";\n            break;\n        case RFSCOUT_TIME_SOURCE_GPS:\n            base = \"GPS\";\n            break;\n        case RFSCOUT_TIME_SOURCE_RTC:\n            base = source_detail[ 0 ] ? source_detail : \"RTC\";\n            break;\n        case RFSCOUT_TIME_SOURCE_NTP:\n            base = \"NTP\";\n            break;\n        default:\n            return \"Unset\";\n    }\n    if( restored_from_config )\n    {\n        snprintf( label, sizeof( label ), \"%s (saved)\", base );\n        return label;\n    }\n    return base;\n}",
)

# Device view gets an explicit Back row; this app intentionally owns the encoder while active.
patch(
    "main/device_view.c",
    "#define DEVICE_ITEM_SAVE 14U\n#define DEVICE_ITEM_COUNT 15U",
    "#define DEVICE_ITEM_SAVE 14U\n#define DEVICE_ITEM_BACK 15U\n#define DEVICE_ITEM_COUNT 16U",
)
patch(
    "main/device_view.c",
    "    draw_row( 118, DEVICE_ITEM_SAVE, \"Save config\", storage_is_available() ? \"SD\" : \"No SD\" );\n\n    char source[ 48 ];\n    snprintf( source, sizeof( source ), \"Time source: %s\", device_time_source_label() );\n    display_draw_text( 22, 143, source, COLOR_DIM, 1U );",
    "    draw_row( 118, DEVICE_ITEM_SAVE, \"Save config\", storage_is_available() ? \"SD\" : \"No SD\" );\n    draw_row( 133, DEVICE_ITEM_BACK, \"Back\", \"\" );\n\n    char source[ 48 ];\n    snprintf( source, sizeof( source ), \"Time source: %s\", device_time_source_label() );\n    display_draw_text( 22, 148, source, COLOR_DIM, 1U );",
)
patch(
    "main/device_view.c",
    "void device_view_input( bool a_pressed, int a_delta )\n{",
    "bool device_view_input( bool a_pressed, int a_delta )\n{",
)
patch(
    "main/device_view.c",
    "    if( a_pressed )\n    {\n        if( DEVICE_ITEM_SAVE == selected_item )",
    "    if( a_pressed )\n    {\n        if( ( DEVICE_ITEM_BACK == selected_item ) && !editing )\n        {\n            return true;\n        }\n        if( DEVICE_ITEM_SAVE == selected_item )",
)
patch(
    "main/device_view.c",
    "            device_view_render();\n            return;\n        }",
    "            device_view_render();\n            return false;\n        }",
)
patch(
    "main/device_view.c",
    "        device_view_render();\n        return;\n    }\n\n    if( 0 == a_delta )\n    {\n        return;\n    }",
    "        device_view_render();\n        return false;\n    }\n\n    if( 0 == a_delta )\n    {\n        return false;\n    }",
)
patch(
    "main/device_view.c",
    "    device_view_render();\n}\n",
    "    device_view_render();\n    return false;\n}\n",
)

print("New Device/settings sources normalized")
