from pathlib import Path


def once(text, old, new, label):
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected 1 match, found {count}")
    return text.replace(old, new, 1)

# config_store.c: stage valid imports, but do not consume SD until device_config
# has successfully parsed and canonicalized the internal copy.
p = Path("main/config_store.c")
s = p.read_text()
s = s.replace("static bool imported;", "static bool import_pending;")
s = once(
    s,
    '''    imported = true;\n\n    if( 0 != unlink( RFSCOUT_SD_CONFIG_IMPORT_PATH ) )\n    {\n        ESP_LOGW( TAG, \"imported SD configuration but could not remove the source file\" );\n    }\n    else\n    {\n        ESP_LOGI( TAG, \"imported SD configuration into internal flash\" );\n    }\n''',
    '''    import_pending = true;\n    ESP_LOGI( TAG, \"staged SD configuration in internal flash\" );\n''',
    "defer import consumption",
)
s = once(
    s,
    '''bool config_store_imported( void )\n{\n    return imported;\n}\n\n''',
    '''bool config_store_import_pending( void )\n{\n    return import_pending;\n}\n\nesp_err_t config_store_finalize_import( void )\n{\n    if( !import_pending )\n    {\n        return ESP_OK;\n    }\n    if( !storage_is_available() )\n    {\n        return ESP_ERR_INVALID_STATE;\n    }\n    if( 0 != unlink( RFSCOUT_SD_CONFIG_IMPORT_PATH ) )\n    {\n        ESP_LOGW( TAG, \"canonicalized SD configuration but could not remove the source file\" );\n        return ESP_FAIL;\n    }\n\n    import_pending = false;\n    ESP_LOGI( TAG, \"completed SD configuration import\" );\n    return ESP_OK;\n}\n\n''',
    "import finalize API",
)
p.write_text(s)

# config_store.h: expose pending/finalize semantics rather than claiming the source
# was already consumed.
p = Path("main/config_store.h")
s = p.read_text()
s = once(
    s,
    '''/** @brief Return whether this boot consumed an SD configuration import. */\nbool config_store_imported( void );\n\n''',
    '''/** @brief Return whether a validated SD import is awaiting canonicalization/finalization. */\nbool config_store_import_pending( void );\n\n/** @brief Consume the SD import after its internal copy has been canonicalized successfully. */\nesp_err_t config_store_finalize_import( void );\n\n''',
    "header import API",
)
p.write_text(s)

# device_config.c: canonicalize first, then consume SD and emit IMPORTED only after
# both sides of the transaction complete.
p = Path("main/device_config.c")
s = p.read_text()
s = s.replace("config_store_imported()", "config_store_import_pending()")
s = once(
    s,
    '''                if( config_store_import_pending() )\n                {\n                    rfscout_event_emit( RFSCOUT_EVENT_CONFIG_IMPORTED,\n                                        RFSCOUT_EVENT_SOURCE_SYSTEM,\n                                        (int32_t)RFSCOUT_CONFIG_SCHEMA_VERSION );\n                }\n''',
    '''                if( config_store_import_pending() )\n                {\n                    const esp_err_t finalize_result = config_store_finalize_import();\n                    if( ESP_OK == finalize_result )\n                    {\n                        rfscout_event_emit( RFSCOUT_EVENT_CONFIG_IMPORTED,\n                                            RFSCOUT_EVENT_SOURCE_SYSTEM,\n                                            (int32_t)RFSCOUT_CONFIG_SCHEMA_VERSION );\n                    }\n                    else\n                    {\n                        ESP_LOGW( TAG,\n                                  \"configuration import finalization failed: %s\",\n                                  esp_err_to_name( finalize_result ) );\n                    }\n                }\n''',
    "device config finalize",
)
p.write_text(s)

# Documentation: make transaction boundary and startup-snapshot semantics explicit.
for name in ["README.md", "docs/ARCHITECTURE.md", "docs/CAPABILITIES.md", "docs/CONFIGURATION.md"]:
    p = Path(name)
    s = p.read_text()
    s = s.replace(
        "`/sdcard/rfscout-example.json` mirrors the current internal configuration when removable storage is available.",
        "`/sdcard/rfscout-example.json` is synchronized from the current internal configuration at startup when removable storage is available.",
    )
    s = s.replace(
        "`/sdcard/rfscout-example.json` mirrors the authoritative internal file when SD is available and is rewritten only when its contents differ.",
        "`/sdcard/rfscout-example.json` is synchronized from the authoritative internal file at startup when SD is available and is rewritten only when its contents differ.",
    )
    s = s.replace(
        "`/sdcard/rfscout-example.json` mirrors the authoritative internal configuration when SD is available and is rewritten only when its contents differ.",
        "`/sdcard/rfscout-example.json` is synchronized from the authoritative internal configuration at startup when SD is available and is rewritten only when its contents differ.",
    )
    s = s.replace(
        "5. delete the SD import only after the internal commit succeeds;",
        "5. load and canonicalize the imported configuration internally, then delete the SD import only after that canonical save succeeds;",
    )
    s = s.replace(
        "A successful import consumes and removes the file.",
        "A successful import consumes and removes the file only after the internal copy has been parsed and canonicalized successfully.",
    )
    p.write_text(s)
