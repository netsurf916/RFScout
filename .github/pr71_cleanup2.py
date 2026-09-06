from pathlib import Path

p = Path('main/device_config.c')
s = p.read_text()

# Remove obsolete schema-version tracking and legacy theme gain helper.
s = s.replace('static int config_source_version;\n', '')
s = s.replace('    config_source_version = (int)RFSCOUT_CONFIG_SCHEMA_VERSION;\n', '')
start = s.index('static void legacy_audio_gains_for_theme(')
end = s.index('/** @brief Copy a bounded configuration string. */', start)
s = s[:start] + s[end:]

# Replace legacy-aware theme parsing with current-schema-only overlay behavior.
start = s.index('/** @brief Apply validated display preference overrides and older-theme audio migration. */')
end = s.index('/** @brief Apply independent device-wide audio presentation policy and gain state. */', start)
new = '''/** @brief Apply validated display preference overrides. */\nstatic void load_display( const cJSON *a_root )\n{\n    const cJSON *display = cJSON_GetObjectItemCaseSensitive( a_root, "display" );\n    if( !cJSON_IsObject( display ) )\n    {\n        return;\n    }\n\n    const cJSON *brightness = cJSON_GetObjectItemCaseSensitive( display, "brightness" );\n    if( cJSON_IsNumber( brightness ) && ( brightness->valueint >= 10 ) &&\n        ( brightness->valueint <= 100 ) )\n    {\n        config.brightness_percent = (uint8_t)brightness->valueint;\n    }\n    config.display_dim_timeout_seconds = json_display_timeout_seconds(\n        display, "dim_timeout_seconds", config.display_dim_timeout_seconds );\n    config.display_off_timeout_seconds = json_display_timeout_seconds(\n        display, "off_timeout_seconds", config.display_off_timeout_seconds );\n    if( ( 0U != config.display_dim_timeout_seconds ) &&\n        ( 0U != config.display_off_timeout_seconds ) &&\n        ( config.display_off_timeout_seconds <= config.display_dim_timeout_seconds ) )\n    {\n        config.display_off_timeout_seconds = 0U;\n    }\n    config.clock_24h = json_bool( display, "clock_24h", config.clock_24h );\n\n    const cJSON *theme = cJSON_GetObjectItemCaseSensitive( display, "theme" );\n    if( cJSON_IsString( theme ) && NULL != theme->valuestring )\n    {\n        rfscout_theme_id_t parsed_theme;\n        if( ui_theme_from_config_name( theme->valuestring, &parsed_theme ) )\n        {\n            config.theme = parsed_theme;\n        }\n        else\n        {\n            ESP_LOGW( TAG, "invalid theme '%s'; using default", theme->valuestring );\n        }\n    }\n}\n\n'''
s = s[:start] + new + s[end:]

# Exact schema validation makes source-version assignment unnecessary.
s = s.replace('    config_source_version = version->valueint;\n\n', '')

# Preserve the 48 dB Recorder input-gain range established by PR #70.
s = s.replace('( input_gain->valueint <= 24 )', '( input_gain->valueint <= 48 )')

p.write_text(s)

# Documentation: remove legacy Discipline compatibility claims.
for name in ['docs/CONFIGURATION.md', 'docs/ROADMAP.md']:
    p = Path(name)
    s = p.read_text()
    s = s.replace('the legacy `discipline` token is normalized to Night + Quiet when encountered inside an otherwise current-schema configuration;\n', 'unsupported or invalid current-schema field values retain their compiled defaults; legacy theme aliases are not supported;\n')
    s = s.replace('Legacy `discipline` is normalized to Night + Quiet when encountered in an otherwise current-schema document.\n', 'Unsupported or invalid current-schema field values retain their compiled defaults; legacy theme aliases such as `discipline` are not supported.\n')
    s = s.replace('Legacy Discipline maps to **Night + Quiet**.\n', '')
    p.write_text(s)
