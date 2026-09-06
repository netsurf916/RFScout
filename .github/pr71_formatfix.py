from pathlib import Path
p = Path('main/device_config.c')
s = p.read_text()
s = s.replace('            {\n                            if( config_store_import_pending() )\n', '            {\n                if( config_store_import_pending() )\n')
s = s.replace('        config_dirty = false;\n            config_source_version = (int)RFSCOUT_CONFIG_SCHEMA_VERSION;\n', '        config_dirty = false;\n        config_source_version = (int)RFSCOUT_CONFIG_SCHEMA_VERSION;\n')
p.write_text(s)
